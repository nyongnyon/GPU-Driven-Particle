#include "pch.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "GameInput.h"
#include "CommandContext.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "BufferManager.h"

#include "PostEffects.h"
#include "Renderer.h"
#include "Camera.h"
#include "CameraController.h"
#include "LightManager.h"
#include "DebugDraw.h"
#include "TexturedQuad.h"
#include "ImGuiLayer.h"

using namespace GameCore;
using namespace Graphics;

class MyProject : public GameCore::IGameApp
{
public:

    MyProject()
    {
    }
    virtual ~MyProject()
    {

    }

    virtual void Startup( void ) override;
    virtual void Cleanup( void ) override;

    virtual void Update( float deltaT ) override;
    virtual void RenderScene( void ) override;

    virtual bool RequiresImGui() const override { return true; }

private:
    void UpdateImGui( float deltaT );

    bool m_ShowImGuiDemo = false;

    Camera m_Camera;
    std::unique_ptr<CameraController> m_CameraController;

    TexturedQuad m_TempQuad;
};

CREATE_APPLICATION( MyProject )

void MyProject::Startup( void )
{
    // Rendering Setting
    //g_SceneColorBuffer.SetClearColor(Color(0.5f, 0.5f, 0.4f));
    PostEffects::EnableAdaptation = true;

    Renderer::Initialize();
    DebugDraw::Initialize();
    QuadRenderer::Initialize();

    // Camera Setting
    m_Camera.SetZRange(0.1f, 1000.f);
    m_CameraController.reset(new OrbitCamera(m_Camera, BoundingSphere(0.f, 0.f, 0.f, 10.f), Vector3(Math::kYUnitVector)));

    // Setup your data
    m_TempQuad.Load(L"Assets/Textures/image.png", false);
    m_TempQuad.SetTint(Color(1.f, 1.f, 1.f));
    m_TempQuad.SetTransform(Vector3(0.f, 2.f, 0.f), 0.f, 5.f);
}

void MyProject::Cleanup( void )
{
    // Free up resources in an orderly fashion
    m_TempQuad.Release();
    m_CameraController.reset();
    QuadRenderer::Shutdown();
    DebugDraw::Shutdown();
    Lighting::Shutdown(); // temp
    Renderer::Shutdown();
}

void MyProject::Update( float deltaT )
{
    ScopedTimer _prof(L"Update State");

    // Update something
    m_CameraController->Update(deltaT);

    DebugDraw::AddLine(Vector3(0.f, 0.f, 0.f), Vector3(0.f, 5.f, 0.f), Color(1.f, 1.f, 1.f));
    DebugDraw::AddLine(Vector3(-1.f, 0.f, 0.f), Vector3(1.f, 0.f, 0.f), Color(1.f, 1.f, 1.f));

    UpdateImGui(deltaT);




}

void MyProject::UpdateImGui( float deltaT )
{
    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Debug");
    ImGui::Text("%.2f ms (%.1f FPS)", deltaT * 1000.f, deltaT > 0.f ? 1.f / deltaT : 0.f);
    ImGui::Text("Mode: %s  [F1 to toggle]", ImGuiLayer::IsCameraMode() ? "Camera" : "UI");
    ImGui::Checkbox("Show ImGui Demo", &m_ShowImGuiDemo);
    ImGui::End();

    if (m_ShowImGuiDemo)
        ImGui::ShowDemoWindow(&m_ShowImGuiDemo);
}

void MyProject::RenderScene( void )
{
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);

    gfxContext.ClearColor(g_SceneColorBuffer);
    gfxContext.ClearDepth(g_SceneDepthBuffer);

    gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV());
    gfxContext.SetViewportAndScissor(0, 0, g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());

    // Rendering something
    DebugDraw::Render(gfxContext, m_Camera.GetViewProjMatrix());
    QuadRenderer::BeginDraw(gfxContext, m_Camera.GetViewProjMatrix());
    m_TempQuad.Draw(gfxContext);



    // Finish
    gfxContext.Finish();
}
