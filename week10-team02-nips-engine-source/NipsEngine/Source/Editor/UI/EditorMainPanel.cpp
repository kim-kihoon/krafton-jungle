

#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/Viewport/ViewportLayout.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "GameFramework/AActor.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include "Render/Renderer/Renderer.h"
#include "Engine/Input/InputRouter.h"
#include "Game/UI/GameUISystem.h"

#include <algorithm>
#include <random>

namespace
{
void SetOpaqueBlendStateCallback(const ImDrawList*, const ImDrawCmd* Cmd)
{
	ID3D11DeviceContext* DeviceContext = static_cast<ID3D11DeviceContext*>(Cmd->UserCallbackData);
	if (!DeviceContext)
		return;

	const float BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xffffffff);
}

ImVec4 ColorFromHex(uint32 Rgb, float Alpha = 1.0f)
{
	return ImVec4(
		static_cast<float>((Rgb >> 16) & 0xff) / 255.0f,
		static_cast<float>((Rgb >> 8) & 0xff) / 255.0f,
		static_cast<float>(Rgb & 0xff) / 255.0f,
		Alpha);
}

void ApplyUnrealImGuiStyle()
{
	ImGuiStyle& Style = ImGui::GetStyle();
	Style.WindowRounding = 2.0f;
	Style.ChildRounding = 2.0f;
	Style.FrameRounding = 2.0f;
	Style.PopupRounding = 2.0f;
	Style.ScrollbarRounding = 2.0f;
	Style.GrabRounding = 2.0f;
	Style.TabRounding = 2.0f;
	Style.WindowBorderSize = 1.0f;
	Style.FrameBorderSize = 1.0f;
	Style.PopupBorderSize = 1.0f;
	Style.WindowPadding = ImVec2(8.0f, 8.0f);
	Style.FramePadding = ImVec2(6.0f, 4.0f);
	Style.ItemSpacing = ImVec2(8.0f, 5.0f);
	Style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);

	ImVec4* Colors = Style.Colors;
	const ImVec4 WindowBg = ColorFromHex(0x151515);
	const ImVec4 ChildBg = ColorFromHex(0x1a1a1a);
	const ImVec4 PanelBg = ColorFromHex(0x242424);
	const ImVec4 PanelHover = ColorFromHex(0x303030);
	const ImVec4 PanelActive = ColorFromHex(0x3a3a3a);
	const ImVec4 Border = ColorFromHex(0x3f3f3f);
	const ImVec4 Text = ColorFromHex(0xd6d6d6);
	const ImVec4 TextDisabled = ColorFromHex(0x808080);
	const ImVec4 Accent = ColorFromHex(0x4f86c6);
	const ImVec4 AccentHover = ColorFromHex(0x5f9ade);
	const ImVec4 AccentActive = ColorFromHex(0x3f6fa8);

	Colors[ImGuiCol_Text] = Text;
	Colors[ImGuiCol_TextDisabled] = TextDisabled;
	Colors[ImGuiCol_WindowBg] = WindowBg;
	Colors[ImGuiCol_ChildBg] = ChildBg;
	Colors[ImGuiCol_PopupBg] = ColorFromHex(0x1a1a1a, 0.98f);
	Colors[ImGuiCol_Border] = Border;
	Colors[ImGuiCol_BorderShadow] = ColorFromHex(0x000000, 0.0f);
	Colors[ImGuiCol_FrameBg] = PanelBg;
	Colors[ImGuiCol_FrameBgHovered] = PanelHover;
	Colors[ImGuiCol_FrameBgActive] = PanelActive;
	Colors[ImGuiCol_TitleBg] = ColorFromHex(0x151515);
	Colors[ImGuiCol_TitleBgActive] = ColorFromHex(0x1a1a1a);
	Colors[ImGuiCol_TitleBgCollapsed] = ColorFromHex(0x151515, 0.9f);
	Colors[ImGuiCol_MenuBarBg] = ColorFromHex(0x1a1a1a);
	Colors[ImGuiCol_ScrollbarBg] = ColorFromHex(0x151515);
	Colors[ImGuiCol_ScrollbarGrab] = ColorFromHex(0x3a3a3a);
	Colors[ImGuiCol_ScrollbarGrabHovered] = ColorFromHex(0x4a4a4a);
	Colors[ImGuiCol_ScrollbarGrabActive] = ColorFromHex(0x5a5a5a);
	Colors[ImGuiCol_CheckMark] = AccentHover;
	Colors[ImGuiCol_SliderGrab] = Accent;
	Colors[ImGuiCol_SliderGrabActive] = AccentHover;
	Colors[ImGuiCol_Button] = PanelBg;
	Colors[ImGuiCol_ButtonHovered] = PanelHover;
	Colors[ImGuiCol_ButtonActive] = PanelActive;
	Colors[ImGuiCol_Header] = ColorFromHex(0x242424);
	Colors[ImGuiCol_HeaderHovered] = ColorFromHex(0x303030);
	Colors[ImGuiCol_HeaderActive] = AccentActive;
	Colors[ImGuiCol_Separator] = Border;
	Colors[ImGuiCol_SeparatorHovered] = Accent;
	Colors[ImGuiCol_SeparatorActive] = AccentHover;
	Colors[ImGuiCol_ResizeGrip] = ColorFromHex(0x4f86c6, 0.35f);
	Colors[ImGuiCol_ResizeGripHovered] = ColorFromHex(0x5f9ade, 0.65f);
	Colors[ImGuiCol_ResizeGripActive] = ColorFromHex(0x5f9ade, 0.9f);
	Colors[ImGuiCol_Tab] = ColorFromHex(0x1a1a1a);
	Colors[ImGuiCol_TabHovered] = ColorFromHex(0x303030);
	Colors[ImGuiCol_TabActive] = ColorFromHex(0x242424);
	Colors[ImGuiCol_TabUnfocused] = ColorFromHex(0x151515);
	Colors[ImGuiCol_TabUnfocusedActive] = ColorFromHex(0x1a1a1a);
	Colors[ImGuiCol_DockingPreview] = ColorFromHex(0x4f86c6, 0.45f);
	Colors[ImGuiCol_DockingEmptyBg] = WindowBg;
	Colors[ImGuiCol_PlotLines] = Accent;
	Colors[ImGuiCol_PlotLinesHovered] = AccentHover;
	Colors[ImGuiCol_PlotHistogram] = ColorFromHex(0xbfa45a);
	Colors[ImGuiCol_PlotHistogramHovered] = ColorFromHex(0xd6bb70);
	Colors[ImGuiCol_TableHeaderBg] = PanelBg;
	Colors[ImGuiCol_TableBorderStrong] = Border;
	Colors[ImGuiCol_TableBorderLight] = ColorFromHex(0x303030);
	Colors[ImGuiCol_TableRowBg] = ColorFromHex(0x000000, 0.0f);
	Colors[ImGuiCol_TableRowBgAlt] = ColorFromHex(0xffffff, 0.035f);
	Colors[ImGuiCol_TextSelectedBg] = ColorFromHex(0x4f86c6, 0.35f);
	Colors[ImGuiCol_DragDropTarget] = ColorFromHex(0x5f9ade, 0.9f);
	Colors[ImGuiCol_NavHighlight] = AccentHover;
	Colors[ImGuiCol_NavWindowingHighlight] = ColorFromHex(0xffffff, 0.45f);
	Colors[ImGuiCol_NavWindowingDimBg] = ColorFromHex(0x000000, 0.35f);
	Colors[ImGuiCol_ModalWindowDimBg] = ColorFromHex(0x000000, 0.55f);
}

