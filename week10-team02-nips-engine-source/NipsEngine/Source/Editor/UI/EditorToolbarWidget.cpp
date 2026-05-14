#include "Editor/UI/EditorToolbarWidget.h"

#include "Editor/UI/EditorContentDrawerWidget.h"
#include "Editor/UI/EditorSceneWidget.h"
#include "Editor/UI/EditorViewportOverlayWidget.h"
#include "Editor/UI/EditorPlayStreamWidget.h"
#include "Editor/EditorEngine.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Viewport/ViewportLayout.h"
#include "Component/GizmoComponent.h"
#include "Core/ResourceManager.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Render/Resource/Texture.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "Serialization/SceneSaveManager.h"
#include "ImGui/imgui.h"

#include <algorithm>
#include <cmath>
#include <Windows.h>
#include <commdlg.h>
#include <filesystem>
#include <shellapi.h>
#include <cstdio>

namespace
{
	template <typename T>
	AActor* SpawnEditorActor(UWorld* World, const FVector& Location)
	{
		if (!World)
		{
			return nullptr;
		}

		T* Actor = World->SpawnActor<T>();
		if (!Actor)
		{
			return nullptr;
		}

		Actor->InitDefaultComponents();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	struct FAddActorEntry
	{
		const char* Category;
		const char* Label;
		AActor* (*Spawn)(UWorld*, const FVector&);
	};

	static const FAddActorEntry AddActorTypes[] = {
		{ "Basic", "Pawn", SpawnEditorActor<APawnActor> },
		{ "Basic", "Scene", SpawnEditorActor<ASceneActor> },
		{ "Rendering", "StaticMesh", SpawnEditorActor<AStaticMeshActor> },
		{ "Rendering", "SkeletalMesh", SpawnEditorActor<ASkeletalMeshActor> },
		{ "Rendering", "TextRender", SpawnEditorActor<ATextRenderActor> },
		{ "Rendering", "SubUV", SpawnEditorActor<ASubUVActor> },
		{ "Rendering", "Billboard", SpawnEditorActor<ABillboardActor> },
		{ "Rendering", "Decal", SpawnEditorActor<ADecalActor> },
		{ "Light", "Directional Light", SpawnEditorActor<ADirectionalLightActor> },
		{ "Light", "Ambient Light", SpawnEditorActor<AAmbientLightActor> },
		{ "Light", "Point Light", SpawnEditorActor<APointLightActor> },
		{ "Light", "Spot Light", SpawnEditorActor<ASpotLightActor> },
		{ "Environment", "Sky Atmosphere", SpawnEditorActor<ASkyAtmosphereActor> },
		{ "Environment", "Height Fog", SpawnEditorActor<AHeightFogActor> },
		{ "Audio", "Audio Zone", SpawnEditorActor<AAudioZoneActor> },
		{ "Gameplay", "Player Start", SpawnEditorActor<APlayerStartActor> },
	};

	bool RenderToolbarIconButton(const char* Id, UTexture* Texture, const ImVec2& ButtonSize, const ImVec2& IconSize, const ImVec4& IconTint)
	{
		if (!Texture || !Texture->GetSRV())
		{
			return false;
		}

		ImGui::InvisibleButton(Id, ButtonSize);
		const bool bClicked = ImGui::IsItemClicked();
		const ImVec2 ButtonMin = ImGui::GetItemRectMin();
		const ImVec2 ButtonMax = ImGui::GetItemRectMax();
		const ImVec2 ButtonCenter(
			(ButtonMin.x + ButtonMax.x) * 0.5f,
			(ButtonMin.y + ButtonMax.y) * 0.5f
		);
		const ImVec2 IconMin(
			ButtonCenter.x - IconSize.x * 0.5f,
			ButtonCenter.y - IconSize.y * 0.5f
		);
		const ImVec2 IconMax(
			ButtonCenter.x + IconSize.x * 0.5f,
			ButtonCenter.y + IconSize.y * 0.5f
		);

		ImVec4 DrawTint = IconTint;
		if (ImGui::IsItemHovered())
		{
			DrawTint.x = (DrawTint.x + 0.12f < 1.0f) ? DrawTint.x + 0.12f : 1.0f;
			DrawTint.y = (DrawTint.y + 0.12f < 1.0f) ? DrawTint.y + 0.12f : 1.0f;
			DrawTint.z = (DrawTint.z + 0.12f < 1.0f) ? DrawTint.z + 0.12f : 1.0f;
		}
		if (ImGui::IsItemActive())
		{
			DrawTint.x *= 0.85f;
			DrawTint.y *= 0.85f;
			DrawTint.z *= 0.85f;
		}

		ImGui::GetWindowDrawList()->AddImage(
			reinterpret_cast<ImTextureID>(Texture->GetSRV()),
			IconMin,
			IconMax,
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f),
			ImGui::ColorConvertFloat4ToU32(DrawTint)
		);

		return bClicked;
	}

