#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Slate/SWindow.h"

struct ID3D11Device;
class FSelectionManager;
class FViewport;
class UCameraComponent;
class UEditorEngine;

class FEditorCameraPreviewWidget
{
public:
	FEditorCameraPreviewWidget() = default;
	~FEditorCameraPreviewWidget() = default;

	void Initialize(UEditorEngine* InEditor, ID3D11Device* InDevice);
	void Release();

	void UpdateSelection(FSelectionManager* SelectionManager);
	void UpdateLayout(const FRect& ActiveViewportRect, float AppWidth, float AppHeight);
	void RenderOverlay();

	bool IsRenderable() const;
	bool IsOverlayHovered() const { return bOverlayHovered; }
	FViewport* GetViewport() const { return Viewport; }
	UCameraComponent* GetCameraComponent() const { return SelectedCamera.Get(); }
	float GetPreviewAspectRatio() const { return PreviewAspectRatio; }

private:
	void ClampPreviewWidth(float AppWidth);

	UEditorEngine* Editor = nullptr;
	FViewport* Viewport = nullptr;
	TWeakObjectPtr<UCameraComponent> SelectedCamera;

	FRect OverlayRect = {};
	float PreviewWidth = 320.0f;
	float PreviewAspectRatio = 16.0f / 9.0f;
	bool bHasLayout = false;
	bool bOverlayHovered = false;
};