const char* GetViewportTypeName(EEditorViewportType Type)
{
	switch (Type)
	{
	case EVT_Perspective:
		return "Perspective";
	case EVT_Orthographic:
		return "Orthographic";
	case EVT_OrthoTop:
		return "Top";
	case EVT_OrthoBottom:
		return "Bottom";
	case EVT_OrthoFront:
		return "Front";
	case EVT_OrthoBack:
		return "Back";
	case EVT_OrthoLeft:
		return "Left";
	case EVT_OrthoRight:
		return "Right";
	default:
		return "Viewport";
	}
}

const char* GetViewModeName(EViewMode Mode)
{
	switch (Mode)
	{
	case EViewMode::Lit:
		return "Lit";
	case EViewMode::Unlit:
		return "Unlit";
	case EViewMode::Wireframe:
		return "Wireframe";
	case EViewMode::SceneDepth:
		return "Scene Depth";
	case EViewMode::WorldNormal:
		return "World Normal";
	case EViewMode::CascadeShadow:
		return "Cascade Shadow";
	case EViewMode::DebugCollision:
		return "Debug Collision";
	default:
		return "Lit";
	}
}

const char* GetViewportSlotName(int32 Index)
{
	switch (Index)
	{
	case 0:
		return "Viewport 0";
	case 1:
		return "Viewport 1";
	case 2:
		return "Viewport 2";
	case 3:
		return "Viewport 3";
	default:
		return "Viewport";
	}
}

enum class EDebugPlaceActorType : uint8
{
	Scene,
	StaticMesh,
	SkeletalMesh,
	Decal,
	HeightFog,
	AmbientLight,
	DirectionalLight,
	PointLight,
	SpotLight,
	Pawn,
	PlayerStart
};

struct FDebugPlaceActorOption
{
	const char* Label = "";
	EDebugPlaceActorType Type = EDebugPlaceActorType::StaticMesh;
};

const FDebugPlaceActorOption GDebugPlaceActorOptions[] = {
	{ "Scene", EDebugPlaceActorType::Scene },
	{ "StaticMesh", EDebugPlaceActorType::StaticMesh },
	{ "SkeletalMesh", EDebugPlaceActorType::SkeletalMesh },
	{ "Decal", EDebugPlaceActorType::Decal },
	{ "Height Fog", EDebugPlaceActorType::HeightFog },
	{ "Ambient Light", EDebugPlaceActorType::AmbientLight },
	{ "Directional Light", EDebugPlaceActorType::DirectionalLight },
	{ "Point Light", EDebugPlaceActorType::PointLight },
	{ "Spot Light", EDebugPlaceActorType::SpotLight },
	{ "Pawn", EDebugPlaceActorType::Pawn },
	{ "Player Start", EDebugPlaceActorType::PlayerStart },
};