	void ShowLastItemTooltip(const char* Title, const char* Description = nullptr)
	{
		if (!Title || !ImGui::IsItemHovered())
		{
			return;
		}

		ImGui::BeginTooltip();
		ImGui::TextUnformatted(Title);
		if (Description && Description[0] != '\0')
		{
			ImGui::Separator();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
			ImGui::TextUnformatted(Description);
			ImGui::PopTextWrapPos();
		}
		ImGui::EndTooltip();
	}

	ImVec2 GetLastItemPopupPos()
	{
		const ImVec2 ItemMin = ImGui::GetItemRectMin();
		const ImVec2 ItemMax = ImGui::GetItemRectMax();
		return ImVec2(ItemMin.x, ItemMax.y + ImGui::GetStyle().ItemSpacing.y);
	}

	bool DrawSnapPopup(const char* PopupId, const ImVec2& PopupPos, const float* Values, int32 ValueCount, float CurrentValue, bool bEnabled, bool bDegrees, bool& bOutEnabled, float& OutValue)
	{
		bool bChanged = false;
		ImGui::SetNextWindowPos(PopupPos, ImGuiCond_Always);
		if (!ImGui::BeginPopup(PopupId))
		{
			return false;
		}

		if (ImGui::Selectable("Off", !bEnabled))
		{
			bOutEnabled = false;
			OutValue = CurrentValue;
			bChanged = true;
		}

		for (int32 Index = 0; Index < ValueCount; ++Index)
		{
			const float Value = Values[Index];
			char Label[32] = {};
			if (bDegrees)
			{
				std::snprintf(Label, sizeof(Label), "%.0f deg", Value);
			}
			else
			{
				std::snprintf(Label, sizeof(Label), "%.2g", Value);
			}

			const bool bSelected = bEnabled && std::abs(CurrentValue - Value) < 0.0001f;
			if (ImGui::Selectable(Label, bSelected))
			{
				bOutEnabled = true;
				OutValue = Value;
				bChanged = true;
			}
		}

		ImGui::EndPopup();
		return bChanged;
	}

	static const char* AddActorCategories[] = {
		"Basic",
		"Rendering",
		"Light",
		"Environment",
		"Audio",
		"Gameplay",
	};

	std::wstring GetSceneDialogInitialDir()
	{
		std::filesystem::path SceneDir(FSceneSaveManager::GetSceneDirectory());
		SceneDir = SceneDir.lexically_normal();
		if (!SceneDir.is_absolute())
		{
			SceneDir = std::filesystem::path(FPaths::ToAbsolute(SceneDir.wstring()));
		}
		SceneDir.make_preferred();
		std::error_code Ec;
		std::filesystem::create_directories(SceneDir, Ec);
		return SceneDir.wstring();
	}
}

void FEditorToolbarWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	TranslateIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Translate.png");
	RotateIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Rotate.png");
	ScaleIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Scale.png");
	WorldSpaceIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/WorldSpace.png");
	LocalSpaceIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/LocalSpace.png");
	TranslateSnapIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Translate_Snap.png");
	RotateSnapIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Rotate_Snap.png");
	ScaleSnapIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Scale_Snap.png");
	ShowFlagIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Show_Flag.png");
	CameraIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Camera.png");
}

