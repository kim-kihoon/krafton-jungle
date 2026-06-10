#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewport/Level/LevelEditorViewportClient.h"
#include "Render/Types/MinimalViewInfo.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Object.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Platform/WindowsWindow.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include "Render/Pipeline/Renderer.h"
#include "Engine/Input/InputSystem.h"
#include "Lua/LuaDebugManager.h"
#include "LuaBlueprint/LuaBlueprintAsset.h"
#include "LuaBlueprint/LuaBlueprintManager.h"

#include "Editor/Slate/SlateApplication.h"
#include "Editor/UI/Util/ImGuiSetting.h"
#include "Editor/UI/Util/NotificationToast.h"
#include "UI/CrosshairOverlay.h"
#include "Viewport/GameViewportClient.h"
#include "Viewport/Viewport.h"

#include "Editor/UI/Asset/Curve/FloatCurveEditorWidget.h"
#include "Editor/UI/Asset/CameraShake/CameraShakeEditorWidget.h"
#include "Editor/UI/Asset/Mesh/MeshEditorWidget.h"
#include "Editor/UI/Asset/Mesh/StaticMeshEditorWidget.h"
#include "Editor/UI/Asset/Animation/AnimGraphEditorWidget.h"
#include "Editor/UI/Asset/LuaBlueprint/LuaBlueprintEditorWidget.h"
#include "Editor/UI/Asset/Material/MaterialEditorWidget.h"
#include "Editor/UI/Asset/Physics/PhysicsAssetEditorWidget.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <random>
#include <utility>

namespace
{
	const FRect* FindPlayViewportScreenRect(UEditorEngine* EditorEngine)
	{
		if (!EditorEngine)
		{
			return nullptr;
		}

		FViewport* PlayViewport = nullptr;
		if (UGameViewportClient* GameViewportClient = EditorEngine->GetGameViewportClient())
		{
			PlayViewport = GameViewportClient->GetViewport();
		}

		const TArray<FLevelEditorViewportClient*>& ViewportClients = EditorEngine->GetLevelViewportClients();
		for (FLevelEditorViewportClient* ViewportClient : ViewportClients)
		{
			if (!ViewportClient || !EditorEngine->ShouldRenderViewportClient(ViewportClient))
			{
				continue;
			}

			if (PlayViewport != nullptr && ViewportClient->GetViewport() == PlayViewport)
			{
				return &ViewportClient->GetViewportScreenRect();
			}
		}

		if (FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport())
		{
			return &ActiveViewport->GetViewportScreenRect();
		}

		return nullptr;
	}

	void RenderPlayCrosshairOverlay(UEditorEngine* EditorEngine)
	{
		if (!EditorEngine || !EditorEngine->IsPlayingInEditor() || !FCrosshairOverlay::IsVisible())
		{
			return;
		}

		const FRect* ViewportRect = FindPlayViewportScreenRect(EditorEngine);
		if (ViewportRect == nullptr || ViewportRect->Width <= 0.0f || ViewportRect->Height <= 0.0f)
		{
			return;
		}

		const ImVec2 Center(
			ViewportRect->X + ViewportRect->Width * 0.5f,
			ViewportRect->Y + ViewportRect->Height * 0.5f);
		FCrosshairOverlay::Draw(ImGui::GetForegroundDrawList(), Center);
	}
}

#include "Asset/Particle/ParticleEditorWidget.h"

namespace
{
struct FDebugPlaceActorOption
{
	const char* Label = "";
	FLevelViewportLayout::EViewportPlaceActorType Type = FLevelViewportLayout::EViewportPlaceActorType::Cube;
};

const FDebugPlaceActorOption GDebugPlaceActorOptions[] = {
	{ "Cube", FLevelViewportLayout::EViewportPlaceActorType::Cube },
	{ "Sphere", FLevelViewportLayout::EViewportPlaceActorType::Sphere },
	{ "Cylinder", FLevelViewportLayout::EViewportPlaceActorType::Cylinder },
	{ "Decal", FLevelViewportLayout::EViewportPlaceActorType::Decal },
	{ "Height Fog", FLevelViewportLayout::EViewportPlaceActorType::HeightFog },
	{ "Ambient Light", FLevelViewportLayout::EViewportPlaceActorType::AmbientLight },
	{ "Directional Light", FLevelViewportLayout::EViewportPlaceActorType::DirectionalLight },
	{ "Point Light", FLevelViewportLayout::EViewportPlaceActorType::PointLight },
	{ "Spot Light", FLevelViewportLayout::EViewportPlaceActorType::SpotLight },
	{ "Character",     FLevelViewportLayout::EViewportPlaceActorType::Character },
	{ "Lua Character", FLevelViewportLayout::EViewportPlaceActorType::LuaCharacter },
	{ "Wheeled Vehicle", FLevelViewportLayout::EViewportPlaceActorType::WheeledVehicle },
};

constexpr float GDocumentTabStripHeight = 34.0f;
constexpr float GDocumentTabWidth = 180.0f;
constexpr float GDocumentTabRightPadding = 8.0f;

struct FDocumentTabVisual
{
	ImVec4 AccentColor;
	const char* Tooltip = "";
};

FDocumentTabVisual GetDocumentTabVisual(EEditorDocumentTabKind Kind)
{
	switch (Kind)
	{
	case EEditorDocumentTabKind::LevelEditor:
		return { ImVec4(0.96f, 0.67f, 0.16f, 1.0f), "Level Scene" };
	case EEditorDocumentTabKind::StaticMeshEditor:
		return { ImVec4(0.22f, 0.78f, 0.45f, 1.0f), "Static Mesh Editor" };
	case EEditorDocumentTabKind::SkeletalMeshEditor:
		return { ImVec4(0.18f, 0.70f, 0.95f, 1.0f), "Skeletal Mesh Editor" };
	case EEditorDocumentTabKind::ParticleEditor:
		return { ImVec4(0.72f, 0.42f, 0.95f, 1.0f), "Particle Editor" };
	case EEditorDocumentTabKind::AnimGraphEditor:
		return { ImVec4(0.86f, 0.58f, 0.22f, 1.0f), "Anim Graph Editor" };
	case EEditorDocumentTabKind::LuaBlueprintEditor:
		return { ImVec4(0.36f, 0.52f, 0.94f, 1.0f), "Lua Blueprint Editor" };
	case EEditorDocumentTabKind::PhysicsAssetEditor:
		return { ImVec4(0.84f, 0.38f, 0.30f, 1.0f), "Physics Asset Editor" };
    case EEditorDocumentTabKind::MaterialEditor:
        return { ImVec4(0.92f, 0.44f, 0.24f, 1.0f), "Material Graph Editor" };
	case EEditorDocumentTabKind::Unsupported:
	default:
		return { ImVec4(0.58f, 0.62f, 0.70f, 1.0f), "Editor Tab" };
	}
}

const char* GetDocumentTabKindName(EEditorDocumentTabKind Kind)
{
	switch (Kind)
	{
	case EEditorDocumentTabKind::LevelEditor: return "Level";
	case EEditorDocumentTabKind::StaticMeshEditor: return "StaticMesh";
	case EEditorDocumentTabKind::SkeletalMeshEditor: return "SkeletalMesh";
	case EEditorDocumentTabKind::ParticleEditor: return "Particle";
	case EEditorDocumentTabKind::AnimGraphEditor: return "AnimGraph";
	case EEditorDocumentTabKind::LuaBlueprintEditor: return "LuaBlueprint";
	case EEditorDocumentTabKind::PhysicsAssetEditor: return "PhysicsAsset";
    case EEditorDocumentTabKind::MaterialEditor:
        return "Material";
	case EEditorDocumentTabKind::Unsupported:
	default:
		return "Unsupported";
	}
}

FString MakeDocumentTabImGuiId(const FEditorDocumentTabEntry& Tab)
{
	return Tab.Label + "###DocumentTab_" + GetDocumentTabKindName(Tab.Id.Kind) + "_" + Tab.Id.PayloadId;
}

FString GetFileStemForDisplay(const FString& Path)
{
	if (Path.empty())
	{
		return FString();
	}

	std::filesystem::path FilePath(FPaths::ToWide(Path));
	FString Stem = FPaths::ToUtf8(FilePath.stem().wstring());
	if (!Stem.empty())
	{
		return Stem;
	}

	return FPaths::ToUtf8(FilePath.filename().wstring());
}

}

