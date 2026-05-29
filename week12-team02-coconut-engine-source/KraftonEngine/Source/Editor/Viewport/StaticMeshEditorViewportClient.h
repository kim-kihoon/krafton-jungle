#pragma once

#include "Viewport/EditorPreviewViewportClient.h"
#include "Viewport/ViewportClient.h"
#include "Editor/Viewport/ViewportCameraTransform.h"
#include "Editor/Slate/SWindow.h"
#include "Core/EngineTypes.h"

#include <d3d11.h>

class FWindowsWindow;
class UStaticMeshComponent;
class UWorld;
class AActor;

class FStaticMeshEditorViewportClient : public FViewportClient, public IEditorPreviewViewportClient
{
public:
	void Initialize(ID3D11Device* Device, uint32 Width, uint32 Height);
	void Release();

	void ResetCameraToPreviewBounds();

	void SetPreviewWorld(UWorld* InWorld) { PreviewWorld = InWorld; }
	void SetPreviewActor(AActor* InActor) { PreviewActor = InActor; }
	void SetPreviewMeshComponent(UStaticMeshComponent* InComp) { PreviewMeshComponent = InComp; }
	void SetPreviewBoundsOverride(const FBoundingBox& InBounds) { PreviewBoundsOverride = InBounds; bHasPreviewBoundsOverride = InBounds.IsValid(); }
	void ClearPreviewBoundsOverride() { bHasPreviewBoundsOverride = false; }
	void SetViewportRect(float X, float Y, float Width, float Height) { ViewportScreenRect = { X, Y, Width, Height }; }
	void SetClearColor(float R, float G, float B, float A = 1.0f)
	{
		ClearColor[0] = R;
		ClearColor[1] = G;
		ClearColor[2] = B;
		ClearColor[3] = A;
	}
	void QueueScrollInput(float ScrollNotches) { PendingScrollNotches += ScrollNotches; }

	bool IsRenderable() const override { return bIsRenderable; }
	bool IsMouseOverViewport() const override;

	FViewport* GetViewport() const override { return Viewport; }
	UWorld* GetPreviewWorld() const override { return PreviewWorld; }

	FViewportRenderOptions& GetRenderOptions() override { return RenderOptions; }
	const FViewportRenderOptions& GetRenderOptions() const override { return RenderOptions; }
	const float* GetClearColor() const override { return ClearColor; }

	void NotifyViewportResized(int32 NewWidth, int32 NewHeight) override;
	bool GetCameraView(FMinimalViewInfo& OutPOV) const override;

	void Tick(float DeltaTime);

private:
	void TickShortcuts();
	void TickInput(float DeltaTime);
	void SyncCameraSmoothingTarget();
	void ApplySmoothedCameraLocation(float DeltaTime);

private:
	FViewport* Viewport = nullptr;
	FWindowsWindow* Window = nullptr;
	FViewportRenderOptions RenderOptions;
	float ClearColor[4] = { 0.12f, 0.12f, 0.13f, 1.0f };

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;
	UStaticMeshComponent* PreviewMeshComponent = nullptr;
	FBoundingBox PreviewBoundsOverride;
	bool bHasPreviewBoundsOverride = false;

	bool bIsRenderable = false;

	FViewportCameraTransform ViewTransform;
	FRect ViewportScreenRect;

	FVector TargetLocation;
	bool bTargetLocationInitialized = false;
	FVector LastAppliedCameraLocation;
	bool bLastAppliedCameraLocationInitialized = false;
	const float SmoothLocationSpeed = 10.0f;
	float PendingScrollNotches = 0.0f;
};