bool FEditorToolbarWidget::OpenSceneFileDialog(FString& OutFilePath) const
{
	OutFilePath.clear();

	WCHAR FileBuffer[MAX_PATH] = { 0 };
	const std::wstring InitialDir = GetSceneDialogInitialDir();
	const std::filesystem::path PrevCwd = std::filesystem::current_path();
	std::error_code ChdirEc;
	std::filesystem::current_path(std::filesystem::path(InitialDir), ChdirEc);

	const std::wstring OpenPattern = std::filesystem::path(InitialDir).append(L"*.Scene").wstring();
	wcsncpy_s(FileBuffer, MAX_PATH, OpenPattern.c_str(), _TRUNCATE);

	OPENFILENAMEW DialogDesc = {};
	DialogDesc.lStructSize = sizeof(DialogDesc);
	DialogDesc.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
	DialogDesc.lpstrFilter = L"Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0";
	DialogDesc.lpstrFile = FileBuffer;
	DialogDesc.nMaxFile = MAX_PATH;
	DialogDesc.lpstrInitialDir = InitialDir.c_str();
	DialogDesc.lpstrDefExt = L"Scene";
	DialogDesc.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	const BOOL bPicked = GetOpenFileNameW(&DialogDesc);
	std::error_code RestoreEc;
	std::filesystem::current_path(PrevCwd, RestoreEc);
	if (!bPicked)
	{
		return false;
	}

	OutFilePath = FPaths::ToUtf8(FileBuffer);
	return true;
}

bool FEditorToolbarWidget::SaveSceneFileDialog(FString& OutFilePath) const
{
	OutFilePath.clear();

	WCHAR FileBuffer[MAX_PATH] = { 0 };
	const std::wstring InitialDir = GetSceneDialogInitialDir();
	const std::filesystem::path PrevCwd = std::filesystem::current_path();
	std::error_code ChdirEc;
	std::filesystem::current_path(std::filesystem::path(InitialDir), ChdirEc);

	const std::wstring DefaultFile = std::filesystem::path(InitialDir).append(L"NewScene.Scene").wstring();
	wcsncpy_s(FileBuffer, MAX_PATH, DefaultFile.c_str(), _TRUNCATE);

	OPENFILENAMEW DialogDesc = {};
	DialogDesc.lStructSize = sizeof(DialogDesc);
	DialogDesc.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
	DialogDesc.lpstrFilter = L"Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0";
	DialogDesc.lpstrFile = FileBuffer;
	DialogDesc.nMaxFile = MAX_PATH;
	DialogDesc.lpstrInitialDir = InitialDir.c_str();
	DialogDesc.lpstrDefExt = L"Scene";
	DialogDesc.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

	const BOOL bPicked = GetSaveFileNameW(&DialogDesc);
	std::error_code RestoreEc;
	std::filesystem::current_path(PrevCwd, RestoreEc);
	if (!bPicked)
	{
		return false;
	}

	OutFilePath = FPaths::ToUtf8(FileBuffer);
	return true;
}

void FEditorToolbarWidget::SetViewportOverlayWidget(FEditorViewportOverlayWidget* InViewportOverlayWidget)
{
	ViewportOverlayWidget = InViewportOverlayWidget;
}

void FEditorToolbarWidget::SetSceneWidget(FEditorSceneWidget* InSceneWidget)
{
	SceneWidget = InSceneWidget;
}

void FEditorToolbarWidget::SetPlayStreamWidget(FEditorPlayStreamWidget* InPlayStreamWidget)
{
	PlayStreamWidget = InPlayStreamWidget;
}

void FEditorToolbarWidget::SetContentDrawerWidget(FEditorContentDrawerWidget* InContentDrawerWidget)
{
	ContentDrawerWidget = InContentDrawerWidget;
}

void FEditorToolbarWidget::SetPanelVisibilityRefs(
	bool* InShowConsole,
	bool* InShowControl,
	bool* InShowProperty,
	bool* InShowSceneManager,
	bool* InShowMaterialEditor,
	bool* InShowStatProfiler,
	bool* InShowCameraShake,
	bool* InShowSkeletalMeshViewer,
	bool* InShowEditorDebug)
{
	bShowConsole = InShowConsole;
	bShowControl = InShowControl;
	bShowProperty = InShowProperty;
	bShowSceneManager = InShowSceneManager;
	bShowMaterialEditor = InShowMaterialEditor;
	bShowStatProfiler = InShowStatProfiler;
	bShowCameraShake = InShowCameraShake;
	bShowSkeletalMeshViewer = InShowSkeletalMeshViewer;
	bShowEditorDebug = InShowEditorDebug;
}