template <typename T>
AActor* SpawnDebugActor(UWorld* World, const FVector& Location)
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

AActor* SpawnDebugPlaceActor(UWorld* World, EDebugPlaceActorType Type, const FVector& Location)
{
	switch (Type)
	{
	case EDebugPlaceActorType::Scene:
		return SpawnDebugActor<ASceneActor>(World, Location);
	case EDebugPlaceActorType::StaticMesh:
		return SpawnDebugActor<AStaticMeshActor>(World, Location);
	case EDebugPlaceActorType::SkeletalMesh:
		return SpawnDebugActor<ASkeletalMeshActor>(World, Location);
	case EDebugPlaceActorType::Decal:
		return SpawnDebugActor<ADecalActor>(World, Location + FVector(0.0f, 0.0f, 1.0f));
	case EDebugPlaceActorType::HeightFog:
		return SpawnDebugActor<AHeightFogActor>(World, Location + FVector(0.0f, 0.0f, 1.0f));
	case EDebugPlaceActorType::AmbientLight:
		return SpawnDebugActor<AAmbientLightActor>(World, Location + FVector(0.0f, 0.0f, 1.0f));
	case EDebugPlaceActorType::DirectionalLight:
	{
		AActor* Actor = SpawnDebugActor<ADirectionalLightActor>(World, Location + FVector(0.0f, 0.0f, 1.0f));
		if (Actor)
		{
			Actor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
		}
		return Actor;
	}
	case EDebugPlaceActorType::PointLight:
		return SpawnDebugActor<APointLightActor>(World, Location + FVector(0.0f, 0.0f, 1.0f));
	case EDebugPlaceActorType::SpotLight:
		return SpawnDebugActor<ASpotLightActor>(World, Location + FVector(0.0f, 0.0f, 1.0f));
	case EDebugPlaceActorType::Pawn:
		return SpawnDebugActor<APawnActor>(World, Location);
	case EDebugPlaceActorType::PlayerStart:
		return SpawnDebugActor<APlayerStartActor>(World, Location);
	default:
		return nullptr;
	}
}
} // namespace
void FEditorMainPanel::Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	IO.IniFilename = "imgui_editor.ini";
	ApplyUnrealImGuiStyle();

	Window = InWindow;
	EditorEngine = InEditorEngine;

	// 1차: malgun.ttf — 한글 + 기본 라틴 (주 폰트)
	ImFontGlyphRangesBuilder KoreanBuilder;
	KoreanBuilder.AddRanges(IO.Fonts->GetGlyphRangesKorean());
	KoreanBuilder.AddRanges(IO.Fonts->GetGlyphRangesDefault());

	KoreanBuilder.BuildRanges(&FontGlyphRanges);

	IO.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf", 16.0f, nullptr, FontGlyphRanges.Data);

	ImFontConfig icon_config;
	icon_config.MergeMode = true;  // 중요: 앞서 로드한 맑은 고딕에 폰트를 병합합니다.
	icon_config.PixelSnapH = true; // 아이콘을 픽셀 경계에 맞춰 선명하게 렌더링

	// 추가할 특수 기호의 유니코드 범위 설정 (▶, ⏸, ■)
	// ▶ (U+25B6), ⏸ (U+23F8), ■ (U+25A0)
	static const ImWchar icon_ranges[] = {
		0x23F8, 0x23F8, // ⏸
		0x25A0, 0x25A0, // ■
		0x25B6, 0x25B6, // ▶
		0,              // 배열의 끝을 알리는 0
	};

	IO.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisym.ttf", 16.0f, &icon_config, icon_ranges);

	// 2차: msyh.ttc — 한자 전체를 malgun이 없는 글리프에만 병합 (fallback)
	ImFontConfig MergeConfig;
	MergeConfig.MergeMode = true;
	IO.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 16.0f, &MergeConfig,
								 IO.Fonts->GetGlyphRangesChineseFull());

	ImGui_ImplWin32_Init((void*)InWindow->GetHWND());
	ImGui_ImplDX11_Init(InRenderer.GetFD3DDevice().GetDevice(), InRenderer.GetFD3DDevice().GetDeviceContext());

	ConsoleWidget.Initialize(InEditorEngine);
	ControlWidget.Initialize(InEditorEngine);
	ContentDrawerWidget.Initialize(InEditorEngine);
	ContentDrawerWidget.SetConsoleWidget(&ConsoleWidget);
	MaterialWidget.Initialize(InEditorEngine);
	PropertyWidget.Initialize(InEditorEngine);
	SceneWidget.Initialize(InEditorEngine);
	ViewportOverlayWidget.Initialize(InEditorEngine);
	StatWidget.Initialize(InEditorEngine);
	PlayStreamWidget.Initialize(InEditorEngine);
	CameraShakeWidget.Initialize(InEditorEngine);
	ToolbarWidget.Initialize(InEditorEngine);
	ToolbarWidget.SetViewportOverlayWidget(&ViewportOverlayWidget);
	ToolbarWidget.SetSceneWidget(&SceneWidget);
	ToolbarWidget.SetPlayStreamWidget(&PlayStreamWidget);
	ToolbarWidget.SetContentDrawerWidget(&ContentDrawerWidget);
	ToolbarWidget.SetPanelVisibilityRefs(&bShowConsole, &bShowControl, &bShowProperty, &bShowSceneManager, 
		&bShowMaterialEditor, &bShowStatProfiler, &bShowCameraShake, nullptr, &bShowEditorDebug);
}

