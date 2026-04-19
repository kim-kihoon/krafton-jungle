#include "GameViewportClient.h"

#include "Core/Engine.h"
#include "Input/InputManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "World/World.h"
#include "Scene/Level.h"
#include "Component/CameraComponent.h"
#include "Math/Frustum.h"
#include "Core/ShowFlags.h"

void FGameViewportClient::Attach(FEngine* Engine, FRenderer* Renderer)
{
	if (Renderer)
	{
		// Renderer->ClearViewportCallbacks();
	}
}

void FGameViewportClient::Detach(FEngine* Engine, FRenderer* Renderer)
{
	if (Renderer)
	{
		// Renderer->ClearViewportCallbacks();
	}
}

void FGameViewportClient::Tick(FEngine* Engine, float DeltaTime)
{
	IViewportClient::Tick(Engine, DeltaTime);
}

void FGameViewportClient::HandleMessage(FEngine* Engine, HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
	if (!Engine) return;

	FInputManager* InputManager = Engine->GetInputManager();
	if (InputManager)
	{
		InputManager->ProcessMessage(Hwnd, Msg, WParam, LParam);
	}
}

void FGameViewportClient::BuildRenderCommands(FEngine* Engine, ULevel* Level, const FFrustum& Frustum,
	const FShowFlags& Flags, const FVector& CameraPosition, FRenderCommandQueue& OutQueue)
{
	IViewportClient::BuildRenderCommands(Engine, Level, Frustum, Flags, CameraPosition, OutQueue);
}

void FGameViewportClient::Render(FEngine* Engine, FRenderer* Renderer)
{
	if (!Engine || !Renderer) return;

	ULevel* Level = ResolveLevel(Engine);
	if (!Level) return;

	UWorld* ActiveWorld = ResolveWorld(Engine);
	if (!ActiveWorld) return;

	UCameraComponent* ActiveCamera = ActiveWorld->GetActiveCameraComponent();
	if (!ActiveCamera) return;

	FRenderCommandQueue Queue;
	Queue.Reserve(Renderer->GetPrevCommandCount());
	Queue.ViewMatrix = ActiveCamera->GetViewMatrix();
	Queue.ProjectionMatrix = ActiveCamera->GetProjectionMatrix();

	FFrustum Frustum;
	Frustum.ExtractFromVP(Queue.ViewMatrix * Queue.ProjectionMatrix);

	const FVector CameraPosition = Queue.ViewMatrix.GetInverse().GetTranslation();

	FShowFlags GameShowFlags;
	// 에디터 전용 시각화(카메라 아이콘, 방향 화살표 등)는 PIE에서 렌더링하지 않는다.
	GameShowFlags.SetFlag(EEngineShowFlags::SF_EditorActorVisualization, false);
	// UUID 텍스트는 PIE 중에도 일반 액터(StaticMesh 등)는 보이도록 허용한다.
	// (CameraActor의 UUID만 숨기는 로직은 RenderCollector에서 별도로 처리됨)
	// GameShowFlags.SetFlag(EEngineShowFlags::SF_UUID, false);
	BuildRenderCommands(Engine, Level, Frustum, GameShowFlags, CameraPosition, Queue);

	Renderer->SubmitCommands(Queue);
	Renderer->ExecuteCommands();
}