void FEditorMainPanel::Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiSetting::LoadSetting();

	ImGuiIO& IO = ImGui::GetIO();
	IO.IniFilename = "Settings/imgui.ini";
	IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	Window = InWindow;
	EditorEngine = InEditorEngine;

	// 한글 지원 폰트 로드 (시스템 맑은 고딕)
	IO.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf", 16.0f, nullptr, IO.Fonts->GetGlyphRangesKorean());

	ImGui_ImplWin32_Init((void*)InWindow->GetHWND());
	ImGui_ImplDX11_Init(InRenderer.GetFD3DDevice().GetDevice(), InRenderer.GetFD3DDevice().GetDeviceContext());

	ImGuiStyle& Style = ImGui::GetStyle();
	ImVec4* Colors = Style.Colors;
	
	const ImVec4 UnifiedBlack = ImVec4(0.024f, 0.024f, 0.028f, 1.0f);
	const ImVec4 UnifiedBlackHovered = ImVec4(0.075f, 0.075f, 0.085f, 1.0f);
	const ImVec4 UnifiedBlackActive = ImVec4(0.105f, 0.105f, 0.118f, 1.0f);

	Colors[ImGuiCol_WindowBg] = UnifiedBlack;
	Colors[ImGuiCol_ChildBg] = UnifiedBlack;
	Colors[ImGuiCol_PopupBg] = UnifiedBlack;
	Colors[ImGuiCol_TitleBg] = UnifiedBlack;
	Colors[ImGuiCol_TitleBgActive] = UnifiedBlack;
	Colors[ImGuiCol_TitleBgCollapsed] = UnifiedBlack;
	Colors[ImGuiCol_MenuBarBg] = UnifiedBlack;
	Colors[ImGuiCol_Tab] = UnifiedBlack;
	Colors[ImGuiCol_TabSelected] = UnifiedBlackActive;
	Colors[ImGuiCol_TabHovered] = UnifiedBlackHovered;
	Colors[ImGuiCol_TabDimmed] = UnifiedBlack;
	Colors[ImGuiCol_TabDimmedSelected] = UnifiedBlackActive;
	Colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.28f, 0.28f, 0.30f, 1.0f);
	Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
	Colors[ImGuiCol_DockingEmptyBg] = UnifiedBlack;
	Colors[ImGuiCol_Header] = UnifiedBlack;
	Colors[ImGuiCol_HeaderHovered] = UnifiedBlackHovered;
	Colors[ImGuiCol_HeaderActive] = UnifiedBlackActive;
	Colors[ImGuiCol_Button] = UnifiedBlackActive;
	Colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.16f, 0.18f, 1.0f);
	Colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
	Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
	Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
	Colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.24f, 1.0f);
	Colors[ImGuiCol_CheckMark] = ImVec4(0.82f, 0.82f, 0.82f, 1.0f);
	Colors[ImGuiCol_Border] = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);

	ConsoleWidget.Initialize(InEditorEngine);
	ControlWidget.Initialize(InEditorEngine);
	PropertyWidget.Initialize(InEditorEngine);
	ReflectionPropertyWidget.Initialize(InEditorEngine);
	SceneWidget.Initialize(InEditorEngine);
	SceneManagerWidget.Initialize(InEditorEngine);
	StatWidget.Initialize(InEditorEngine);
	ContentBrowserWidget.Initialize(InEditorEngine, InRenderer.GetFD3DDevice().GetDevice());
	ShadowMapDebugWidget.Initialize(InEditorEngine);
	AnimationDebugWidget.Initialize(InEditorEngine);
	AssetEditorManager.Initialize(InEditorEngine);

	AssetEditorManager.RegisterEditor<FFloatCurveEditorWidget>();
	AssetEditorManager.RegisterEditor<FCameraShakeEditorWidget>();
	AssetEditorManager.RegisterEditor<FMeshEditorWidget>();
	AssetEditorManager.RegisterEditor<FStaticMeshEditorWidget>();
	AssetEditorManager.RegisterEditor<FAnimGraphEditorWidget>();
	AssetEditorManager.RegisterEditor<FParticleEditorWidget>();
	AssetEditorManager.RegisterEditor<FLuaBlueprintEditorWidget>();
    AssetEditorManager.RegisterEditor<FMaterialEditorWidget>();
}