void FEditorMainPanel::Release()
{
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void FEditorMainPanel::Render(float DeltaTime)
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ProcessPendingDebugActions();
	ToolbarWidget.Render(DeltaTime);

	const ImGuiID DockspaceId = ImGui::GetID("EditorDockSpaceV3");
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	const float ReservedTopHeight = ToolbarWidget.GetReservedTopHeight();
	const float ReservedBottomHeight = std::max(
		ContentDrawerWidget.GetReservedBottomHeight(),
		ConsoleWidget.GetReservedBottomHeight());
	ImGui::SetNextWindowPos(ImVec2(MainViewport->Pos.x, MainViewport->Pos.y + ReservedTopHeight));
	ImGui::SetNextWindowSize(ImVec2(
		MainViewport->Size.x,
		std::max(1.0f, MainViewport->Size.y - ReservedTopHeight - ReservedBottomHeight)));
	ImGui::SetNextWindowViewport(MainViewport->ID);

	constexpr ImGuiWindowFlags DockspaceWindowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("##EditorDockSpaceHost", nullptr, DockspaceWindowFlags);
	ImGui::DockSpace(DockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::End();
	ImGui::PopStyleVar(3);

	EnsureDefaultDockLayout(DockspaceId);
	DockPendingSkeletalMeshViewers();

	RenderViewportHostWindow();

	float ConsoleTakeoverHeight = 0.0f;
	if (ContentDrawerWidget.ConsumeOpenRequest())
	{
		ConsoleWidget.SetOpen(false);
		bShowConsole = false;
	}

	if (ContentDrawerWidget.ConsumeConsoleTakeover(ConsoleTakeoverHeight))
	{
		bShowConsole = true;
		ConsoleWidget.OpenFromDrawerTakeover(ConsoleTakeoverHeight);
	}

	ConsoleWidget.SetOpen(bShowConsole);
	ConsoleWidget.Render(DeltaTime);
	if (ConsoleWidget.ConsumeOpenRequest())
	{
		ContentDrawerWidget.SetOpen(false);
	}
	bShowConsole = ConsoleWidget.IsOpen();
	auto RenderWindowMenuPanel = [DeltaTime](bool& bShowPanel, auto& Widget)
	{
		if (!bShowPanel)
		{
			return;
		}

		Widget.SetOpen(true);
		Widget.Render(DeltaTime);
		bShowPanel = Widget.IsOpen();
	};

	RenderWindowMenuPanel(bShowMaterialEditor, MaterialWidget);
	RenderWindowMenuPanel(bShowProperty, PropertyWidget);
	RenderWindowMenuPanel(bShowSceneManager, SceneWidget);
	RenderWindowMenuPanel(bShowStatProfiler, StatWidget);
	RenderWindowMenuPanel(bShowCameraShake, CameraShakeWidget);
	RenderEditorDebugPanel();
	FEditorSkeletalMeshViewerWidget* FocusedViewerThisFrame = nullptr;
	bool bFocusedViewerStillOpen = false;
	for (auto It = SkeletalMeshViewers.begin(); It != SkeletalMeshViewers.end(); )
	{
		(*It)->Render(DeltaTime);

		if (!(*It)->IsOpen())
		{
			if (FocusedSkeletalMeshViewer == It->get())
			{
				FocusedSkeletalMeshViewer = nullptr;
			}
			It = SkeletalMeshViewers.erase(It);
		}
		else
		{
			if ((*It)->IsWindowFocused())
			{
				FocusedViewerThisFrame = It->get();
			}
			if (FocusedSkeletalMeshViewer == It->get())
			{
				bFocusedViewerStillOpen = true;
			}
			++It;
		}
	}
	if (FocusedViewerThisFrame)
	{
		FocusedSkeletalMeshViewer = FocusedViewerThisFrame;
	}
	else if (!bFocusedViewerStillOpen)
	{
		FocusedSkeletalMeshViewer = SkeletalMeshViewers.empty() ? nullptr : SkeletalMeshViewers.back().get();
	}
	ViewportOverlayWidget.Render(DeltaTime);
	ContentDrawerWidget.Render(DeltaTime);
	if (ContentDrawerWidget.ConsumeConsoleTakeover(ConsoleTakeoverHeight))
	{
		bShowConsole = true;
		ConsoleWidget.OpenFromDrawerTakeover(ConsoleTakeoverHeight);
	}
	if (ContentDrawerWidget.ConsumeOpenRequest())
	{
		ConsoleWidget.SetOpen(false);
		bShowConsole = false;
	}
	bShowConsole = ConsoleWidget.IsOpen();

	// 게임 UI는 PIE 중에만 표시합니다. 편집 중에는 씬 작업을 방해하지 않습니다.
	if (EditorEngine && EditorEngine->GetEditorState() == EViewportPlayState::Playing)
	{
		GameUISystem::Get().RenderPanelsOnly(EUIRenderMode::Play);
	}

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void FEditorMainPanel::RenderEditorDebugPanel()
{
	if (!bShowEditorDebug || !EditorEngine)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 320.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Editor Debug", &bShowEditorDebug))
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
			UWorld* World = EditorEngine->GetFocusedWorld();
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
					FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
					FEditorViewportClient* ActiveViewport = Layout.GetViewportClient(Layout.GetLastFocusedViewportIndex());
					if (ActiveViewport)
					{
						if (FViewportCamera* ActiveCamera = ActiveViewport->GetCamera())
						{
							FVector CameraForward = ActiveCamera->GetEffectiveForward();
							CameraForward.Z = 0.0f;
							if (!CameraForward.IsNearlyZero())
							{
								CameraForward.Normalize();
								GridForward = CameraForward;
								GridRight = FVector(-CameraForward.Y, CameraForward.X, 0.0f);
							}

							FVector SpawnForward = ActiveCamera->GetEffectiveForward();
							if (SpawnForward.IsNearlyZero())
							{
								SpawnForward = ActiveCamera->GetForwardVector();
							}
							SpawnForward.NormalizeSafe();
							GridOrigin = ActiveCamera->GetLocation() + SpawnForward * DebugCameraForwardDistance;
						}
					}
				}

				const float RowOffset = bDebugGridCenter ? (static_cast<float>(DebugGridRows - 1) * 0.5f) : 0.0f;
				const float ColOffset = bDebugGridCenter ? (static_cast<float>(DebugGridCols - 1) * 0.5f) : 0.0f;
				const float LayerOffset = bDebugGridCenter ? (static_cast<float>(DebugGridLayers - 1) * 0.5f) : 0.0f;

				std::mt19937 RNG{ std::random_device{}() };
				std::uniform_real_distribution<float> YawDist(-DebugRandomYawRange, DebugRandomYawRange);
				std::uniform_real_distribution<float> JitterXYDist(-DebugJitterXY, DebugJitterXY);
				std::uniform_real_distribution<float> JitterZDist(-DebugJitterZ, DebugJitterZ);

				std::vector<AActor*> SpawnedActors;
				SpawnedActors.reserve(static_cast<size_t>(TotalSpawnCount));
				int32 SpawnedCount = 0;
				const FDebugPlaceActorOption& Option = GDebugPlaceActorOptions[DebugPlaceActorTypeIndex];
				FSelectionManager& SelectionManager = EditorEngine->GetSelectionManager();

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

							AActor* SpawnedActor = SpawnDebugPlaceActor(World, Option.Type, SpawnLocation);
							if (!SpawnedActor)
							{
								continue;
							}

							if (bDebugRandomYaw)
							{
								SpawnedActor->SetActorRotation(FVector(0.0f, YawDist(RNG), 0.0f));
							}

							SelectionManager.Select(SpawnedActor);
							SpawnedActors.push_back(SpawnedActor);
							++SpawnedCount;
						}
					}
				}

				World->SyncSpatialIndex();
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

