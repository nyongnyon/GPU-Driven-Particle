#include "ParticleTiledCommon.hlsli"

StructuredBuffer<Particle> Particles : register(t0);
ByteAddressBuffer SortedList : register(t1);
ByteAddressBuffer DrawArgs : register(t2);

RWStructuredBuffer<TiledParticle> TiledOut : register(u1);
RWByteAddressBuffer TileBatchMask : register(u3);

uint NearToFarSlot(uint j, uint count)
{
    return SortedList.Load((count - 1 - j) * 8);
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint count = DrawArgs.Load(4);
    const uint id = DTid.x;
    if (id >= count)
        return;
    
    const TiledParticle tp = BuildTiledParticle(Particles[NearToFarSlot(id, count)]);
    TiledOut[id] = tp;
    
    if (tp.quad.halfExt.x <= 0.f)
        return;
    
    const float2 pMin = tp.quad.center - tp.quad.halfExt;
    const float2 pMax = tp.quad.center + tp.quad.halfExt;
    const int2 tMin = max((int2) floor(pMin / TILE_SIZE_PX), 0);
    const int2 tMax = min((int2) floor(pMax / TILE_SIZE_PX), (int2) TileCount - 1);

    const uint wordsPerTile = WordsPerTile(count);  // maxParticles(50000) / (256 * 32)의 올림 = 7? 8?
    const uint batch = id / BATCH_SIZE;     // id / 256 = 내 batch id, 5번가정
    const uint wordInTile = batch / BATCHES_PER_WORD;   // 5번 / 32 = 0
    const uint bit = 1u << (batch % BATCHES_PER_WORD);
    
    for (int y = tMin.y; y <= tMax.y; ++y)
    {
        for (int x = tMin.x; x <= tMax.x; ++x)
        {
            const float2 tileMin = float2(x, y) * TILE_SIZE_PX;
            if (!OverlapsTile(tp, tileMin, tileMin + TILE_SIZE_PX))   // 타일 셰이더와 "같은 함수"로 판정
                continue;

            const uint tileIndex = y * TileCount.x + x;
            uint origin;
            TileBatchMask.InterlockedOr((tileIndex * wordsPerTile + wordInTile) * 4, bit, origin);
        }
    }
}