void FEditorToolbarWidget::Render(float DeltaTime)
{
	(void)DeltaTime;
	constexpr float EditorToolBarHeight = 40.0f;

	const ImGuiIO& IO = ImGui::GetIO();

	if (!IO.WantTextInput && IO.KeyCtrl)
	{
		if (ContentDrawerWidget && ImGui::IsKeyPressed(ImGuiKey_Space, false))
		{
			ContentDrawerWidget->ToggleOpen();
		}

		if (SceneWidget && ImGui::IsKeyPressed(ImGuiKey_N, false))
		{
			SceneWidget->NewScene();
		}
		if (SceneWidget && ImGui::IsKeyPressed(ImGuiKey_O, false))
		{
			FString PickedPath;
			if (OpenSceneFileDialog(PickedPath))
			{
				SceneWidget->LoadSceneFromFilePath(PickedPath);
			}
		}
		if (SceneWidget && ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			FString PickedPath;
			if (SaveSceneFileDialog(PickedPath))
			{
				SceneWidget->SaveSceneToFilePath(PickedPath);
			}
		}
	}

	ImVec2 OriginalPadding = ImGui::GetStyle().FramePadding;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(OriginalPadding.x, 5.0f));
	const float MenuBarHeight = ImGui::GetFrameHeight();
	ReservedTopHeight = MenuBarHeight + EditorToolBarHeight;

	bool bMenuBarOpened = ImGui::BeginMainMenuBar();

	ImGui::PopStyleVar();

	if (!bMenuBarOpened)
	{
		return;
	}

	RenderFilesMenu();
    RenderWindowMenu();
	RenderEditMenu();
	RenderHelpMenu();

	ImGui::EndMainMenuBar();
	RenderEditorToolBar(MenuBarHeight, EditorToolBarHeight);
}

void FEditorToolbarWidget::RenderEditorToolBar(float MenuBarHeight, float ToolBarHeight)
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(MainViewport->Pos.x, MainViewport->Pos.y + MenuBarHeight));
	ImGui::SetNextWindowSize(ImVec2(MainViewport->Size.x, ToolBarHeight));
	ImGui::SetNextWindowViewport(MainViewport->ID);

	constexpr ImGuiWindowFlags ToolBarFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoNavFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 5.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));

	if (ImGui::Begin("##EditorGlobalToolBar", nullptr, ToolBarFlags))
	{
		int32 FocusedViewportIndex = 0;
		if (EditorEngine)
		{
			FocusedViewportIndex = EditorEngine->GetViewportLayout().GetLastFocusedViewportIndex();
		}

		RenderAddActorMenu(FocusedViewportIndex);
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		RenderGizmoTools();
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		constexpr ImVec2 SettingsButtonSize(28.0f, 22.0f);
		constexpr ImVec2 SettingsIconSize(18.0f, 18.0f);
		const ImVec4 SettingsIconTint(0.78f, 0.80f, 0.84f, 1.0f);

		const bool bShowSettingsClicked = ShowFlagIconTexture && ShowFlagIconTexture->GetSRV()
			? RenderToolbarIconButton("##ShowSettings", ShowFlagIconTexture, SettingsButtonSize, SettingsIconSize, SettingsIconTint)
			: ImGui::Button("Show", ImVec2(58.0f, 22.0f));
		const ImVec2 ShowSettingsPopupPos = GetLastItemPopupPos();
		ShowLastItemTooltip("Show settings", "Open viewport visibility and overlay display options.");
		if (bShowSettingsClicked)
		{
			ImGui::OpenPopup("ShowSettingsPopup");
		}

		ImGui::SameLine();
		const bool bCameraSettingsClicked = CameraIconTexture && CameraIconTexture->GetSRV()
			? RenderToolbarIconButton("##CameraSettings", CameraIconTexture, SettingsButtonSize, SettingsIconSize, SettingsIconTint)
			: ImGui::Button("Camera", ImVec2(68.0f, 22.0f));
		const ImVec2 CameraSettingsPopupPos = GetLastItemPopupPos();
		ShowLastItemTooltip("Camera settings", "Open camera movement and viewport camera options.");
		if (bCameraSettingsClicked)
		{
			ImGui::OpenPopup("CameraSettingsPopup");
		}

		ImGui::SetNextWindowPos(ShowSettingsPopupPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(360.0f, 430.0f), ImGuiCond_Appearing);
		if (ImGui::BeginPopup("ShowSettingsPopup"))
		{
			if (ViewportOverlayWidget)
			{
				ViewportOverlayWidget->RenderViewportSettings(0.0f, false, EViewportSettingsSection::Show);
			}
			else
			{
				ImGui::TextDisabled("Viewport settings unavailable.");
			}
			ImGui::EndPopup();
		}

		ImGui::SetNextWindowPos(CameraSettingsPopupPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(360.0f, 240.0f), ImGuiCond_Appearing);
		if (ImGui::BeginPopup("CameraSettingsPopup"))
		{
			if (ViewportOverlayWidget)
			{
				ViewportOverlayWidget->RenderViewportSettings(0.0f, false, EViewportSettingsSection::Camera);
			}
			else
			{
				ImGui::TextDisabled("Camera settings unavailable.");
			}
			ImGui::EndPopup();
		}

		if (PlayStreamWidget)
		{
			ImGui::SameLine();
			PlayStreamWidget->Render(0.0f);
		}
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(4);
}