void FEditorMainPanel::Release()
{
	UIEditorWidget.Close();
	AssetEditorManager.CloseAll();
	ConsoleWidget.Shutdown();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void FEditorMainPanel::SaveToSettings() const
{
	ContentBrowserWidget.SaveToSettings();
}

void FEditorMainPanel::TickAssetEditors(float DeltaTime)
{
	AssetEditorManager.Tick(DeltaTime);
}

void FEditorMainPanel::Render(float DeltaTime)
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	RenderMainMenuBar();

	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	RenderDocumentTabStrip(FooterHeight);
	RenderMainDockSpace(FooterHeight, GDocumentTabStripHeight);

	// 뷰포트 렌더링은 EditorEngine이 담당 (SSplitter 레이아웃 + ImGui::Image)
	if (EditorEngine && DocumentTabs.IsLevelEditorActive())
	{
		SCOPE_STAT_CAT("EditorEngine->RenderViewportUI", "5_UI");
		EditorEngine->RenderViewportUI(DeltaTime);

		if (FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport())
		{
			EditorEngine->GetOverlayStatSystem().RenderImGui(*EditorEngine, ActiveViewport->GetViewportScreenRect());
		}
	}

	const FEditorSettings& Settings = FEditorSettings::Get();
	const bool bLevelDocumentActive = DocumentTabs.IsLevelEditorActive();

	if (!bHideEditorWindows && Settings.UI.bImGUISettings)
	{
		ImGuiSetting::ShowSetting();
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bControl)
	{
		SCOPE_STAT_CAT("ControlWidget.Render", "5_UI");
		ControlWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bProperty)
	{
		SCOPE_STAT_CAT("PropertyWidget.Render", "5_UI");
		PropertyWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bReflectionProperty)
	{
		SCOPE_STAT_CAT("ReflectionPropertyWidget.Render", "5_UI");
		ReflectionPropertyWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bScene)
	{
		SCOPE_STAT_CAT("SceneWidget.Render", "5_UI");
		SceneWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bSceneManager)
	{
		SCOPE_STAT_CAT("SceneManagerWidget.Render", "5_UI");
		SceneManagerWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bStat)
	{
		SCOPE_STAT_CAT("StatWidget.Render", "5_UI");
		StatWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bShadowMapDebug)
	{
		ShadowMapDebugWidget.Render(DeltaTime);
	}

	if (!bHideEditorWindows && bLevelDocumentActive && Settings.UI.bAnimationDebug)
	{
		SCOPE_STAT_CAT("AnimationDebugWidget.Render", "5_UI");
		AnimationDebugWidget.Render(DeltaTime);
	}

	ProjectSettingsWidget.Render();
	WorldSettingsWidget.Render();

	if (!bHideEditorWindows && bLevelDocumentActive)
	{
		RenderEditorDebugPanel();
	}

	if (!bHideEditorWindows && !bLevelDocumentActive)
	{
		RenderActiveDocument(GDocumentTabStripHeight, FooterHeight, DeltaTime);
	}

	RenderShortcutOverlay();
	RenderContentBrowserDrawer(DeltaTime);
	RenderConsoleDrawer(DeltaTime);
	RenderFooterOverlay(DeltaTime);

	AssetEditorManager.Render(DeltaTime);
	UIEditorWidget.Render(DeltaTime);

	// 토스트 알림 (항상 최상위에 표시)
	FNotificationToast::Render();

	// PIE HUD 위에 조준점을 마지막에 그린다.
	RenderPlayCrosshairOverlay(EditorEngine);

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void FEditorMainPanel::RenderMainMenuBar()
{
	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Scene", "Ctrl+N") && EditorEngine)
		{
			EditorEngine->NewScene();
		}
		if (ImGui::MenuItem("Open Scene...", "Ctrl+O") && EditorEngine)
		{
			EditorEngine->LoadSceneWithDialog();
		}
		if (ImGui::MenuItem("Save Scene", "Ctrl+S") && EditorEngine)
		{
			EditorEngine->SaveScene();
		}
		if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S") && EditorEngine)
		{
			EditorEngine->SaveSceneAsWithDialog();
		}

		ImGui::Separator();
		const char* CurrentSceneLabel = "Current: Unsaved Scene";
		FString CurrentScenePath;
		FString CurrentSceneText;
		if (EditorEngine && EditorEngine->HasCurrentLevelFilePath())
		{
			CurrentScenePath = EditorEngine->GetCurrentLevelFilePath();
			CurrentSceneText = FString("Current: ") + CurrentScenePath;
			CurrentSceneLabel = CurrentSceneText.c_str();
		}
		ImGui::BeginDisabled();
		ImGui::MenuItem(CurrentSceneLabel, nullptr, false, false);
		ImGui::EndDisabled();
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Windows"))
	{
		bShowWidgetList = true;
		ImGui::OpenPopup("##WidgetListPopup");
	}
	if (ImGui::BeginPopup("##WidgetListPopup"))
	{
		ImGui::Checkbox("Camera", &Settings.UI.bControl);
		ImGui::Checkbox("Detial", &Settings.UI.bProperty);
		ImGui::Checkbox("Reflection Property", &Settings.UI.bReflectionProperty);
		ImGui::Checkbox("Outliner", &Settings.UI.bScene);
		ImGui::Checkbox("Scene Manager", &Settings.UI.bSceneManager);
		ImGui::Checkbox("Stat Profiler", &Settings.UI.bStat);
		if (ImGui::Checkbox("Content Browser (Ctrl+Space)", &Settings.UI.bContentBrowser) && Settings.UI.bContentBrowser)
		{
			bConsoleDrawerVisible = false;
			ConsoleDrawerAnim = 0.0f;
			ConsoleBacktickCycleState = 0;
			bFocusConsoleInputNextFrame = false;
			bBringConsoleDrawerToFrontNextFrame = false;
			bBringContentBrowserDrawerToFrontNextFrame = true;
		}
		ImGui::Checkbox("Editor Debug", &Settings.UI.bEditorDebug);
		ImGui::Checkbox("Shadow Map Debug", &Settings.UI.bShadowMapDebug);
		ImGui::Checkbox("Animation Debug", &Settings.UI.bAnimationDebug);
		ImGui::Separator();
		ImGui::Checkbox("IMGUI_Setting", &Settings.UI.bImGUISettings);
		ImGui::EndPopup();
	}
	else
	{
		bShowWidgetList = false;
	}

	if (ImGui::MenuItem("Project Settings"))
	{
		ProjectSettingsWidget.bOpen = true;
	}

	if (ImGui::MenuItem("World Settings"))
	{
		WorldSettingsWidget.bOpen = true;
	}

	if (ImGui::MenuItem("Shortcut"))
	{
		bShowShortcutOverlay = !bShowShortcutOverlay;
	}

	ImGui::EndMainMenuBar();
}

void FEditorMainPanel::RenderMainDockSpace(float ReservedBottomHeight, float ReservedTopHeight)
{
	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImVec2 DockSpaceSize = MainViewport->WorkSize;
	DockSpaceSize.y = max(1.0f, DockSpaceSize.y - ReservedTopHeight - ReservedBottomHeight);
	ImVec2 DockSpacePos = MainViewport->WorkPos;
	DockSpacePos.y += ReservedTopHeight;

	ImGui::SetNextWindowPos(DockSpacePos);
	ImGui::SetNextWindowSize(DockSpaceSize);
	ImGui::SetNextWindowViewport(MainViewport->ID);

	ImGuiWindowFlags HostWindowFlags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus;

	char Label[32];
	std::snprintf(Label, sizeof(Label), "WindowOverViewport_%08X", MainViewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin(Label, nullptr, HostWindowFlags);
	ImGui::PopStyleVar(3);

	ImGuiID DockSpaceId = ImGui::GetID("DockSpace");
	ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f));

	ImGui::End();
}

