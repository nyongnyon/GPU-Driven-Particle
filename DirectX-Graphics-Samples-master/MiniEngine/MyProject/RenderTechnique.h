#pragma once
#include "ColorBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "imgui.h"

class GraphicsPSO;
class DepthBuffer;

struct ParticleRenderTarget
{
    ColorBuffer& color;
    DepthBuffer& depth;
};

class RenderTechnique
{
public:
    virtual ~RenderTechnique() = default;

    virtual void Initialize() {}
    virtual void Shutdown() {}

    virtual void ConfigurePSO(GraphicsPSO& pso) const = 0;

    // true면 Renderer가 draw 직전에 깊이 정렬(back-to-front)된 입력으로 교체한다
    virtual bool RequiresDepthSort() const { return false; }

    virtual void BeginDraw(GraphicsContext& gfx, const ParticleRenderTarget& target) {}
    virtual void EndDraw(GraphicsContext& gfx, const ParticleRenderTarget& target) {}

    // Optional debug pass after EndDraw (not inside any measured timer)
    virtual void DrawDebug(GraphicsContext& gfx, const ParticleRenderTarget& target) {}
};

// SinglePass 전용
class SinglePassTechnique : public RenderTechnique
{
public:
    explicit SinglePassTechnique(const D3D12_BLEND_DESC& blend, bool depthSorted = false)
        : m_Blend(blend), m_DepthSorted(depthSorted) {}
    void ConfigurePSO(GraphicsPSO& pso) const override;
    bool RequiresDepthSort() const override { return m_DepthSorted; }


private:
    D3D12_BLEND_DESC m_Blend;
    bool             m_DepthSorted = false;
};

// WBOIT 전용, multi pass, multi rtv
class WBOITTechnique : public RenderTechnique
{
public:
    void Initialize() override;
    void Shutdown() override;

    void ConfigurePSO(GraphicsPSO& pso) const override;

    void BeginDraw(GraphicsContext& gfx, const ParticleRenderTarget& target) override;
    void EndDraw(GraphicsContext& gfx, const ParticleRenderTarget& target) override;
    void DrawDebug(GraphicsContext& gfx, const ParticleRenderTarget& target) override;

    // Debug views of the render targets (Accum / Revealage), shown with ImGui::Image
    enum DebugView : uint32_t { kDebugColor = 0, kDebugWeight, kDebugReveal, kNumDebugViews };

    struct DebugOptions
    {
        bool    enabled = false;        // false = the debug pass does not run at all
        float   weightRange = 100.f;    // accum.a shown as white (log scale)
    };

    DebugOptions&       GetDebugOptions() { return m_DebugOptions; }
    ImTextureID         GetDebugTexture(uint32_t view) const { return view < kNumDebugViews ? m_DebugTex[view] : ImTextureID_Invalid; }
    uint32_t            GetDebugWidth() const { return m_DebugTargets[0].GetWidth(); }
    uint32_t            GetDebugHeight() const { return m_DebugTargets[0].GetHeight(); }
    static const char*  DebugViewName(uint32_t view);

private:
    void EnsureSize(uint32_t width, uint32_t height);
    void EnsureDebugSize(uint32_t width, uint32_t height);
    void DestroyDebugTargets();

    static constexpr DXGI_FORMAT kAccumFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT kRevealFormat = DXGI_FORMAT_R8_UNORM;

    ColorBuffer     m_Accum;
    ColorBuffer     m_Reveal;

    RootSignature   m_CompositeSig;
    GraphicsPSO     m_CompositePSO{ L"Particle WBOIT Composite PSO" };

    static constexpr DXGI_FORMAT kDebugFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    DebugOptions    m_DebugOptions;
    ColorBuffer     m_DebugTargets[kNumDebugViews];
    ImTextureID     m_DebugTex[kNumDebugViews] = { ImTextureID_Invalid, ImTextureID_Invalid, ImTextureID_Invalid };
    RootSignature   m_DebugSig;
    GraphicsPSO     m_DebugPSO{ L"Particle WBOIT Debug PSO" };
};