void FEditorToolbarWidget::RenderAddActorMenu(int32 ViewportIndex)
{
	constexpr ImVec2 ToolButtonSize(68.0f, 22.0f);
	constexpr float CountWidth = 48.0f;

	ImGui::PushID(ViewportIndex);
	if (ImGui::Button("+  Add", ToolButtonSize))
	{
		ImGui::OpenPopup("AddActorMenu");
	}
	const ImVec2 AddActorPopupPos = GetLastItemPopupPos();
	ShowLastItemTooltip("Add actor", "Open the actor creation menu for the focused viewport.");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(CountWidth);
	if (ImGui::DragInt("##SpawnCount", &SpawnCount, 0.1f, 1, 100))
	{
		SpawnCount = std::clamp(SpawnCount, 1, 100);
	}
	ShowLastItemTooltip("Spawn count", "Set how many actors to create from the Add menu.");

	ImGui::SetNextWindowPos(AddActorPopupPos, ImGuiCond_Always);
	if (!ImGui::BeginPopup("AddActorMenu"))
	{
		ImGui::PopID();
		return;
	}

	if (!EditorEngine)
	{
		ImGui::TextDisabled("No editor world.");
		ImGui::EndPopup();
		ImGui::PopID();
		return;
	}

	UWorld* World = EditorEngine->GetFocusedWorld();
	if (!World)
	{
		ImGui::TextDisabled("No focused world.");
		ImGui::EndPopup();
		ImGui::PopID();
		return;
	}

	const char* CurrentCategory = nullptr;
	for (const FAddActorEntry& Entry : AddActorTypes)
	{
		if (CurrentCategory == nullptr || strcmp(CurrentCategory, Entry.Category) != 0)
		{
			CurrentCategory = Entry.Category;
			ImGui::SeparatorText(CurrentCategory);
		}

		if (ImGui::Selectable(Entry.Label))
		{
			AActor* LastSpawnedActor = nullptr;
			const FVector BaseLocation = GetActorPlacementLocation(ViewportIndex);
			const int32 ClampedCount = std::clamp(SpawnCount, 1, 100);
			for (int32 Index = 0; Index < ClampedCount; ++Index)
			{
				const FVector Offset(static_cast<float>(Index) * 1.5f, 0.0f, 0.0f);
				LastSpawnedActor = Entry.Spawn(World, BaseLocation + Offset);
			}

			if (LastSpawnedActor)
			{
				EditorEngine->GetSelectionManager().Select(LastSpawnedActor);
			}
			SpawnCount = 1;
		}
	}

	ImGui::EndPopup();
	ImGui::PopID();
}