void FEditorMainPanel::RenderDocumentTabStrip(float ReservedBottomHeight)
{
	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImVec2 TabPos = MainViewport->WorkPos;
	ImVec2 TabSize = ImVec2(MainViewport->WorkSize.x, GDocumentTabStripHeight);
	TabSize.y = max(1.0f, (std::min)(TabSize.y, MainViewport->WorkSize.y - ReservedBottomHeight));

	ImGui::SetNextWindowPos(TabPos);
	ImGui::SetNextWindowSize(TabSize);
	ImGui::SetNextWindowViewport(MainViewport->ID);

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoScrollWithMouse
		| ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.065f, 0.070f, 0.085f, 1.0f));
	ImGui::Begin("DocumentTabsHost", nullptr, WindowFlags);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);

	TArray<FEditorDocumentTabId> PendingCloseTabs;
	bool bHasPendingActiveTab = false;
	FEditorDocumentTabId PendingActiveTab;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const TArray<FEditorDocumentTabEntry>& Tabs = DocumentTabs.GetTabs();

	for (int32 Index = 0; Index < static_cast<int32>(Tabs.size()); ++Index)
	{
		const FEditorDocumentTabEntry& Tab = Tabs[Index];
		FString Label = Tab.Label;
		if (Tab.Id.Kind == EEditorDocumentTabKind::LevelEditor)
		{
			Label = (EditorEngine && EditorEngine->HasCurrentLevelFilePath())
				? FString("Level - ") + GetFileStemForDisplay(EditorEngine->GetCurrentLevelFilePath())
				: FString("Level - Untitled");
		}
		if (Tab.bDirty)
		{
			Label += "*";
		}

		ImGui::PushID(Index);
		const ImVec2 TabMin = ImGui::GetCursorScreenPos();
		const ImVec2 TabMax(TabMin.x + GDocumentTabWidth, TabMin.y + GDocumentTabStripHeight);
		ImGui::InvisibleButton("##DocumentTab", ImVec2(GDocumentTabWidth, GDocumentTabStripHeight));
		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = DocumentTabs.GetActiveTab() == Tab.Id;
		const FDocumentTabVisual Visual = GetDocumentTabVisual(Tab.Id.Kind);

		const float CloseSize = 16.0f;
		const ImVec2 CloseMin(TabMax.x - CloseSize - 7.0f, TabMin.y + (GDocumentTabStripHeight - CloseSize) * 0.5f);
		const ImVec2 CloseMax(CloseMin.x + CloseSize, CloseMin.y + CloseSize);
		const bool bCloseHovered = Tab.bCanClose && ImGui::IsMouseHoveringRect(CloseMin, CloseMax);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !bCloseHovered)
		{
			bHasPendingActiveTab = true;
			PendingActiveTab = Tab.Id;
		}
		if (bCloseHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			PendingCloseTabs.push_back(Tab.Id);
		}

		const ImU32 TabBg = ImGui::GetColorU32(
			bActive
				? ImVec4(0.115f, 0.125f, 0.150f, 1.0f)
				: (bHovered ? ImVec4(0.095f, 0.102f, 0.125f, 1.0f) : ImVec4(0.075f, 0.080f, 0.098f, 1.0f)));
		const ImU32 BorderColor = ImGui::GetColorU32(ImVec4(0.035f, 0.038f, 0.046f, 1.0f));
		DrawList->AddRectFilled(TabMin, TabMax, TabBg, 0.0f);
		DrawList->AddRect(TabMin, TabMax, BorderColor);

		const ImVec2 IconCenter(TabMin.x + 17.0f, TabMin.y + GDocumentTabStripHeight * 0.5f);
		DrawList->AddCircleFilled(IconCenter, 5.0f, ImGui::GetColorU32(Visual.AccentColor), 12);
		if (Tab.Id.Kind == EEditorDocumentTabKind::LevelEditor)
		{
			DrawList->AddTriangleFilled(
				ImVec2(IconCenter.x, IconCenter.y - 6.0f),
				ImVec2(IconCenter.x - 6.0f, IconCenter.y + 5.0f),
				ImVec2(IconCenter.x + 6.0f, IconCenter.y + 5.0f),
				ImGui::GetColorU32(Visual.AccentColor));
		}

		const ImU32 TextColor = ImGui::GetColorU32(bActive ? ImVec4(0.88f, 0.91f, 0.96f, 1.0f) : ImVec4(0.58f, 0.62f, 0.70f, 1.0f));
		const ImVec2 TextMin(TabMin.x + 32.0f, TabMin.y + (GDocumentTabStripHeight - ImGui::GetTextLineHeight()) * 0.5f);
		const ImVec2 TextMax(Tab.bCanClose ? CloseMin.x - 5.0f : TabMax.x - 10.0f, TabMax.y - 5.0f);
		if (TextMax.x > TextMin.x + 6.0f)
		{
			DrawList->PushClipRect(TextMin, TextMax, true);
			DrawList->AddText(TextMin, TextColor, Label.c_str());
			DrawList->PopClipRect();
		}

		if (Tab.bCanClose)
		{
			const ImU32 CloseColor = ImGui::GetColorU32(
				bCloseHovered ? ImVec4(0.94f, 0.96f, 1.0f, 1.0f) : ImVec4(0.50f, 0.54f, 0.62f, 1.0f));
			if (bCloseHovered)
			{
				DrawList->AddRectFilled(CloseMin, CloseMax, ImGui::GetColorU32(ImVec4(0.23f, 0.25f, 0.31f, 1.0f)), 3.0f);
			}
			DrawList->AddLine(ImVec2(CloseMin.x + 4.0f, CloseMin.y + 4.0f), ImVec2(CloseMax.x - 4.0f, CloseMax.y - 4.0f), CloseColor, 1.5f);
			DrawList->AddLine(ImVec2(CloseMax.x - 4.0f, CloseMin.y + 4.0f), ImVec2(CloseMin.x + 4.0f, CloseMax.y - 4.0f), CloseColor, 1.5f);
		}

		if (bActive)
		{
			DrawList->AddLine(
				ImVec2(TabMin.x, TabMax.y - 1.0f),
				ImVec2(TabMax.x, TabMax.y - 1.0f),
				ImGui::GetColorU32(Visual.AccentColor),
				2.0f);
		}

		if (bHovered)
		{
			ImGui::SetTooltip("%s\n%s", Label.c_str(), Visual.Tooltip);
		}

		ImGui::PopID();
		ImGui::SameLine(0.0f, 0.0f);
	}

	if (bHasPendingActiveTab)
	{
		DocumentTabs.SetActiveTab(PendingActiveTab);
	}

	for (const FEditorDocumentTabId& TabId : PendingCloseTabs)
	{
		AssetEditorManager.CloseEditorForTab(TabId);
		DocumentTabs.CloseTab(TabId);
	}

	const float UsedWidth = GDocumentTabWidth * static_cast<float>(Tabs.size());
	if (UsedWidth < TabSize.x - GDocumentTabRightPadding)
	{
		ImGui::SetCursorScreenPos(ImVec2(TabPos.x + UsedWidth, TabPos.y));
		ImGui::Dummy(ImVec2(TabSize.x - UsedWidth - GDocumentTabRightPadding, GDocumentTabStripHeight));
	}

	ImGui::End();
}

