#include "Game/Render/GameRenderPipeline.h"

#include "Game/GameEngine.h"
#include "Game/UI/GameUISystem.h"
#include "Game/Viewport/GameViewportClient.h"
#include "GameFramework/AActor.h"
#include "Render/Renderer/Renderer.h"
#include "GameFramework/World.h"
#include "Math/Vector4.h"
#include "Runtime/SceneView.h"
#include "Core/Logging/Stats.h"
#include "Core/Logging/GPUProfiler.h"

namespace
{
	FShowFlags MakeGameShowFlags()
	{
		FShowFlags ShowFlags;
		ShowFlags.bShadow = true;
		return ShowFlags;
	}

	constexpr EShadowFilterType GameShadowFilterType = EShadowFilterType::ESM;
}

FGameRenderPipeline::FGameRenderPipeline(UGameEngine* InEngine, FRenderer& InRenderer)
	: Engine(InEngine)
{
	Collector.Initialize(InRenderer.GetFD3DDevice().GetDevice());
}

FGameRenderPipeline::~FGameRenderPipeline()
{
	Collector.Release();
}

void FGameRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
	(void)DeltaTime;

#if STATS
	FStatManager::Get().TakeSnapshot();
	FGPUProfiler::Get().TakeSnapshot();
#endif

	if (!Engine->GetWorld())
	{
		return;
	}

	Renderer.BeginFrame();
	RenderViewport(Renderer);
	Renderer.EndFrame();
}

void FGameRenderPipeline::RenderViewport(FRenderer& Renderer)
{
	FSceneView SceneView;
	FGameViewportClient* Viewport = nullptr;
	if (!PrepareViewport(Renderer, SceneView, Viewport))
	{
		return;
	}

	UWorld* World = Viewport->GetFocusedWorld();
	if (!World)
	{
		return;
	}

	const FShowFlags ShowFlags = MakeGameShowFlags();

	// 씬 수집 (기즈모/선택/그리드/에디터 오버레이 생략)
	Renderer.GetEditorLineBatcher().Clear();
	Collector.SetLineBatcher(&Renderer.GetEditorLineBatcher());
	Collector.CollectWorld(World, ShowFlags, SceneView.ViewMode, Bus, &SceneView.CameraFrustum);
	if (AActor* HoveredActor = Viewport->GetPlayerController().GetHoveredPickableActor())
	{
		TArray<AActor*> HoveredActors = { HoveredActor };
		Collector.CollectOutline(HoveredActors, FVector4(1.0f, 0.92f, 0.05f, 1.0f), 5.0f, Bus);
	}
	if (AActor* HeldActor = Viewport->GetPlayerController().GetHeldNonCleaningToolActor())
	{
		TArray<AActor*> HeldActors = { HeldActor };
		Collector.CollectOutline(HeldActors, FVector4(1.0f, 0.05f, 0.02f, 1.0f), 5.0f, Bus);
	}

	Renderer.PrepareBatchers(Bus);
	Renderer.Render(Bus);
	Renderer.PresentToBackBuffer(Renderer.GetCurrentSceneSRV());

	Renderer.RenderToCurrentTarget([](int32 Width, int32 Height)
	{
		GameUISystem::Get().RenderToCurrentTarget(EUIRenderMode::Play, Width, Height);
	});
}

bool FGameRenderPipeline::PrepareViewport(FRenderer& Renderer, FSceneView& OutSceneView, FGameViewportClient*& OutViewportClient)
{
	OutViewportClient = Engine->GetGameViewport();
	if (!OutViewportClient)
	{
		return false;
	}

	OutViewportClient->BuildSceneView(OutSceneView);

	const FViewportRect& Rect = OutSceneView.ViewRect;
	if (Rect.Width <= 0 || Rect.Height <= 0)
	{
		return false;
	}

	FViewportRenderResource& ViewportResource = Renderer.AcquireViewportResource(static_cast<uint32>(Rect.Width), static_cast<uint32>(Rect.Height), 0);
	FRenderTargetSet& RenderTargets = ViewportResource.GetView();

	Renderer.BeginViewportFrame(&RenderTargets);

	const FShowFlags ShowFlags = MakeGameShowFlags();
	Bus.Clear();
	Bus.SetViewProjection(OutSceneView.ViewMatrix, OutSceneView.ProjectionMatrix);
	Bus.SetCameraPlane(OutSceneView.NearPlane, OutSceneView.FarPlane);
	Bus.SetRenderSettings(OutSceneView.ViewMode, ShowFlags);
	Bus.SetShadowFilterType(GameShadowFilterType);
	Bus.SetViewportSize(FVector2(static_cast<float>(Rect.Width), static_cast<float>(Rect.Height)));
	Bus.SetViewportOrigin(FVector2(0.0f, 0.0f));
	Bus.SetFXAAEnabled(!OutSceneView.bOrthographic);

	return true;
}