void FEditorMainPanel::ProcessPendingDebugActions()
{
	if (!bPendingClearLastBatch || !EditorEngine)
	{
		return;
	}
	bPendingClearLastBatch = false;

	UWorld* World = EditorEngine->GetFocusedWorld();
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
		if (!Actor || Actor->GetFocusedWorld() != World)
		{
			continue;
		}

		World->DestroyActor(Actor);
		++DestroyedCount;
	}

	DebugLastSpawnedActors.clear();
	World->SyncSpatialIndex();
	FEditorConsoleWidget::AddLog("Grid cleared: %d actors\n", DestroyedCount);
}

void FEditorMainPanel::Update()
{
	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigWindowsMoveFromTitleBarOnly = true;

	bool bViewportOperationActive = false;
	if (EditorEngine)
	{
		FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
		for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
		{
			if (Layout.GetViewportClient(i)->IsActiveOperation())
			{
				bViewportOperationActive = true;
				break;
			}
		}
	}

	const bool bPropertyModalBlockingInput = PropertyWidget.IsModalInputBlocking();

	bool bSkeletalMeshViewerBlockingInput = false;
	bool bSkeletalMeshViewerCapturedInput = false;

	for (const auto& Viewer : SkeletalMeshViewers)
	{
		if (Viewer->IsViewportInputActive()) bSkeletalMeshViewerBlockingInput = true;
		if (Viewer->IsViewportInputCaptured()) bSkeletalMeshViewerCapturedInput = true;
	}

	if (bViewportOperationActive || bSkeletalMeshViewerCapturedInput)
	{
		IO.ConfigFlags |= ImGuiConfigFlags_NoMouse;
	}
	else
	{
		IO.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
	}

	FGuiInputState& GuiState = FInputRouter::GetGuiInputState();
	GuiState.bBlockViewportInput = bPropertyModalBlockingInput || bSkeletalMeshViewerBlockingInput;
	GuiState.bUsingMouse =
		bPropertyModalBlockingInput || bSkeletalMeshViewerBlockingInput || (bViewportOperationActive ? false : IO.WantCaptureMouse);
	GuiState.bUsingKeyboard = bPropertyModalBlockingInput || bSkeletalMeshViewerBlockingInput || IO.WantCaptureKeyboard;

	// IME는 ImGui가 텍스트 입력을 원할 때만 활성화.
	// 그 외에는 OS 수준에서 IME 컨텍스트를 NULL로 연결해 한글 조합이
	// 뷰포트에 남는 현상을 원천 차단한다.
	if (Window)
	{
		HWND hWnd = Window->GetHWND();
		if (IO.WantTextInput)
		{
			// InputText 포커스 중 — 기본 IME 컨텍스트 복원
			ImmAssociateContextEx(hWnd, static_cast<HIMC>(INVALID_HANDLE_VALUE), IACE_DEFAULT);
		}
		else
		{
			// InputText 포커스 없음 — IME 컨텍스트 해제 (조합 불가)
			ImmAssociateContext(hWnd, NULL);
		}
	}
}