void FEditorMainPanel::RenderActiveDocument(float ReservedTopHeight, float ReservedBottomHeight, float DeltaTime)
{
	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImVec2 HostPos = MainViewport->WorkPos;
	HostPos.y += ReservedTopHeight;
	ImVec2 HostSize = MainViewport->WorkSize;
	HostSize.y = max(1.0f, HostSize.y - ReservedTopHeight - ReservedBottomHeight);

	ImGui::SetNextWindowPos(HostPos);
	ImGui::SetNextWindowSize(HostSize);
	ImGui::SetNextWindowViewport(MainViewport->ID);

	ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
	ImGui::Begin("ActiveDocumentHost", nullptr, HostFlags);
	ImGui::PopStyleVar(3);

	if (!AssetEditorManager.RenderActiveEditorDocument(DocumentTabs.GetActiveTab(), DeltaTime))
	{
		DocumentTabs.CloseTab(DocumentTabs.GetActiveTab());
	}

	ImGui::End();
}

void FEditorMainPanel::RenderShortcutOverlay()
{
	if (!bShowShortcutOverlay)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(320.0f, 150.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Shortcut Help", &bShowShortcutOverlay, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::End();
		return;
	}

	ImGui::TextUnformatted("File");
	ImGui::Separator();
	ImGui::TextUnformatted("Ctrl+N : New Scene");
	ImGui::TextUnformatted("Ctrl+O : Open Scene");
	ImGui::TextUnformatted("Ctrl+S : Save Scene");
	ImGui::TextUnformatted("Ctrl+Shift+S : Save Scene As");
	ImGui::Separator();
	ImGui::TextUnformatted("Editor");
	ImGui::Separator();
	ImGui::TextUnformatted("` : Focus console input / open console drawer");
	ImGui::TextUnformatted("Crtl + Spacebar : open content bowser");
	ImGui::TextUnformatted("F : Focus on selection");
	ImGui::TextUnformatted("Ctrl + LMB : Multi Picking (Toggle)");
	ImGui::TextUnformatted("Ctrl + Alt + LMB Drag : Area Selection");
	ImGui::TextUnformatted("F2: Actor or Component Rename");
	ImGui::Separator();
	ImGui::TextUnformatted("Physics Editor");
	ImGui::Separator();
	ImGui::TextUnformatted("F : Focus viewport on selected body, shape, or constraint");
	ImGui::TextUnformatted("Delete : Remove selected body or constraint");
	ImGui::Separator();
	ImGui::TextUnformatted("EsterEgg");
	ImGui::Separator();
	ImGui::TextUnformatted("Why does no one care about my shortcuts?");

	ImGui::End();
}

