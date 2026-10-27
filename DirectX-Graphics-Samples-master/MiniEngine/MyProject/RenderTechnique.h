#pragma once
#include "ColorBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"

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

private:
    void EnsureSize(uint32_t width, uint32_t height);

    static constexpr DXGI_FORMAT kAccumFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT kRevealFormat = DXGI_FORMAT_R8_UNORM;

    ColorBuffer     m_Accum;
    ColorBuffer     m_Reveal;

    RootSignature   m_CompositeSig;
    GraphicsPSO     m_CompositePSO{ L"Particle WBOIT Composite PSO" };
};