void FEditorToolbarWidget::RenderGizmoTools()
{
	if (!EditorEngine || !EditorEngine->GetGizmo())
	{
		ImGui::TextDisabled("Gizmo unavailable");
		return;
	}

	constexpr ImVec2 GizmoModeButtonSize(28.0f, 22.0f);
	constexpr ImVec2 GizmoModeIconSize(18.0f, 18.0f);
	const ImVec4 ActiveTint(0.149f, 0.733f, 1.0f, 1.0f); // #26bbff
	const ImVec4 InactiveTint(0.78f, 0.80f, 0.84f, 1.0f);

	UGizmoComponent* Gizmo = EditorEngine->GetGizmo();
	const bool bHasTranslateIcon = TranslateIconTexture && TranslateIconTexture->GetSRV();
	const bool bHasRotateIcon = RotateIconTexture && RotateIconTexture->GetSRV();
	const bool bHasScaleIcon = ScaleIconTexture && ScaleIconTexture->GetSRV();

	const ImVec4 TranslateTint = Gizmo->IsTranslateMode() ? ActiveTint : InactiveTint;
	const ImVec4 RotateTint = Gizmo->IsRotateMode() ? ActiveTint : InactiveTint;
	const ImVec4 ScaleTint = Gizmo->IsScaleMode() ? ActiveTint : InactiveTint;

	const bool bTranslateClicked = bHasTranslateIcon
		? RenderToolbarIconButton("##GizmoTranslate", TranslateIconTexture, GizmoModeButtonSize, GizmoModeIconSize, TranslateTint)
		: ImGui::Selectable("Move", Gizmo->IsTranslateMode(), 0, ImVec2(58.0f, 22.0f));
	ShowLastItemTooltip("Location", "Move the selected actor, component, or bone with the translate gizmo.");
	if (bTranslateClicked)
	{
		Gizmo->SetTranslateMode();
	}
	ImGui::SameLine();
	const bool bRotateClicked = bHasRotateIcon
		? RenderToolbarIconButton("##GizmoRotate", RotateIconTexture, GizmoModeButtonSize, GizmoModeIconSize, RotateTint)
		: ImGui::Selectable("Rotate", Gizmo->IsRotateMode(), 0, ImVec2(58.0f, 22.0f));
	ShowLastItemTooltip("Rotation", "Rotate the selected actor, component, or bone with the rotate gizmo.");
	if (bRotateClicked)
	{
		Gizmo->SetRotateMode();
	}
	ImGui::SameLine();
	const bool bScaleClicked = bHasScaleIcon
		? RenderToolbarIconButton("##GizmoScale", ScaleIconTexture, GizmoModeButtonSize, GizmoModeIconSize, ScaleTint)
		: ImGui::Selectable("Scale", Gizmo->IsScaleMode(), 0, ImVec2(58.0f, 22.0f));
	ShowLastItemTooltip("Scale", "Scale the selected actor, component, or bone with the scale gizmo.");
	if (bScaleClicked)
	{
		Gizmo->SetScaleMode();
	}
	ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

	const bool bWorldSpace = Gizmo->IsWorldSpace();
	UTexture* CurrentSpaceIconTexture = bWorldSpace ? WorldSpaceIconTexture : LocalSpaceIconTexture;
	const bool bHasSpaceIcon = CurrentSpaceIconTexture && CurrentSpaceIconTexture->GetSRV();
	constexpr ImVec2 SpaceButtonSize(28.0f, 22.0f);
	constexpr ImVec2 SpaceIconSize(18.0f, 18.0f);

	const bool bSpaceClicked = bHasSpaceIcon
		? RenderToolbarIconButton("##GizmoSpace", CurrentSpaceIconTexture, SpaceButtonSize, SpaceIconSize, InactiveTint)
		: ImGui::Selectable(bWorldSpace ? "World" : "Local", false, 0, ImVec2(58.0f, 22.0f));
	ShowLastItemTooltip(
		bWorldSpace ? "World space" : "Local space",
		bWorldSpace ? "Use world axes for gizmo movement and rotation." : "Use the selected object's local axes for gizmo movement and rotation.");

	if (bSpaceClicked)
	{
		Gizmo->SetWorldSpace(!bWorldSpace);
	}
	ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
	constexpr ImVec2 SnapButtonSize(28.0f, 22.0f);
	constexpr ImVec2 SnapIconSize(18.0f, 18.0f);
	constexpr float DegToRad = 0.017453292519943295f;
	constexpr float RadToDeg = 57.29577951308232f;
	static constexpr float TranslateSnapValues[] = { 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
	static constexpr float RotateSnapValuesDeg[] = { 5.0f, 10.0f, 15.0f, 45.0f, 90.0f };
	static constexpr float ScaleSnapValues[] = { 0.1f, 0.25f, 0.5f, 1.0f };

	const bool bHasTranslateSnapIcon = TranslateSnapIconTexture && TranslateSnapIconTexture->GetSRV();
	const bool bHasRotateSnapIcon = RotateSnapIconTexture && RotateSnapIconTexture->GetSRV();
	const bool bHasScaleSnapIcon = ScaleSnapIconTexture && ScaleSnapIconTexture->GetSRV();

	const bool bTranslateSnapClicked = bHasTranslateSnapIcon
		? RenderToolbarIconButton("##TranslateSnap", TranslateSnapIconTexture, SnapButtonSize, SnapIconSize, Gizmo->IsTranslateSnapEnabled() ? ActiveTint : InactiveTint)
		: ImGui::Button("T Snap", ImVec2(62.0f, 22.0f));
	const ImVec2 TranslateSnapPopupPos = GetLastItemPopupPos();
	ShowLastItemTooltip("Location snap", "Choose fixed distance increments for translate gizmo movement.");
	if (bTranslateSnapClicked)
	{
		ImGui::OpenPopup("TranslateSnapPopup");
	}
	bool bSnapEnabled = Gizmo->IsTranslateSnapEnabled();
	float SnapValue = Gizmo->GetTranslateSnapValue();
	if (DrawSnapPopup("TranslateSnapPopup", TranslateSnapPopupPos, TranslateSnapValues, IM_ARRAYSIZE(TranslateSnapValues), SnapValue, bSnapEnabled, false, bSnapEnabled, SnapValue))
	{
		Gizmo->SetTranslateSnap(bSnapEnabled, SnapValue);
	}

	ImGui::SameLine();
	const bool bRotateSnapClicked = bHasRotateSnapIcon
		? RenderToolbarIconButton("##RotateSnap", RotateSnapIconTexture, SnapButtonSize, SnapIconSize, Gizmo->IsRotateSnapEnabled() ? ActiveTint : InactiveTint)
		: ImGui::Button("R Snap", ImVec2(62.0f, 22.0f));
	const ImVec2 RotateSnapPopupPos = GetLastItemPopupPos();
	ShowLastItemTooltip("Rotation snap", "Choose fixed angle increments for rotate gizmo movement.");
	if (bRotateSnapClicked)
	{
		ImGui::OpenPopup("RotateSnapPopup");
	}
	bSnapEnabled = Gizmo->IsRotateSnapEnabled();
	SnapValue = Gizmo->GetRotateSnapValue() * RadToDeg;
	if (DrawSnapPopup("RotateSnapPopup", RotateSnapPopupPos, RotateSnapValuesDeg, IM_ARRAYSIZE(RotateSnapValuesDeg), SnapValue, bSnapEnabled, true, bSnapEnabled, SnapValue))
	{
		Gizmo->SetRotateSnap(bSnapEnabled, SnapValue * DegToRad);
	}

	ImGui::SameLine();
	const bool bScaleSnapClicked = bHasScaleSnapIcon
		? RenderToolbarIconButton("##ScaleSnap", ScaleSnapIconTexture, SnapButtonSize, SnapIconSize, Gizmo->IsScaleSnapEnabled() ? ActiveTint : InactiveTint)
		: ImGui::Button("S Snap", ImVec2(62.0f, 22.0f));
	const ImVec2 ScaleSnapPopupPos = GetLastItemPopupPos();
	ShowLastItemTooltip("Scale snap", "Choose fixed size increments for scale gizmo changes.");
	if (bScaleSnapClicked)
	{
		ImGui::OpenPopup("ScaleSnapPopup");
	}
	bSnapEnabled = Gizmo->IsScaleSnapEnabled();
	SnapValue = Gizmo->GetScaleSnapValue();
	if (DrawSnapPopup("ScaleSnapPopup", ScaleSnapPopupPos, ScaleSnapValues, IM_ARRAYSIZE(ScaleSnapValues), SnapValue, bSnapEnabled, false, bSnapEnabled, SnapValue))
	{
		Gizmo->SetScaleSnap(bSnapEnabled, SnapValue);
	}
}

FVector FEditorToolbarWidget::GetActorPlacementLocation(int32 ViewportIndex) const
{
	if (!EditorEngine)
	{
		return FVector::ZeroVector;
	}

	const FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
	const int32 ClampedViewportIndex =
		std::clamp(ViewportIndex, 0, FEditorViewportLayout::MaxViewports - 1);
	const FEditorViewportClient* Client = Layout.GetViewportClient(ClampedViewportIndex);
	const FViewportCamera* Camera = Client ? Client->GetCamera() : nullptr;
	if (!Camera)
	{
		return FVector::ZeroVector;
	}

	FVector Forward = Camera->GetEffectiveForward();
	if (Forward.IsNearlyZero())
	{
		Forward = Camera->GetForwardVector();
	}
	Forward.NormalizeSafe();
	return Camera->GetLocation() + Forward * 5.0f;
}

void FEditorToolbarWidget::RenderFilesMenu()
{
	if (!ImGui::BeginMenu("Files"))
	{
		return;
	}

	if (SceneWidget)
	{
		if (ImGui::MenuItem("New Scene", "Ctrl+N"))
		{
			SceneWidget->NewScene();
		}
		if (ImGui::MenuItem("Load Scene", "Ctrl+O"))
		{
			FString PickedPath;
			if (OpenSceneFileDialog(PickedPath))
			{
				SceneWidget->LoadSceneFromFilePath(PickedPath);
			}
		}
		if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
		{
			FString PickedPath;
			if (SaveSceneFileDialog(PickedPath))
			{
				SceneWidget->SaveSceneToFilePath(PickedPath);
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Reload Asset From Disk"))
		{
			SceneWidget->RefreshSceneAndAssets();
			if (ContentDrawerWidget)
			{
				ContentDrawerWidget->RefreshAssetTree();
			}
		}
		if (ImGui::MenuItem("Open Asset Folder"))
		{
			const std::wstring AssetDir = FPaths::ToAbsolute(L"Asset");
			ShellExecuteW(nullptr, L"open", AssetDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
	}
	else
	{
		ImGui::MenuItem("New Scene", "Ctrl+N", false, false);
		ImGui::MenuItem("Load Scene", "Ctrl+O", false, false);
		ImGui::MenuItem("Save Scene", "Ctrl+S", false, false);
		ImGui::Separator();
		ImGui::MenuItem("Reload Asset From Disk", nullptr, false, false);
		ImGui::MenuItem("Open Asset Folder", nullptr, false, false);
	}

	ImGui::EndMenu();
}

void FEditorToolbarWidget::RenderWindowMenu()
{
	if (!ImGui::BeginMenu("Window"))
	{
		return;
	}

	if (bShowConsole)
	{
		bool bConsoleVisible = *bShowConsole;
		if (ImGui::MenuItem("Console", nullptr, bConsoleVisible))
		{
			*bShowConsole = !bConsoleVisible;
			if (*bShowConsole && ContentDrawerWidget)
			{
				ContentDrawerWidget->StartConsoleTakeover();
			}
		}
	}
	if (bShowProperty) ImGui::MenuItem("Property", nullptr, bShowProperty);
	if (bShowSceneManager) ImGui::MenuItem("Outliner", nullptr, bShowSceneManager);
	if (bShowMaterialEditor) ImGui::MenuItem("Material Editor", nullptr, bShowMaterialEditor);
	if (bShowStatProfiler) ImGui::MenuItem("Stat Profiler", nullptr, bShowStatProfiler);
	if (bShowCameraShake) ImGui::MenuItem("Camera Shake", nullptr, bShowCameraShake);
	if (bShowSkeletalMeshViewer) ImGui::MenuItem("SkeletalMesh Viewer", nullptr, bShowSkeletalMeshViewer);
	if (bShowEditorDebug) ImGui::MenuItem("Editor Debug", nullptr, bShowEditorDebug);

	if (ContentDrawerWidget)
	{
		bool bContentDrawerOpen = ContentDrawerWidget->IsOpen();
		if (ImGui::MenuItem("Content Drawer", "Ctrl+Space", bContentDrawerOpen))
		{
			ContentDrawerWidget->SetOpen(!bContentDrawerOpen);
		}
	}
	else
	{
		ImGui::MenuItem("Content Drawer", "Ctrl+Space", false, false);
	}

	ImGui::EndMenu();
}

void FEditorToolbarWidget::RenderEditMenu()
{
	if (!ImGui::BeginMenu("Edit"))
	{
		return;
	}

	if (ImGui::MenuItem("Remove Cache from Disk"))
	{
		FResourceManager::Get().DeleteAllCacheFiles();
	}

	ImGui::EndMenu();
}

void FEditorToolbarWidget::RenderHelpMenu()
{
	if (!ImGui::BeginMenu("Help"))
	{
		return;
	}

	if (ViewportOverlayWidget)
	{
		if (ImGui::MenuItem("Shortcuts"))
		{
			ViewportOverlayWidget->SetShortcutsWindowVisible(true);
		}
	}
	else
	{
		ImGui::MenuItem("Shortcuts", nullptr, false, false);
	}

	ImGui::EndMenu();
}
