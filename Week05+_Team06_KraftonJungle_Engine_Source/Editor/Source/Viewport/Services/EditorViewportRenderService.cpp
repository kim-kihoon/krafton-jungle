#include "Viewport/Services/EditorViewportRenderService.h"

#include "EditorEngine.h"
#include "Viewport/EditorViewportRegistry.h"
#include "Actor/Actor.h"
#include "Core/Engine.h"
#include "Gizmo/Gizmo.h"
#include "Math/Frustum.h"
#include "Renderer/Material.h"
#include "Renderer/Renderer.h"
#include "Scene/Level.h"
#include "Camera/Camera.h"
#include "UI/EditorUI.h"
#include "Viewport/BlitRenderer.h"
#include "Viewport/Viewport.h"
#include "Component/SkyComponent.h"
#include "Component/CameraComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Asset/ObjManager.h"
#include "Slate/Widget/Painter.h"

namespace
{
	void BuildGridVectors(const FRenderCommandQueue& Queue, const FViewportLocalState& LocalState, FVector& OutGridAxisU, FVector& OutGridAxisV, FVector& OutViewForward)
	{
		const FMatrix ViewInverse = Queue.ViewMatrix.GetInverse();
		OutViewForward = ViewInverse.GetForwardVector().GetSafeNormal();

		if (LocalState.ProjectionType == EViewportType::Perspective)
		{
			OutGridAxisU = FVector::ForwardVector;
			OutGridAxisV = FVector::RightVector;
			return;
		}

		OutGridAxisU = ViewInverse.GetRightVector().GetSafeNormal();
		OutGridAxisV = ViewInverse.GetUpVector().GetSafeNormal();
	}
}


