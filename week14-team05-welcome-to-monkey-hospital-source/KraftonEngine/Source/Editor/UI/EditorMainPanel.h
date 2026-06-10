#pragma once

#include "Core/Types/CoreTypes.h"
#include "Editor/UI/Panel/EditorConsoleWidget.h"
#include "Editor/UI/Panel/EditorControlWidget.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/UI/Panel/EditorPropertyWidget.h"
#include "Editor/UI/Panel/EditorReflectionPropertyWidget.h"
#include "Editor/UI/Panel/EditorSceneWidget.h"
#include "Editor/UI/Panel/EditorSceneManagerWidget.h"
#include "Editor/UI/Panel/EditorStatWidget.h"
#include "Editor/UI/Debug/EditorShadowMapDebugWidget.h"
#include "Editor/UI/Debug/EditorAnimationDebugWidget.h"
#include "Editor/UI/Panel/EditorProjectSettingsWidget.h"
#include "Editor/UI/Panel/EditorWorldSettingsWidget.h"
#include "Editor/UI/ContentBrowser/ContentBrowser.h"
#include "Editor/UI/Asset/AssetEditorManager.h"
#include "Editor/UI/Asset/UI/UIEditorWidget.h"
#include "Editor/UI/EditorDocumentTabManager.h"
#include "Object/GarbageCollection.h"
#include "Math/Vector.h"

#include <filesystem>

class AActor;
class FRenderer;
class UEditorEngine;
class FWindowsWindow;
class IEditorPreviewViewportClient;

class FEditorMainPanel
{
public:
	void Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine);
	void Release();

	void TickAssetEditors(float DeltaTime);
	void Render(float DeltaTime);
	void Update();
	void SaveToSettings() const;
	void HideEditorWindows();
	void ShowEditorWindows();
	void SetShowEditorOnlyComponents(bool bEnable)
	{
		PropertyWidget.SetShowEditorOnlyComponents(bEnable);
		SceneManagerWidget.SetShowEditorOnlyComponents(bEnable);
	}
	bool IsShowingEditorOnlyComponents() const { return PropertyWidget.IsShowingEditorOnlyComponents(); }
	void HideEditorWindowsForPIE();
	void RestoreEditorWindowsAfterPIE();
	void RefreshContentBrowser() { ContentBrowserWidget.Refresh(); }
	void SetContentBrowserIconSize(float Size) { ContentBrowserWidget.SetIconSize(Size); }
	float GetContentBrowserIconSize() const { return ContentBrowserWidget.GetIconSize(); }
	const std::wstring& GetContentBrowserCurrentPath() const { return ContentBrowserWidget.GetCurrentDirectoryPath(); }

	void OpenAssetEditorForObject(UObject* Object);
	void OpenUIEditor(const std::filesystem::path& Path);
	void CollectAssetEditorPreviewViewportClients(TArray<IEditorPreviewViewportClient*>& OutClients) const;
	bool IsMouseOverAssetEditorPreviewViewport() const;
	bool IsLevelDocumentActive() const { return DocumentTabs.IsLevelEditorActive(); }
    void AddReferencedObjects(FReferenceCollector& Collector) { AssetEditorManager.AddReferencedObjects(Collector); }

private:
	void RenderMainMenuBar();
	void RenderMainDockSpace(float ReservedBottomHeight, float ReservedTopHeight);
	void RenderDocumentTabStrip(float ReservedBottomHeight);
	void RenderActiveDocument(float ReservedTopHeight, float ReservedBottomHeight, float DeltaTime);
	void RenderShortcutOverlay();
	void RenderEditorDebugPanel();
	void RenderContentBrowserDrawer(float DeltaTime);
	void RenderConsoleDrawer(float DeltaTime);
	void RenderFooterOverlay(float DeltaTime);
	void HandleGlobalShortcuts();
	void ToggleContentBrowserDrawer();
	void ToggleConsoleDrawer(bool bFocusInput);
	void ProcessPendingDebugActions();

	FWindowsWindow* Window = nullptr;
	UEditorEngine* EditorEngine = nullptr;
	FEditorConsoleWidget ConsoleWidget;
	FEditorControlWidget ControlWidget;
	FEditorPropertyWidget PropertyWidget;
	FEditorReflectionPropertyWidget ReflectionPropertyWidget;
	FEditorSceneWidget SceneWidget;
	FEditorSceneManagerWidget SceneManagerWidget;
	FEditorStatWidget StatWidget;
	FEditorContentBrowserWidget ContentBrowserWidget;
	EditorShadowMapDebugWidget ShadowMapDebugWidget;
	FEditorAnimationDebugWidget AnimationDebugWidget;
	EditorProjectSettingsWidget ProjectSettingsWidget;
	EditorWorldSettingsWidget WorldSettingsWidget;
	FAssetEditorManager AssetEditorManager;
	FUIEditorWidget UIEditorWidget;
	FEditorDocumentTabManager DocumentTabs;

	bool bShowWidgetList = false;
	bool bShowShortcutOverlay = false;
	bool bHideEditorWindows = false;
	bool bHasSavedUIVisibility = false;
	bool bSavedShowWidgetList = false;
	bool bConsoleDrawerVisible = false;
	bool bBringConsoleDrawerToFrontNextFrame = false;
	bool bFocusConsoleInputNextFrame = false;
	bool bFocusConsoleButtonNextFrame = false;
	bool bBringContentBrowserDrawerToFrontNextFrame = false;
	int32 ConsoleBacktickCycleState = 0;
	float ConsoleDrawerAnim = 0.0f;
	float ContentBrowserDrawerAnim = 0.0f;
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
	TArray<AActor*> DebugLastSpawnedActors;
	bool bPendingClearLastBatch = false;
	uint64 LastLuaDebugEditorFocusSerial = 0;
	FEditorSettings::FUIVisibility SavedUIVisibility{};
};