void FEditorMainPanel::RenderEditorDebugPanel()
{
	FEditorSettings& Settings = FEditorSettings::Get();
	if (!Settings.UI.bEditorDebug || !EditorEngine)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 320.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Editor Debug", &Settings.UI.bEditorDebug))
	{
		ImGui::End();
		return;
	}

	if (ImGui::CollapsingHeader("Place Actors (Grid)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const int32 OptionCount = static_cast<int32>(sizeof(GDebugPlaceActorOptions) / sizeof(GDebugPlaceActorOptions[0]));
		if (DebugPlaceActorTypeIndex < 0)
		{
			DebugPlaceActorTypeIndex = 0;
		}
		if (DebugPlaceActorTypeIndex >= OptionCount)
		{
			DebugPlaceActorTypeIndex = OptionCount - 1;
		}

		const char* CurrentActorLabel = GDebugPlaceActorOptions[DebugPlaceActorTypeIndex].Label;
		if (ImGui::BeginCombo("Actor Type", CurrentActorLabel))
		{
			for (int32 Index = 0; Index < OptionCount; ++Index)
			{
				const bool bSelected = (DebugPlaceActorTypeIndex == Index);
				if (ImGui::Selectable(GDebugPlaceActorOptions[Index].Label, bSelected))
				{
					DebugPlaceActorTypeIndex = Index;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::DragInt("Rows", &DebugGridRows, 1.0f, 1, 1024, "%d");
		ImGui::DragInt("Cols", &DebugGridCols, 1.0f, 1, 1024, "%d");
		ImGui::DragInt("Layers", &DebugGridLayers, 1.0f, 1, 256, "%d");
		ImGui::DragFloat("Grid Spacing", &DebugGridSpacing, 0.1f, 0.1f, 1000.0f, "%.2f");
		ImGui::Checkbox("Center Grid Around Origin", &bDebugGridCenter);

		ImGui::Separator();
		ImGui::Checkbox("Use Camera Forward Origin", &bDebugUseCameraOrigin);
		if (bDebugUseCameraOrigin)
		{
			ImGui::DragFloat("Camera Forward Distance", &DebugCameraForwardDistance, 0.5f, 0.0f, 100000.0f, "%.1f");
		}
		else
		{
			ImGui::DragFloat3("Manual Origin", &DebugManualGridOrigin.X, 0.1f, -100000.0f, 100000.0f, "%.2f");
		}

		ImGui::Separator();
		ImGui::Checkbox("Random Yaw", &bDebugRandomYaw);
		ImGui::BeginDisabled(!bDebugRandomYaw);
		ImGui::DragFloat("Yaw Range (+/-)", &DebugRandomYawRange, 1.0f, 0.0f, 180.0f, "%.1f");
		ImGui::EndDisabled();

		ImGui::Checkbox("Apply Position Jitter", &bDebugApplyJitter);
		ImGui::BeginDisabled(!bDebugApplyJitter);
		ImGui::DragFloat("Jitter XY", &DebugJitterXY, 0.05f, 0.0f, 1000.0f, "%.2f");
		ImGui::DragFloat("Jitter Z", &DebugJitterZ, 0.05f, 0.0f, 1000.0f, "%.2f");
		ImGui::EndDisabled();

		if (DebugGridRows < 1) DebugGridRows = 1;
		if (DebugGridCols < 1) DebugGridCols = 1;
		if (DebugGridLayers < 1) DebugGridLayers = 1;
		if (DebugGridSpacing < 0.1f) DebugGridSpacing = 0.1f;
		if (DebugRandomYawRange < 0.0f) DebugRandomYawRange = 0.0f;
		if (DebugRandomYawRange > 180.0f) DebugRandomYawRange = 180.0f;
		if (DebugJitterXY < 0.0f) DebugJitterXY = 0.0f;
		if (DebugJitterZ < 0.0f) DebugJitterZ = 0.0f;

		const long long TotalSpawnCount =
			static_cast<long long>(DebugGridRows) *
			static_cast<long long>(DebugGridCols) *
			static_cast<long long>(DebugGridLayers);
		ImGui::Text("Total Actors: %lld", TotalSpawnCount);
		ImGui::Text("Last Batch: %u", static_cast<uint32>(DebugLastSpawnedActors.size()));

		if (ImGui::Button("Spawn Grid Actors"))
		{
			UWorld* World = EditorEngine->GetWorld();
			if (!World)
			{
				FEditorConsoleWidget::AddLog("Grid spawn failed: invalid world\n");
			}
			else
			{
				FVector GridOrigin = DebugManualGridOrigin;
				FVector GridRight(1.0f, 0.0f, 0.0f);
				FVector GridForward(0.0f, 1.0f, 0.0f);
				if (bDebugUseCameraOrigin)
				{
					// D.3: 컴포넌트가 아닌 POV 통화로 read.
					FMinimalViewInfo POV;
					if (EditorEngine->GetActiveViewportPOV(POV))
					{
						FVector CameraForward = POV.Rotation.GetForwardVector();
						CameraForward.Z = 0.0f;
						if (CameraForward.Length() > 0.0001f)
						{
							CameraForward.Normalize();
							GridForward = CameraForward;
							GridRight = FVector(-CameraForward.Y, CameraForward.X, 0.0f);
						}
						GridOrigin = POV.Location + POV.Rotation.GetForwardVector() * DebugCameraForwardDistance;
					}
				}

				const float RowOffset = bDebugGridCenter ? (static_cast<float>(DebugGridRows - 1) * 0.5f) : 0.0f;
				const float ColOffset = bDebugGridCenter ? (static_cast<float>(DebugGridCols - 1) * 0.5f) : 0.0f;
				const float LayerOffset = bDebugGridCenter ? (static_cast<float>(DebugGridLayers - 1) * 0.5f) : 0.0f;

				std::mt19937 RNG{ std::random_device{}() };
				std::uniform_real_distribution<float> YawDist(-DebugRandomYawRange, DebugRandomYawRange);
				std::uniform_real_distribution<float> JitterXYDist(-DebugJitterXY, DebugJitterXY);
				std::uniform_real_distribution<float> JitterZDist(-DebugJitterZ, DebugJitterZ);

				TArray<AActor*> SpawnedActors;
				SpawnedActors.reserve(static_cast<size_t>(TotalSpawnCount));
				int32 SpawnedCount = 0;
				const FDebugPlaceActorOption& Option = GDebugPlaceActorOptions[DebugPlaceActorTypeIndex];

				for (int32 Layer = 0; Layer < DebugGridLayers; ++Layer)
				{
					for (int32 Row = 0; Row < DebugGridRows; ++Row)
					{
						for (int32 Col = 0; Col < DebugGridCols; ++Col)
						{
							FVector SpawnLocation = GridOrigin
								+ GridRight * ((static_cast<float>(Col) - ColOffset) * DebugGridSpacing)
								+ GridForward * ((static_cast<float>(Row) - RowOffset) * DebugGridSpacing)
								+ FVector(0.0f, 0.0f, (static_cast<float>(Layer) - LayerOffset) * DebugGridSpacing);

							if (bDebugApplyJitter)
							{
								SpawnLocation += GridRight * JitterXYDist(RNG)
									+ GridForward * JitterXYDist(RNG)
									+ FVector(0.0f, 0.0f, JitterZDist(RNG));
							}

							AActor* SpawnedActor = EditorEngine->SpawnPlaceActor(Option.Type, SpawnLocation);
							if (!SpawnedActor)
							{
								continue;
							}

							if (bDebugRandomYaw)
							{
								SpawnedActor->SetActorRotation(FVector(0.0f, YawDist(RNG), 0.0f));
							}

							SpawnedActors.push_back(SpawnedActor);
							++SpawnedCount;
						}
					}
				}

				DebugLastSpawnedActors = std::move(SpawnedActors);
				FEditorConsoleWidget::AddLog("Grid placed: %d actors\n", SpawnedCount);
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Clear Last Batch"))
		{
			bPendingClearLastBatch = true;
		}
	}

	ImGui::End();
}

void FEditorMainPanel::RenderContentBrowserDrawer(float DeltaTime)
{
	constexpr float DrawerMaxHeight = 320.0f;
	constexpr float AnimSpeed = 16.0f;
	constexpr float ConsoleDrawerMaxHeight = 320.0f;

	const bool bShouldBeVisible = !bHideEditorWindows
		&& DocumentTabs.IsLevelEditorActive()
		&& FEditorSettings::Get().UI.bContentBrowser;
	const float TargetAnim = bShouldBeVisible ? 1.0f : 0.0f;
	float Alpha = DeltaTime * AnimSpeed;
	if (Alpha > 1.0f)
	{
		Alpha = 1.0f;
	}
	ContentBrowserDrawerAnim += (TargetAnim - ContentBrowserDrawerAnim) * Alpha;
	if (!bShouldBeVisible && ContentBrowserDrawerAnim < 0.001f)
	{
		ContentBrowserDrawerAnim = 0.0f;
	}
	if (ContentBrowserDrawerAnim <= 0.001f)
	{
		return;
	}

	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	const float ConsoleHeight = ConsoleDrawerMaxHeight * ConsoleDrawerAnim;
	const float DrawerHeight = DrawerMaxHeight * ContentBrowserDrawerAnim;
	if (DrawerHeight <= 1.0f)
	{
		return;
	}

	const ImVec2 DrawerPos(
		MainViewport->WorkPos.x,
		MainViewport->WorkPos.y + MainViewport->WorkSize.y - FooterHeight - ConsoleHeight - DrawerHeight);
	const ImVec2 DrawerSize(MainViewport->WorkSize.x, DrawerHeight);
	ImGui::SetNextWindowPos(DrawerPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(DrawerSize, ImGuiCond_Always);
	if (bBringContentBrowserDrawerToFrontNextFrame)
	{
		ImGui::SetNextWindowFocus();
	}

	ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoCollapse;

	ContentBrowserWidget.RenderWithFlags(DeltaTime, Flags);
	bBringContentBrowserDrawerToFrontNextFrame = false;
}

void FEditorMainPanel::RenderConsoleDrawer(float DeltaTime)
{
	constexpr float DrawerMaxHeight = 320.0f;
	constexpr float AnimSpeed = 16.0f;

	const float TargetAnim = bConsoleDrawerVisible ? 1.0f : 0.0f;
	float Alpha = DeltaTime * AnimSpeed;
	if (Alpha > 1.0f)
	{
		Alpha = 1.0f;
	}
	ConsoleDrawerAnim += (TargetAnim - ConsoleDrawerAnim) * Alpha;
	if (!bConsoleDrawerVisible && ConsoleDrawerAnim < 0.001f)
	{
		ConsoleDrawerAnim = 0.0f;
	}
	if (ConsoleDrawerAnim <= 0.001f)
	{
		return;
	}

	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	const float DrawerHeight = DrawerMaxHeight * ConsoleDrawerAnim;
	if (DrawerHeight <= 1.0f)
	{
		return;
	}

	const ImVec2 DrawerPos(
		MainViewport->WorkPos.x,
		MainViewport->WorkPos.y + MainViewport->WorkSize.y - FooterHeight - DrawerHeight);
	const ImVec2 DrawerSize(MainViewport->WorkSize.x, DrawerHeight);
	ImGui::SetNextWindowPos(DrawerPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(DrawerSize, ImGuiCond_Always);
	if (bBringConsoleDrawerToFrontNextFrame)
	{
		ImGui::SetNextWindowFocus();
	}

	ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoFocusOnAppearing;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.11f, 0.98f));
	if (ImGui::Begin("##ConsoleDrawer", nullptr, Flags))
	{
		ConsoleWidget.RenderDrawerToolbar();
		ImGui::Separator();
		ConsoleWidget.RenderLogContents(0.0f);
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);

	bBringConsoleDrawerToFrontNextFrame = false;
}

void FEditorMainPanel::RenderFooterOverlay(float DeltaTime)
{
	(void)DeltaTime;

	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	const ImVec2 FooterPos(
		MainViewport->WorkPos.x,
		MainViewport->WorkPos.y + MainViewport->WorkSize.y - FooterHeight);
	const ImVec2 FooterSize(MainViewport->WorkSize.x, FooterHeight);

	ImGui::SetNextWindowPos(FooterPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(FooterSize, ImGuiCond_Always);
	ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoNav;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.065f, 0.075f, 0.98f));
	if (ImGui::Begin("##EditorFooter", nullptr, Flags))
	{
		if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
		{
			switch (ConsoleBacktickCycleState)
			{
			case 0:
				ConsoleBacktickCycleState = 1;
				bConsoleDrawerVisible = false;
				bFocusConsoleInputNextFrame = true;
				break;
			case 1:
				ConsoleBacktickCycleState = 2;
				bConsoleDrawerVisible = true;
				FEditorSettings::Get().UI.bContentBrowser = false;
				ContentBrowserDrawerAnim = 0.0f;
				bBringContentBrowserDrawerToFrontNextFrame = false;
				bBringConsoleDrawerToFrontNextFrame = true;
				bFocusConsoleInputNextFrame = true;
				break;
			default:
				ConsoleBacktickCycleState = 0;
				bConsoleDrawerVisible = false;
				bFocusConsoleInputNextFrame = false;
				bFocusConsoleButtonNextFrame = true;
				break;
			}
		}

		if (bFocusConsoleButtonNextFrame)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusConsoleButtonNextFrame = false;
		}

		const bool bContentBrowserOpen = FEditorSettings::Get().UI.bContentBrowser;
		if (bContentBrowserOpen)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::SmallButton("Content"))
		{
			ToggleContentBrowserDrawer();
		}
		if (bContentBrowserOpen)
		{
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();

		if (ImGui::SmallButton("Console"))
		{
			ToggleConsoleDrawer(true);
		}

		ImGui::SameLine();
		const bool bDrawerOpen = ConsoleDrawerAnim > 0.5f;
		const float InputWidth = MainViewport->WorkSize.x * (bDrawerOpen ? 0.35f : 0.175f);
		ConsoleWidget.RenderInputLine("##FooterConsoleInput", InputWidth, bFocusConsoleInputNextFrame);
		if (bFocusConsoleInputNextFrame)
		{
			ConsoleBacktickCycleState = bConsoleDrawerVisible ? 2 : 1;
		}
		bFocusConsoleInputNextFrame = false;

		ImGui::SameLine();
		ImGui::Text("Domain: %s", EditorEngine && EditorEngine->IsPlayingInEditor() ? "PIE" : "Editor");

		const FString LevelLabel = EditorEngine && EditorEngine->HasCurrentLevelFilePath()
			? FString("Level: ") + EditorEngine->GetCurrentLevelFilePath()
			: FString("Level: Unsaved");
		const float LevelWidth = ImGui::CalcTextSize(LevelLabel.c_str()).x;
		const float LevelX = MainViewport->WorkSize.x - ImGui::GetStyle().WindowPadding.x - LevelWidth;

		const char* LatestLog = ConsoleWidget.GetLatestLogMessage();
		if (LatestLog && LatestLog[0] != '\0')
		{
			const float LogWidth = ImGui::CalcTextSize(LatestLog).x;
			float LogX = LevelX - 16.0f - LogWidth;
			const float MinLogX = ImGui::GetCursorPosX() + 8.0f;
			if (LogX < MinLogX)
			{
				LogX = MinLogX;
			}
			ImGui::SameLine(LogX);
			ImGui::TextUnformatted(LatestLog);
		}

		ImGui::SameLine(LevelX);
		ImGui::TextUnformatted(LevelLabel.c_str());
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

void FEditorMainPanel::Update()
{
	HandleGlobalShortcuts();
	ProcessPendingDebugActions();

	ImGuiIO& IO = ImGui::GetIO();

	// GuiState 는 ImGui IO 의 충실한 미러 한 곳뿐.
	// "뷰포트 위면 해제" 핵은 제거 — 입력 소유권은 이제 FSlateApplication 의
	// ImGui 인지 hover 가 단독으로 결정한다.
	InputSystem::Get().GetGuiInputState().bUsingMouse     = IO.WantCaptureMouse;
	InputSystem::Get().GetGuiInputState().bUsingKeyboard  = IO.WantCaptureKeyboard || bShowShortcutOverlay;
	InputSystem::Get().GetGuiInputState().bUsingTextInput = IO.WantTextInput;

	// ImGui 사실을 입력 소유권 중재자에 주입
	FSlateApplication::Get().SetTextInputActive(IO.WantTextInput);

	// IME는 ImGui가 텍스트 입력을 원할 때만 활성화.
	if (Window)
	{
		HWND hWnd = Window->GetHWND();
		if (IO.WantTextInput)
		{
			ImmAssociateContextEx(hWnd, NULL, IACE_DEFAULT);
		}
		else
		{
			ImmAssociateContext(hWnd, NULL);
		}
	}
}

void FEditorMainPanel::ToggleContentBrowserDrawer()
{
	FEditorSettings& Settings = FEditorSettings::Get();
	Settings.UI.bContentBrowser = !Settings.UI.bContentBrowser;
	if (Settings.UI.bContentBrowser)
	{
		bConsoleDrawerVisible = false;
		ConsoleDrawerAnim = 0.0f;
		ConsoleBacktickCycleState = 0;
		bFocusConsoleInputNextFrame = false;
		bBringConsoleDrawerToFrontNextFrame = false;
	}
	bBringContentBrowserDrawerToFrontNextFrame = Settings.UI.bContentBrowser;
}

void FEditorMainPanel::ToggleConsoleDrawer(bool bFocusInput)
{
	bConsoleDrawerVisible = !bConsoleDrawerVisible;
	if (bConsoleDrawerVisible)
	{
		FEditorSettings::Get().UI.bContentBrowser = false;
		ContentBrowserDrawerAnim = 0.0f;
		bBringContentBrowserDrawerToFrontNextFrame = false;
	}
	bBringConsoleDrawerToFrontNextFrame = bConsoleDrawerVisible;
	bFocusConsoleInputNextFrame = bConsoleDrawerVisible && bFocusInput;
	ConsoleBacktickCycleState = bConsoleDrawerVisible ? 2 : 0;
	if (!bConsoleDrawerVisible)
	{
		bFocusConsoleButtonNextFrame = true;
	}
}

void FEditorMainPanel::ProcessPendingDebugActions()
{
	if (!EditorEngine)
	{
		return;
	}

	FLuaDebugEditorFocusRequest LuaDebugFocus;
	if (FLuaDebugManager::PeekEditorFocusRequest(LuaDebugFocus)
		&& LuaDebugFocus.Serial != LastLuaDebugEditorFocusSerial
		&& !LuaDebugFocus.Location.BlueprintPath.empty())
	{
		LastLuaDebugEditorFocusSerial = LuaDebugFocus.Serial;
		if (EditorEngine && EditorEngine->IsPIEPossessedMode())
		{
			EditorEngine->TogglePIEControlMode();
		}
		FSlateApplication::Get().ClearInputOwner();
		InputSystem::Get().ResetTransientState();
		if (ULuaBlueprintAsset* Blueprint = FLuaBlueprintManager::Get().Load(LuaDebugFocus.Location.BlueprintPath))
		{
			ShowEditorWindows();
			OpenAssetEditorForObject(Blueprint);
		}
	}

	if (!bPendingClearLastBatch)
	{
		return;
	}
	bPendingClearLastBatch = false;

	UWorld* World = EditorEngine->GetWorld();
	int32 DestroyedCount = 0;
	if (!World)
	{
		DebugLastSpawnedActors.clear();
		FEditorConsoleWidget::AddLog("Grid cleared: 0 actors\n");
		return;
	}

	EditorEngine->GetSelectionManager().ClearSelection();
	for (AActor* Actor : DebugLastSpawnedActors)
	{
		if (!IsValid(Actor) || Actor->GetWorld() != World)
		{
			continue;
		}

		World->DestroyActor(Actor);
		++DestroyedCount;
	}

	DebugLastSpawnedActors.clear();
	FEditorConsoleWidget::AddLog("Grid cleared: %d actors\n", DestroyedCount);
}

void FEditorMainPanel::HandleGlobalShortcuts()
{
	if (!EditorEngine)
	{
		return;
	}
	if (EditorEngine->IsPIEPossessedMode())
	{
		return;
	}

	ImGuiIO& IO = ImGui::GetIO();
	if (IO.WantTextInput)
	{
		return;
	}

	InputSystem& Input = InputSystem::Get();
	if (!Input.GetKey(VK_CONTROL))
	{
		return;
	}

	if (Input.GetKeyDown(VK_SPACE))
	{
		ToggleContentBrowserDrawer();
		return;
	}

	const bool bShift = Input.GetKey(VK_SHIFT);
	if (Input.GetKeyDown('N'))
	{
		EditorEngine->NewScene();
	}
	else if (Input.GetKeyDown('O'))
	{
		EditorEngine->LoadSceneWithDialog();
	}
	else if (Input.GetKeyDown('S'))
	{
		if (bShift)
		{
			EditorEngine->SaveSceneAsWithDialog();
		}
		else
		{
			EditorEngine->SaveScene();
		}
	}
}

void FEditorMainPanel::HideEditorWindows()
{
	if (bHasSavedUIVisibility)
	{
		bHideEditorWindows = true;
		bShowWidgetList = false;
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();
	SavedUIVisibility = Settings.UI;
	bSavedShowWidgetList = bShowWidgetList;
	bHasSavedUIVisibility = true;
	bHideEditorWindows = true;
	bShowWidgetList = false;

	Settings.UI.bConsole = false;
	Settings.UI.bControl = false;
	Settings.UI.bProperty = false;
	Settings.UI.bReflectionProperty = false;
	Settings.UI.bScene = false;
	Settings.UI.bSceneManager = false;
	Settings.UI.bStat = false;
	Settings.UI.bContentBrowser = false;
	Settings.UI.bImGUISettings = false;
	Settings.UI.bEditorDebug = false;
	Settings.UI.bShadowMapDebug = false;
}

void FEditorMainPanel::ShowEditorWindows()
{
	if (!bHasSavedUIVisibility)
	{
		bHideEditorWindows = false;
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();
	Settings.UI = SavedUIVisibility;
	bShowWidgetList = bSavedShowWidgetList;
	bHideEditorWindows = false;
	bHasSavedUIVisibility = false;
}

void FEditorMainPanel::HideEditorWindowsForPIE()
{
	HideEditorWindows();
}

void FEditorMainPanel::RestoreEditorWindowsAfterPIE()
{
	ShowEditorWindows();
}

void FEditorMainPanel::OpenAssetEditorForObject(UObject* Object)
{
	FAssetEditorOpenResult Result = AssetEditorManager.OpenEditorForObject(Object);
	if (Result.bOpened && Result.bDocumentTab)
	{
		DocumentTabs.OpenOrFocusTab(Result.TabId, Result.Label, true);
	}
}

void FEditorMainPanel::OpenUIEditor(const std::filesystem::path& Path)
{
	UIEditorWidget.Open(Path);
}

void FEditorMainPanel::CollectAssetEditorPreviewViewportClients(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (!DocumentTabs.IsLevelEditorActive())
	{
		AssetEditorManager.CollectPreviewViewportClientsForTab(DocumentTabs.GetActiveTab(), OutClients);
	}
}

bool FEditorMainPanel::IsMouseOverAssetEditorPreviewViewport() const
{
	if (DocumentTabs.IsLevelEditorActive())
	{
		return false;
	}

	return AssetEditorManager.IsMouseOverEditorViewportForTab(DocumentTabs.GetActiveTab());
}
