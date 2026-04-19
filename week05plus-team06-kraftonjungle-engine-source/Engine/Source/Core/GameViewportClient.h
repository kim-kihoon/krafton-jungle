#pragma once
#include "Core/ViewportClient.h"
#include "Types/CoreTypes.h"

class FEngein;
class FRenderer;
class ULevel;
class FFrustum;
struct FRenderCommandQueue;
class FShowFlags;

class ENGINE_API FGameViewportClient : public IViewportClient
{
public:
	void Attach(FEngine* Engine, FRenderer* Renderer) override;
	void Detach(FEngine* Engine, FRenderer* Renderer) override;
	void Tick(FEngine* Engine, float DeltaTime) override;

	void HandleMessage(FEngine* Engine, HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam) override;

	/*void HandleFileDoubleClick(const FString& FilePath) override;
	void HandleFileDropOnViewport(const FString& FilePath) override;*/

	void BuildRenderCommands(FEngine* Engine, ULevel* Level, const FFrustum& Frustum, const FShowFlags& Flags, const FVector& CameraPosition, FRenderCommandQueue& OutQueue) override;
	void Render(FEngine* Engine, FRenderer* Renderer) override;
};
