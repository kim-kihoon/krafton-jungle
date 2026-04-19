#include "ViewportClient.h"
#include "World/World.h"
#include "Input/InputManager.h"
#include "Camera/Camera.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Material.h"
#include "Scene/Level.h"
#include "Debug/EngineLog.h"
#include "Component/UUIDBillboardComponent.h"
#include "Component/SubUVComponent.h"
#include "Core/Engine.h"
#include "Component/TextRenderComponent.h"
#include "Component/CameraComponent.h"
#include "Math/Frustum.h"


void IViewportClient::Attach(FEngine* Engine, FRenderer* Renderer)
{
}

void IViewportClient::Detach(FEngine* Engine, FRenderer* Renderer)
{
}

void IViewportClient::Tick(FEngine* Engine, float DeltaTime)
{
}

void IViewportClient::HandleMessage(FEngine* Engine, HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
}

ULevel* IViewportClient::ResolveLevel(FEngine* Engine) const
{
	return Engine ? Engine->GetActiveLevel() : nullptr;
}

UWorld* IViewportClient::ResolveWorld(FEngine* Engine) const
{
	return Engine ? Engine->GetActiveWorld() : nullptr;
}

void IViewportClient::BuildRenderCommands(FEngine* Engine, ULevel* Level, const FFrustum& Frustum, const FShowFlags& Flags, const FVector& CameraPosition, FRenderCommandQueue& OutQueue)
{
	UWorld* World = nullptr;
	if (Level)
	{
		World = Level->GetTypedOuter<UWorld>();
	}

	if (!World)
	{
		World = ResolveWorld(Engine);
	}

	if (!World) return;

	// Persistent + Streaming 전체 액터를 렌더
	TArray<AActor*> AllActors = World->GetAllActors();
	RenderCollector.CollectRenderCommands(AllActors, Frustum, Flags, CameraPosition, OutQueue);
}

void IViewportClient::HandleFileDoubleClick(const FString& FilePath)
{

}

void IViewportClient::HandleFileDropOnViewport(const FString& FilePath)
{

}

void IViewportClient::Render(FEngine* Engine, FRenderer* Renderer)
{
}