bool FEditorMainPanel::ShouldResetDefaultDockLayout(ImGuiID DockspaceId) const
{
	ImGuiDockNode* RootNode = ImGui::DockBuilderGetNode(DockspaceId);
	if (!RootNode)
		return true;

	// A leaf dockspace means ImGui could not restore a useful editor split layout.
	// This happens with a fresh or corrupted imgui_editor.ini.
	if (RootNode->IsLeafNode())
		return true;

	const ImGuiID LegacyConsoleId = ImHashStr("Console");
	if (const ImGuiWindowSettings* ConsoleSettings = ImGui::FindWindowSettingsByID(LegacyConsoleId))
	{
		return ConsoleSettings->DockId != 0;
	}

	return false;
}

void FEditorMainPanel::EnsureDefaultDockLayout(ImGuiID DockspaceId)
{
	if (bDefaultDockLayoutChecked)
		return;

	bDefaultDockLayoutChecked = true;
	if (!ShouldResetDefaultDockLayout(DockspaceId))
		return;

	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	const float ReservedTopHeight = ToolbarWidget.GetReservedTopHeight();
	const float ReservedBottomHeight = std::max(
		ContentDrawerWidget.GetReservedBottomHeight(),
		ConsoleWidget.GetReservedBottomHeight());
	ImGui::DockBuilderRemoveNode(DockspaceId);
	if (ImGuiWindowSettings* ConsoleSettings = ImGui::FindWindowSettingsByID(ImHashStr("Console")))
	{
		ConsoleSettings->DockId = 0;
	}
	ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodePos(DockspaceId, ImVec2(Viewport->Pos.x, Viewport->Pos.y + ReservedTopHeight));
	ImGui::DockBuilderSetNodeSize(
		DockspaceId,
		ImVec2(Viewport->Size.x, std::max(1.0f, Viewport->Size.y - ReservedTopHeight - ReservedBottomHeight)));

	ImGuiID MainNode = DockspaceId;
	ImGuiID RightNode = 0;
	ImGuiID RightTopNode = 0;
	ImGuiID RightBottomNode = 0;

	ImGui::DockBuilderSplitNode(MainNode, ImGuiDir_Right, 0.185f, &RightNode, &MainNode);
	ImGui::DockBuilderSplitNode(RightNode, ImGuiDir_Up, 0.22f, &RightTopNode, &RightBottomNode);

	ImGui::DockBuilderDockWindow("Viewport", MainNode);
	ImGui::DockBuilderDockWindow("Outliner", RightTopNode);
	ImGui::DockBuilderDockWindow("Stat Profiler", RightTopNode);
	ImGui::DockBuilderDockWindow("Details", RightBottomNode);
	ImGui::DockBuilderDockWindow("Material Editor", RightBottomNode);
	ImGui::DockBuilderDockWindow("ObjViewer Panel", RightBottomNode);
	ImGui::DockBuilderDockWindow("SkeletalMesh Viewer", RightBottomNode);

	ImGui::DockBuilderFinish(DockspaceId);
}

void FEditorMainPanel::OpenSkeletalMeshViewer(const FString& MeshPath)
{
	auto NewViewer = std::make_shared<FEditorSkeletalMeshViewerWidget>();
	NewViewer->SetInstanceId(NextViewerInstanceId++);
	NewViewer->Initialize(EditorEngine);
	NewViewer->OpenMesh(MeshPath);
	NewViewer->RequestFocus();

	PendingSkeletalMeshViewerDockWindows.push_back(NewViewer->GetWindowName());
	SkeletalMeshViewers.push_back(NewViewer);
}

void FEditorMainPanel::DockPendingSkeletalMeshViewers()
{
	if (PendingSkeletalMeshViewerDockWindows.empty())
	{
		return;
	}

	ImGuiID TargetDockNodeId = 0;
	if (const ImGuiWindowSettings* ViewportSettings = ImGui::FindWindowSettingsByID(ImHashStr("Viewport")))
	{
		TargetDockNodeId = ViewportSettings->DockId;
	}
	if (TargetDockNodeId == 0)
	{
		return;
	}

	for (const FString& WindowName : PendingSkeletalMeshViewerDockWindows)
	{
		ImGui::DockBuilderDockWindow(WindowName.c_str(), TargetDockNodeId);
	}
	PendingSkeletalMeshViewerDockWindows.clear();
}

