#include "Editor/UI/Panel/EditorCameraPreviewWidget.h"

#include "Component/ActorComponent.h"
#include "Component/Camera/CameraComponent.h"
#include "Editor/Selection/SelectionManager.h"
#include "Math/MathUtils.h"
#include "Viewport/Viewport.h"
#include "ImGui/imgui.h"

void FEditorCameraPreviewWidget::Initialize(UEditorEngine* InEditor, ID3D11Device* InDevice)
{
	Editor = InEditor;

	if (!InDevice)
	{
		return;
	}

	Viewport = new FViewport();
	Viewport->Initialize(InDevice, static_cast<uint32>(PreviewWidth), static_cast<uint32>(PreviewWidth / PreviewAspectRatio));
}

void FEditorCameraPreviewWidget::Release()
{
	SelectedCamera = nullptr;
	Editor = nullptr;

	if (Viewport)
	{
		Viewport->Release();
		delete Viewport;
		Viewport = nullptr;
	}
}

void FEditorCameraPreviewWidget::UpdateSelection(FSelectionManager* SelectionManager)
{
	UCameraComponent* Camera = nullptr;
	if (SelectionManager)
	{
		Camera = Cast<UCameraComponent>(SelectionManager->GetSelectedActorComponent());
	}

	SelectedCamera = Camera;
}

void FEditorCameraPreviewWidget::ClampPreviewWidth(float AppWidth)
{
	const float MaxWidth = (AppWidth > 0.0f) ? (AppWidth * 0.45f) : 640.0f;
	PreviewWidth = Clamp(PreviewWidth, 180.0f, (std::max)(180.0f, MaxWidth));
}

void FEditorCameraPreviewWidget::UpdateLayout(const FRect& ActiveViewportRect, float AppWidth, float AppHeight)
{
	bHasLayout = false;
	bOverlayHovered = false;

	if (ActiveViewportRect.Width <= 1.0f || ActiveViewportRect.Height <= 1.0f)
	{
		return;
	}

	if (AppWidth > 0.0f && AppHeight > 0.0f)
	{
		PreviewAspectRatio = AppWidth / AppHeight;
	}
	else
	{
		PreviewAspectRatio = 16.0f / 9.0f;
	}

	if (PreviewWidth <= 0.0f)
	{
		PreviewWidth = AppWidth * 0.25f;
	}

	ClampPreviewWidth(AppWidth);

	const float PreviewHeight = PreviewWidth / PreviewAspectRatio;
	const float Margin = 16.0f;
	OverlayRect.Width = PreviewWidth;
	OverlayRect.Height = PreviewHeight;
	OverlayRect.X = ActiveViewportRect.X + ActiveViewportRect.Width - OverlayRect.Width - Margin;
	OverlayRect.Y = ActiveViewportRect.Y + ActiveViewportRect.Height - OverlayRect.Height - Margin;

	if (OverlayRect.X < ActiveViewportRect.X + Margin)
	{
		OverlayRect.X = ActiveViewportRect.X + Margin;
	}
	if (OverlayRect.Y < ActiveViewportRect.Y + Margin)
	{
		OverlayRect.Y = ActiveViewportRect.Y + Margin;
	}

	if (Viewport)
	{
		const uint32 TargetWidth = static_cast<uint32>((std::max)(1.0f, OverlayRect.Width));
		const uint32 TargetHeight = static_cast<uint32>((std::max)(1.0f, OverlayRect.Height));
		if (Viewport->GetWidth() != TargetWidth || Viewport->GetHeight() != TargetHeight)
		{
			Viewport->RequestResize(TargetWidth, TargetHeight);
		}
	}

	bHasLayout = true;
}

bool FEditorCameraPreviewWidget::IsRenderable() const
{
	return Viewport && Viewport->GetSRV() && SelectedCamera.Get() && bHasLayout;
}

void FEditorCameraPreviewWidget::RenderOverlay()
{
	bOverlayHovered = false;
	if (!IsRenderable())
	{
		return;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Min(OverlayRect.X, OverlayRect.Y);
	const ImVec2 Max(OverlayRect.X + OverlayRect.Width, OverlayRect.Y + OverlayRect.Height);

	DrawList->AddRectFilled(Min, Max, IM_COL32(8, 10, 12, 255));
	DrawList->AddImage(reinterpret_cast<ImTextureID>(Viewport->GetSRV()), Min, Max);
	DrawList->AddRect(Min, Max, IM_COL32(255, 255, 255, 170), 0.0f, 0, 1.0f);

	const float GripSize = 14.0f;
	const ImVec2 GripMin(Max.x - GripSize, Max.y - GripSize);
	DrawList->AddRectFilled(GripMin, Max, IM_COL32(15, 15, 15, 120));
	DrawList->AddLine(ImVec2(Max.x - 4.0f, Max.y - 12.0f), ImVec2(Max.x - 12.0f, Max.y - 4.0f), IM_COL32(255, 255, 255, 180), 1.0f);
	DrawList->AddLine(ImVec2(Max.x - 4.0f, Max.y - 7.0f), ImVec2(Max.x - 7.0f, Max.y - 4.0f), IM_COL32(255, 255, 255, 180), 1.0f);

	bOverlayHovered = ImGui::IsMouseHoveringRect(Min, Max, true);

	const ImVec2 SavedCursor = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(GripMin);
	ImGui::InvisibleButton("##CameraPreviewResizeGrip", ImVec2(GripSize, GripSize));
	const bool bGripHovered = ImGui::IsItemHovered();
	const bool bGripActive = ImGui::IsItemActive();
	if (bGripHovered || bGripActive)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
		bOverlayHovered = true;
	}

	if (bGripActive)
	{
		const ImVec2 MousePos = ImGui::GetIO().MousePos;
		PreviewWidth = MousePos.x - OverlayRect.X;
		ClampPreviewWidth(ImGui::GetMainViewport()->Size.x);
	}
	ImGui::SetCursorScreenPos(SavedCursor);
}