void FEditorViewportRenderService::RenderAll(
	FEngine* Engine,
	FRenderer* Renderer,
	FEditorEngine* EditorEngine,
	FEditorViewportRegistry& ViewportRegistry,
	FEditorUI& EditorUI,
	FGizmo& Gizmo,
	FBlitRenderer& BlitRenderer,
	const std::shared_ptr<FMaterial>& WireFrameMaterial,
	FRenderMesh* GridMesh,
	FMaterial* GridMaterial,
	const FBuildRenderCommands& BuildRenderCommands) const
{
	if (!Engine || !Renderer || !EditorEngine)
	{
		return;
	}

	ID3D11Device* Device = Renderer->GetDevice();
	ID3D11DeviceContext* Context = Renderer->GetDeviceContext();
	if (!Device || !Context)
	{
		return;
	}

	ULevel* Level = Engine->GetLevel();
	if (!Level)
	{
		return;
	}

	constexpr float ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
	const TArray<FViewportEntry>& Entries = ViewportRegistry.GetEntries();

	const FViewportId PIEViewportId = EditorEngine->GetPIEViewportId();

	for (const FViewportEntry& Entry : Entries)
	{
		if (!Entry.bActive || !Entry.Viewport)
		{
			continue;
		}

		Entry.Viewport->EnsureResources(Device);

		ID3D11RenderTargetView* RTV = Entry.Viewport->GetRTV();
		ID3D11DepthStencilView* DSV = Entry.Viewport->GetDSV();
		if (!RTV || !DSV)
		{
			continue;
		}

		const FRect& Rect = Entry.Viewport->GetRect();
		D3D11_VIEWPORT Viewport = {};
		Viewport.TopLeftX = 0.0f;
		Viewport.TopLeftY = 0.0f;
		Viewport.Width = static_cast<float>(Rect.Width);
		Viewport.Height = static_cast<float>(Rect.Height);
		Viewport.MinDepth = 0.0f;
		Viewport.MaxDepth = 1.0f;

		Context->ClearRenderTargetView(RTV, ClearColor);
		Context->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		ULevel* TargetLevel = Engine->GetLevel();
		bool bUseGameCamera = false;

		if (EditorEngine->IsPlayingInEditor())
		{
			// [분할 PIE 복구]: PIE용 뷰포트만 라이브 레벨을 사용하고, 나머지는 에디터 레벨(정지)을 사용
			if (Entry.Id == PIEViewportId)
			{
				TargetLevel = EditorEngine->GetActiveLevel(); // PIE 라이브 레벨
				bUseGameCamera = true;
			}
			else
			{
				TargetLevel = EditorEngine->GetEditorLevel(); // 에디터 정지 레벨
			}
		}

		Renderer->SetCurrentRenderingLevel(TargetLevel);
		Renderer->BeginLevelPass(RTV, DSV, Viewport);

		const float AspectRatio = static_cast<float>(Rect.Width) / static_cast<float>(Rect.Height);
		FRenderCommandQueue Queue;
		Queue.Reserve(Renderer->GetPrevCommandCount());

		if (bUseGameCamera)
		{
			UWorld* PIEWorld = EditorEngine->GetActiveWorld();
			if (PIEWorld && PIEWorld->GetActiveCameraComponent() && PIEWorld->GetActiveCameraComponent()->GetCamera())
			{
				FCamera* GameCamera = PIEWorld->GetActiveCameraComponent()->GetCamera();
				GameCamera->SetAspectRatio(AspectRatio);
				Queue.ProjectionMatrix = GameCamera->GetProjectionMatrix();
				Queue.ViewMatrix = GameCamera->GetViewMatrix();
			}
			else bUseGameCamera = false;
		}
		if (!bUseGameCamera)
		{
			Queue.ProjectionMatrix = Entry.LocalState.BuildProjMatrix(AspectRatio);
			Queue.ViewMatrix = Entry.LocalState.BuildViewMatrix();
		}

		FFrustum Frustum;
		Frustum.ExtractFromVP(Queue.ViewMatrix * Queue.ProjectionMatrix);
		const FVector CameraPosition = Queue.ViewMatrix.GetInverse().GetTranslation();

		// PIE 상태라면 모든 뷰포트에서 에디터 요소(빌보드, UUID, 시각화 아이콘 등) 플래그를 끈다.
		FShowFlags RenderShowFlags = Entry.LocalState.ShowFlags;
		const bool bIsPIE = EditorEngine->IsPlayingInEditor();
		const bool bIsPIEViewport = bIsPIE && (Entry.Id == PIEViewportId);

		if (bIsPIE)
		{
			// [수정]: PIE 중에도 일반 액터(StaticMesh 등)의 UUID는 보이도록 SF_UUID를 유지하거나 켭니다.
			// 카메라 액터의 UUID는 RenderCollector에서 개별적으로 숨겨집니다.
			// [수정]: PIE 중에도 게임용 빌보드(SubUV 등)가 보일 수 있도록 SF_Billboard는 끄지 않습니다.
			// 에디터 아이콘 등은 IsEditorOnly() 체크와 SF_EditorActorVisualization에 의해 필터링됩니다.
			RenderShowFlags.SetFlag(EEngineShowFlags::SF_EditorActorVisualization, false);
			
			// 에디터 설정에서 UUID가 켜져있다면 PIE 화면에서도 강제로 출력되도록 보장
			if (Entry.LocalState.ShowFlags.HasFlag(EEngineShowFlags::SF_UUID))
			{
				RenderShowFlags.SetFlag(EEngineShowFlags::SF_UUID, true);
			}
		}

		BuildRenderCommands(Engine, TargetLevel, Frustum, RenderShowFlags, CameraPosition, Queue);

		AActor* GizmoTarget = EditorEngine->GetSelectedActor();
		const bool bGizmoTargetInCorrectWorld = GizmoTarget && (GizmoTarget->GetLevel() == TargetLevel);

		// [수정]: PIE 메인 뷰포트가 아닐 때만 기즈모를 렌더링 (나머지 3개 에디터 뷰포트용)
		if (bGizmoTargetInCorrectWorld && GizmoTarget->GetComponentByClass<USkyComponent>() == nullptr && !bIsPIEViewport)
		{
			Gizmo.BuildRenderCommands(GizmoTarget, &Entry, Queue);
		}

		if (Entry.LocalState.ViewMode == ERenderMode::Wireframe && WireFrameMaterial)
		{
			ApplyWireframe(Queue, WireFrameMaterial.get());
		}

		// [수정]: PIE 메인 뷰포트가 아닐 때만 그리드를 렌더링 (또는 에디터 설정에 따라)
		if (Entry.LocalState.bShowGrid && GridMesh && GridMaterial && !bIsPIEViewport)
		{
			FVector GridAxisU = FVector::ForwardVector;
			FVector GridAxisV = FVector::RightVector;
			FVector ViewForward = FVector::ForwardVector;
			BuildGridVectors(Queue, Entry.LocalState, GridAxisU, GridAxisV, ViewForward);

			GridMaterial->SetParameterData("GridSize", &Entry.LocalState.GridSize, 4);
			GridMaterial->SetParameterData("LineThickness", &Entry.LocalState.LineThickness, 4);
			GridMaterial->SetParameterData("GridAxisU", &GridAxisU, sizeof(FVector));
			GridMaterial->SetParameterData("GridAxisV", &GridAxisV, sizeof(FVector));
			GridMaterial->SetParameterData("ViewForward", &ViewForward, sizeof(FVector));

			FRenderCommand GridCommand;
			GridCommand.RenderMesh = GridMesh;
			GridCommand.Material = GridMaterial;
			GridCommand.WorldMatrix = FMatrix::Identity;
			GridCommand.RenderLayer = ERenderLayer::Default;
			Queue.AddCommand(GridCommand);
		}

		Renderer->SubmitCommands(Queue);
		Renderer->ExecuteCommands();
		EditorEngine->FlushDebugDrawForViewport(Renderer, Entry.LocalState.ShowFlags, false);
		Renderer->EndLevelPass();
	}
	EditorEngine->ClearDebugDrawForFrame();

	Renderer->BindSwapChainRTV();
	BlitRenderer.BlitAll(Context, Entries);

	Renderer->BindSwapChainRTV();
	if (FSlateApplication* Slate = EditorEngine->GetSlateApplication())
	{
		FPainter Painter(Renderer);

		RECT rc{};
		::GetClientRect(Renderer->GetHwnd(), &rc);
		Painter.SetScreenSize(rc.right - rc.left, rc.bottom - rc.top);
		Slate->Paint(Painter);
		Painter.Flush();
	}

	EditorUI.Render();
}

void FEditorViewportRenderService::ApplyWireframe(FRenderCommandQueue& Queue, FMaterial* WireMaterial)
{
	for (FRenderCommand& Command : Queue.Commands)
	{
		if (Command.RenderLayer != ERenderLayer::Overlay)
		{
			Command.Material = WireMaterial;
		}
	}
}