// ImGui로 Viewport 가 차지할 영역을 계산하고 만든다.
void FEditorMainPanel::RenderViewportHostWindow()
{
	if (!EditorEngine)
		return;
	constexpr ImGuiWindowFlags WindowFlags = 0;
	FGuiInputState& GuiState = FInputRouter::GetGuiInputState();

	if (!ImGui::Begin("Viewport", nullptr, WindowFlags))
	{
		GuiState.bViewportHostVisible = false;
		GuiState.ViewportHostRect = FViewportRect();
		EditorEngine->GetViewportLayout().SetHostRect(FViewportRect());
		ImGui::End();
		return;
	}

	const ImVec2 ContentSize = ImGui::GetContentRegionAvail();
	if (ContentSize.x > 1.0f && ContentSize.y > 1.0f)
	{
		const ImVec2 ContentPos = ImGui::GetCursorScreenPos();
		const FViewportRect HostRect(
			static_cast<int32>(ContentPos.x),
			static_cast<int32>(ContentPos.y),
			static_cast<int32>(ContentSize.x),
			static_cast<int32>(ContentSize.y));

		GuiState.bViewportHostVisible = true;
		GuiState.ViewportHostRect = HostRect;
		EditorEngine->GetViewportLayout().SetHostRect(HostRect);
		
		uint32 ViewportNum = EditorEngine->GetViewportLayout().IsSingleViewportMode() ? 1 : 4;

		for (uint32 i = 0; i < ViewportNum; i++)
		{
			auto& VP = EditorEngine->GetViewportLayout().GetSceneViewport(i);

			const ID3D11ShaderResourceView* SceneColorSRV = VP.GetOutSRV();

			ImVec2 Size = ImVec2(
				static_cast<float>(VP.GetRect().Width),
				static_cast<float>(VP.GetRect().Height));

			if (SceneColorSRV)
			{
				ID3D11DeviceContext* DeviceContext = EditorEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
				ImDrawList* DrawList = ImGui::GetWindowDrawList();

				DrawList->AddCallback(SetOpaqueBlendStateCallback, DeviceContext);
				ImGui::Image(reinterpret_cast<ImTextureID>(SceneColorSRV), Size);
				DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
			}
			else
			{
				ImGui::Dummy(Size);
			}

			// 2x2 배치
			if (i % 2 == 0)
				ImGui::SameLine();
		}

		// 뷰포트별 독립 메뉴바 오버레이
		{
			FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
			const float MenuBarH = ImGui::GetFrameHeight();

			for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
			{
				FViewportRect ViewportRect = Layout.GetSceneViewport(i).GetRect();
				if (ViewportRect.Width <= 0 || ViewportRect.Height <= 0)
					continue;

				const float LocalX = static_cast<float>(ViewportRect.X - HostRect.X);
				const float LocalY = static_cast<float>(ViewportRect.Y - HostRect.Y);
				if (LocalX < 0.0f || LocalY < 0.0f)
					continue;

				ImGui::SetCursorScreenPos(ImVec2(ContentPos.x + LocalX, ContentPos.y + LocalY));

				char ChildID[32];
				snprintf(ChildID, sizeof(ChildID), "##VPMenu%d", i);

				ImGui::PushStyleColor(ImGuiCol_ChildBg, ColorFromHex(0x151515, 0.75f));
				constexpr ImGuiWindowFlags OverlayFlags =
					ImGuiWindowFlags_MenuBar |
					ImGuiWindowFlags_NoScrollbar |
					ImGuiWindowFlags_NoScrollWithMouse |
					ImGuiWindowFlags_NoNav |
					ImGuiWindowFlags_NoFocusOnAppearing;

				if (ImGui::BeginChild(ChildID, ImVec2(static_cast<float>(ViewportRect.Width), MenuBarH), false, OverlayFlags))
				{
					if (ImGui::BeginMenuBar())
					{
						RenderViewportMenuBarForIndex(i);
						ImGui::EndMenuBar();
					}
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
			}
		}
	}
	else
	{
		GuiState.bViewportHostVisible = false;
		GuiState.ViewportHostRect = FViewportRect();
		EditorEngine->GetViewportLayout().SetHostRect(FViewportRect());
	}

	ImGui::End();
}

// 개별 뷰포트 메뉴바 렌더링 — Index 번 뷰포트에 대한 Layout / Type / View / Stats 메뉴
void FEditorMainPanel::RenderViewportMenuBarForIndex(int32 Index)
{
	FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
	FEditorViewportClient* Client = Layout.GetViewportClient(Index);
	FEditorViewportState& State = Layout.GetViewportState(Index);

	const char* SlotName = GetViewportSlotName(Index);
	const char* ViewportTypeName = GetViewportTypeName(Client->GetViewportType());
	const char* ViewModeName = GetViewModeName(State.ViewMode);
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float RightGroupWidth =
		ImGui::CalcTextSize(SlotName).x +
		ImGui::CalcTextSize(" | ").x +
		ImGui::CalcTextSize(ViewportTypeName).x +
		ImGui::CalcTextSize(" | ").x +
		ImGui::CalcTextSize(ViewModeName).x +
		ImGui::CalcTextSize("Layout").x +
		ImGui::CalcTextSize("Type").x +
		ImGui::CalcTextSize("View").x +
		ImGui::CalcTextSize("Stats").x +
		Style.ItemSpacing.x * 8.0f +
		Style.FramePadding.x * 8.0f;
	const float RightAlignedX = ImGui::GetWindowContentRegionMax().x - RightGroupWidth;
	ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), RightAlignedX));

	ImGui::TextDisabled("%s | %s | %s", SlotName, ViewportTypeName, ViewModeName);
	ImGui::SameLine();

	if (ImGui::BeginMenu("Layout"))
	{
		const bool bSingle = Layout.IsSingleViewportMode();

		if (ImGui::MenuItem("SingleView", nullptr, bSingle))
			Layout.SetSingleViewportMode(true, Index);
		if (ImGui::MenuItem("Quad View", nullptr, !bSingle))
			Layout.SetSingleViewportMode(false);

		if (bSingle)
		{
			ImGui::Separator();
			for (int32 j = 0; j < FEditorViewportLayout::MaxViewports; ++j)
			{
				const bool bSel = (Layout.GetSingleViewportIndex() == j);
				if (ImGui::MenuItem(GetViewportSlotName(j), nullptr, bSel))
				{
					Layout.SetSingleViewportMode(true, j);
					Layout.SetLastFocusedViewportIndex(j);
				}
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Type"))
	{
		const bool bPerspectiveSelected = (Client->GetViewportType() == EVT_Perspective);
		if (ImGui::MenuItem("Perspective", nullptr, bPerspectiveSelected))
		{
			Client->SetViewportType(EVT_Perspective);
			Client->ApplyCameraMode();
		}

		const bool bOrthographicSelected = (Client->GetViewportType() == EVT_Orthographic);
		if (ImGui::MenuItem("Orthographic", nullptr, bOrthographicSelected))
		{
			Client->SetViewportType(EVT_Orthographic);
			Client->ApplyCameraMode();
		}

		ImGui::Separator();

		static constexpr EEditorViewportType kOrthoTypes[] = {
			EVT_OrthoTop, EVT_OrthoBottom,
			EVT_OrthoFront, EVT_OrthoBack,
			EVT_OrthoLeft, EVT_OrthoRight
		};
		for (EEditorViewportType Type : kOrthoTypes)
		{
			const bool bSel = (Client->GetViewportType() == Type);
			if (ImGui::MenuItem(GetViewportTypeName(Type), nullptr, bSel))
			{
				Client->SetViewportType(Type);
				Client->ApplyCameraMode();
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("View"))
	{
		// View 메뉴 노출 순서는 일반 렌더 모드와 디버그 모드를 구분해서 관리한다.
		// 새 view mode를 에디터에서 선택 가능하게 하려면 여기와 GetViewModeName을 함께 확장한다.
		static constexpr EViewMode MainModes[] = {
			EViewMode::Lit,
			EViewMode::Unlit,
			EViewMode::Wireframe,
			EViewMode::SceneDepth,
			EViewMode::WorldNormal,
		};
		static constexpr const char* MainLabels[] = {
			"Lit",
			"Unlit",
			"Wireframe",
			"Scene Depth",
			"World Normal",
		};
		static constexpr EViewMode DebugModes[] = {
			EViewMode::CascadeShadow,
			EViewMode::DebugCollision,
		};
		static constexpr const char* DebugLabels[] = {
			"Cascade Shadow",
			"Collision",
		};

		static_assert(IM_ARRAYSIZE(MainModes) == IM_ARRAYSIZE(MainLabels));
		for (int32 j = 0; j < static_cast<int32>(IM_ARRAYSIZE(MainModes)); ++j)
		{
			const bool bSel = (State.ViewMode == MainModes[j]);
			if (ImGui::MenuItem(MainLabels[j], nullptr, bSel))
				State.ViewMode = MainModes[j];
		}

		ImGui::Separator();

		static_assert(IM_ARRAYSIZE(DebugModes) == IM_ARRAYSIZE(DebugLabels));
		for (int32 j = 0; j < static_cast<int32>(IM_ARRAYSIZE(DebugModes)); ++j)
		{
			const bool bSel = (State.ViewMode == DebugModes[j]);
			if (ImGui::MenuItem(DebugLabels[j], nullptr, bSel))
				State.ViewMode = DebugModes[j];
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Stats"))
	{
		if (ImGui::MenuItem("FPS", nullptr, &State.bShowStatFPS))
			State.UpdateStatOrder(EStatType::FPS, State.bShowStatFPS);

		if (ImGui::MenuItem("Memory", nullptr, &State.bShowStatMemory))
			State.UpdateStatOrder(EStatType::Memory, State.bShowStatMemory);

		if (ImGui::MenuItem("Nametable", nullptr, &State.bShowStatNameTable))
			State.UpdateStatOrder(EStatType::NameTable, State.bShowStatNameTable);

		if (ImGui::MenuItem("Lightcull", nullptr, &State.bShowStatLightCull))
			State.UpdateStatOrder(EStatType::LightCull, State.bShowStatLightCull);

		if (ImGui::MenuItem("Shadow", nullptr, &State.bShowStatShadow))
			State.UpdateStatOrder(EStatType::Shadow, State.bShowStatShadow);

		ImGui::Separator();
		
		if (ImGui::MenuItem("Shadow Atlas", nullptr, &State.bShowStatShadowAtlas))
			State.UpdateStatOrder(EStatType::ShadowAtlas, State.bShowStatShadowAtlas);

		ImGui::EndMenu();
	}
}
