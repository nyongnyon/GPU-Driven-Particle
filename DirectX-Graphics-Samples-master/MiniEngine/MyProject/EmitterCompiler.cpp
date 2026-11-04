#include "pch.h"
#include "EmitterCompiler.h"

#include "LayoutBuilder.h"
#include "ModuleDesc.h"

#include <unordered_map>

CompiledEmitter EmitterCompiler::Compile() const
{
    CompiledEmitter out;

    struct Entry 
    {
        AttributeKey key;
        AttrType     type;
        int          order;
        bool         read = false, written = false;
        std::string  firstUser;
    };
    std::unordered_map<std::string, Entry> table;
    int nextOrder = 0;

    // [1] 모으기와 규칙검사
    auto collect = [&](const Usage& u, const std::string& user, int level, Stage stage,
        bool isRead, bool isRenderer) {
            AttributeKey k = AttributeKey::Parse(u.attr);
            const std::string who = user + (isRenderer ? "" : std::string("(") + StageName(stage) + ")");

            if (k.ns == Namespace::Unknown || k.name.empty()) 
            {
                out.errors.push_back(who + ": '" + u.attr + "' 네임스페이스를 알 수 없음");
                return;
            }
            if (k.mod != Modifier::None && k.ns != Namespace::Particles) 
            {
                out.errors.push_back(who + ": '" + u.attr + "' Previous/Initial 은 Particles 에만 붙일 수 있음");
                return;
            }

            // 읽기 규칙: 자기보다 "아래"단계의 값은 못 읽는다
            if (isRead) 
            {
                if (k.ns == Namespace::Particles && level < 2)
                    out.errors.push_back(who + ": '" + u.attr + "' System/Emitter 단계에서는 파티클 값을 읽을 수 없음");
                if (k.ns == Namespace::Emitter && level < 1)
                    out.errors.push_back(who + ": '" + u.attr + "' System 단계에서는 Emitter 값을 읽을 수 없음");
                if (!isRenderer && stage == Stage::ParticleSpawn && k.mod == Modifier::Previous)
                    out.warnings.push_back(who + ": '" + u.attr + "' Spawn 단계에서는 Previous 가 아직 채워지지 않음");
                if (!isRenderer && stage == Stage::ParticleSpawn && k.mod == Modifier::Initial)
                    out.warnings.push_back(who + ": '" + u.attr + "' Initial 은 Spawn 이 끝날 때 복사되므로 Spawn 안에서는 아직 비어 있음");
            }
            // 쓰기 규칙: 자기 단계의 값만 쓸 수 있다
            else {
                if (k.ns == Namespace::Engine)
                    out.errors.push_back(who + ": '" + u.attr + "' Engine 값은 쓸 수 없음");
                if (k.ns == Namespace::User)
                    out.errors.push_back(who + ": '" + u.attr + "' User 값은 외부(블루프린트/C++)에서만 설정");
                if (k.mod != Modifier::None)
                    out.errors.push_back(who + ": '" + u.attr + "' Previous/Initial 은 엔진이 자동으로 채움 (직접 쓰기 금지)");
                if (k.ns == Namespace::Particles && level != 2)
                    out.errors.push_back(who + ": '" + u.attr + "' 파티클 값은 Particle 단계에서만 쓸 수 있음");
                if (k.ns == Namespace::Emitter && level != 1)
                    out.errors.push_back(who + ": '" + u.attr + "' Emitter 값은 Emitter 단계에서만 쓸 수 있음");
                if (k.ns == Namespace::System && level != 0)
                    out.errors.push_back(who + ": '" + u.attr + "' System 값은 System 단계에서만 쓸 수 있음");
            }

            // 중복 제거 (전체 이름 기준) + 타입 충돌 검사
            const std::string full = k.Full();
            auto it = table.find(full);
            if (it == table.end()) {
                Entry e{ k, u.type, nextOrder++, false, false, who };
                it = table.emplace(full, e).first;
            }
            else if (it->second.type != u.type) {
                out.errors.push_back("타입 충돌: '" + full + "' " + it->second.firstUser + " 는 " +
                    TypeName(it->second.type) + ", " + who + " 는 " + TypeName(u.type));
            }
            (isRead ? it->second.read : it->second.written) = true;
        };

    for (const auto& m : modules_) {
        int lv = StageLevel(m.stage);
        for (const auto& u : m.writes) collect(u, m.name, lv, m.stage, false, false);
        for (const auto& u : m.reads)  collect(u, m.name, lv, m.stage, true, false);
    }
    for (const auto& r : renderers_)
        for (const auto& u : r.reads) collect(u, r.name, 2, Stage::ParticleUpdate, true, true);

    // ---------- [2] 수식어 처리: 원본이 있어야 하고, 원본은 복사 때문에 "읽힘" ----------
    for (auto& [full, e] : table) {
        if (e.key.mod == Modifier::None || !e.read) continue;

        const std::string baseName = e.key.Base().Full();
        auto base = table.find(baseName);
        if (base == table.end() || !base->second.written) {
            out.errors.push_back("'" + full + "' 를 쓰려면 누군가 '" + baseName + "' 를 써야 함 (복사할 원본 없음)");
            continue;
        }
        if (base->second.type != e.type) {
            out.errors.push_back("'" + full + "' 와 원본 '" + baseName + "' 의 타입이 다름");
            continue;
        }
        base->second.read = true;   // 엔진 복사가 원본을 읽는다 → Trim 되지 않게
        e.written = true;           // 엔진이 채운다 → "아무도 안 씀" 경고 대상 아님
    }

    if (!out.Ok()) return out;

    // ---------- ③ 처음 등장 순서대로 저장 위치별 배정 ----------
    std::vector<const Entry*> ordered;
    for (const auto& [full, e] : table) ordered.push_back(&e);
    std::sort(ordered.begin(), ordered.end(),
        [](const Entry* a, const Entry* b) { return a->order < b->order; });

    for (const Entry* e : ordered) {
        const std::string full = e->key.Full();
        switch (e->key.GetStorage()) {
        case Storage::ParticleBuffer:
            if (trim_ && !e->read) { out.trimmed.push_back(full); break; }   // Trim
            if (e->read && !e->written)
                out.warnings.push_back("'" + full + "' 읽기만 하고 아무도 쓰지 않음 (기본값 사용)");
            out.particles.Add(e->key, e->type);
            break;
        case Storage::EmitterStore:
            if (e->read && !e->written)
                out.warnings.push_back("'" + full + "' 읽기만 하고 아무도 쓰지 않음 (기본값 사용)");
            out.emitterParams.Add(e->key, e->type);
            break;
        case Storage::SystemStore:
            out.systemParams.Add(e->key, e->type);
            break;
        case Storage::Temporary:
            if (e->read && !e->written)
                out.warnings.push_back("'" + full + "' 임시값을 읽지만 아무도 쓰지 않음");
            out.temporaries.push_back(full);
            break;
        case Storage::EngineProvided:
            out.engineValues.push_back(full);
            break;
        default:
            break;
        }
    }

    // ---------- ④ 복사 규칙 생성 (레이아웃이 확정된 뒤라 줄 번호를 알 수 있음) ----------
    for (const Slot& s : out.particles.slots) {
        if (s.key.mod == Modifier::None) continue;
        const Slot* base = out.particles.Find(s.key.Base().Full());
        CopyOp op{ base->key.Full(), s.key.Full(), base->start, s.start, ComponentCount(s.type) };
        if (s.key.mod == Modifier::Previous) out.copyAtUpdateStart.push_back(op);
        else                                 out.copyAtSpawnEnd.push_back(op);
    }
    return out;
}