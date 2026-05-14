#pragma once

#include "ImGui/imgui.h"
#include "Editor/UI/EditorConsoleWidget.h"
#include "Editor/UI/EditorControlWidget.h"
#include "Editor/UI/EditorContentDrawerWidget.h"
#include "Editor/UI/EditorMaterialWidget.h"
#include "Editor/UI/EditorPropertyWidget.h"
#include "Editor/UI/EditorSceneWidget.h"
#include "Editor/UI/EditorViewportOverlayWidget.h"
#include "Editor/UI/EditorStatWidget.h"
#include "Editor/UI/EditorToolbarWidget.h"
#include "Editor/UI/EditorPlayStreamWidget.h"
#include "Editor/UI/EditorCameraShakeWidget.h"
#include "Editor/UI/EditorSkeletalMeshViewerWidget.h"
#include "Math/Vector.h"

#include <memory>
#include <vector>

class AActor;
class FRenderer;
class UEditorEngine;
class FWindowsWindow;

class FEditorMainPanel
{
public:
	void Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine);
	void Release();
	void Render(float DeltaTime);
	void Update();

	FEditorPropertyWidget& GetPropertyWidget() { return PropertyWidget; }
	FEditorMaterialWidget& GetMaterialWidget() { return MaterialWidget; }
	FEditorSceneWidget& GetSceneWidget() { return SceneWidget; }
	const FEditorSceneWidget& GetSceneWidget() const { return SceneWidget; }
	void OpenSkeletalMeshViewer(const FString& MeshPath);
	FSkeletalMeshPreviewScene* GetSkeletalMeshPreviewScene()
	{
		return FocusedSkeletalMeshViewer ? FocusedSkeletalMeshViewer->GetPreviewScene() : nullptr;
	}
	const FSkeletalMeshPreviewScene* GetSkeletalMeshPreviewScene() const
	{
		return FocusedSkeletalMeshViewer ? FocusedSkeletalMeshViewer->GetPreviewScene() : nullptr;
	}
	const std::vector<std::shared_ptr<FEditorSkeletalMeshViewerWidget>>& GetSkeletalMeshViewers() const
	{
		return SkeletalMeshViewers;
	}

	void ResetWidgetSelections()
	{
		PropertyWidget.ResetSelection();
		MaterialWidget.ResetSelection();
	}

private:
	void RenderViewportHostWindow();
	void RenderViewportMenuBarForIndex(int32 ViewportIndex);
	void RenderEditorDebugPanel();
	void ProcessPendingDebugActions();
	void EnsureDefaultDockLayout(ImGuiID DockspaceId);
	bool ShouldResetDefaultDockLayout(ImGuiID DockspaceId) const;
	void DockPendingSkeletalMeshViewers();
private:
	FWindowsWindow* Window = nullptr;
	UEditorEngine* EditorEngine = nullptr;

	ImVector<ImWchar> FontGlyphRanges; // 폰트 아틀라스 빌드 전까지 수명 유지 필요
	FEditorConsoleWidget ConsoleWidget;
	FEditorControlWidget ControlWidget;
	FEditorContentDrawerWidget ContentDrawerWidget;
	FEditorPropertyWidget PropertyWidget;
	FEditorSceneWidget SceneWidget;
	FEditorMaterialWidget MaterialWidget;
	FEditorViewportOverlayWidget ViewportOverlayWidget;
	FEditorStatWidget StatWidget;
	FEditorToolbarWidget ToolbarWidget;
	FEditorPlayStreamWidget PlayStreamWidget;
	FEditorCameraShakeWidget CameraShakeWidget;
	FEditorSkeletalMeshViewerWidget* FocusedSkeletalMeshViewer = nullptr;
	std::vector<std::shared_ptr<FEditorSkeletalMeshViewerWidget>> SkeletalMeshViewers;
	std::vector<FString> PendingSkeletalMeshViewerDockWindows;
	int32 NextViewerInstanceId = 1;

	bool bShowConsole = false;
	bool bShowCameraShake = true;
	bool bShowControl = false;
	bool bShowProperty = true;
	bool bShowSceneManager = true;
	bool bShowMaterialEditor = true;
	bool bShowStatProfiler = true;
	bool bShowPlayStream = true;
	bool bShowEditorDebug = true;
	bool bDefaultDockLayoutChecked = false;

	int32 DebugPlaceActorTypeIndex = 0;
	int32 DebugGridRows = 10;
	int32 DebugGridCols = 10;
	int32 DebugGridLayers = 1;
	float DebugGridSpacing = 2.0f;
	bool bDebugGridCenter = true;
	bool bDebugUseCameraOrigin = true;
	float DebugCameraForwardDistance = 30.0f;
	FVector DebugManualGridOrigin = FVector(0.0f, 0.0f, 0.0f);
	bool bDebugRandomYaw = false;
	float DebugRandomYawRange = 180.0f;
	bool bDebugApplyJitter = false;
	float DebugJitterXY = 0.0f;
	float DebugJitterZ = 0.0f;
	std::vector<AActor*> DebugLastSpawnedActors;
	bool bPendingClearLastBatch = false;
};
