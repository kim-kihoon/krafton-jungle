

#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/Viewport/ViewportLayout.h"
#include "Engine/Runtime/WindowsWindow.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include "Render/Renderer/Renderer.h"
#include "Engine/Input/InputRouter.h"
#include "Game/UI/GameUISystem.h"

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

const char* GetViewportTypeName(EEditorViewportType Type)
{
	switch (Type)
	{
	case EVT_Perspective:
		return "Perspective";
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
} // namespace
void FEditorMainPanel::Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	IO.IniFilename = "imgui_editor.ini";

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
	MaterialWidget.Initialize(InEditorEngine);
	PropertyWidget.Initialize(InEditorEngine);
	SceneWidget.Initialize(InEditorEngine);
	ViewportOverlayWidget.Initialize(InEditorEngine);
	StatWidget.Initialize(InEditorEngine);
	PlayStreamWidget.Initialize(InEditorEngine);
	ToolbarWidget.Initialize(InEditorEngine);
	ToolbarWidget.SetViewportOverlayWidget(&ViewportOverlayWidget);
	ToolbarWidget.SetSceneWidget(&SceneWidget);
	ToolbarWidget.SetPlayStreamWidget(&PlayStreamWidget);
	ToolbarWidget.SetPanelVisibilityRefs(&bShowConsole, &bShowControl, &bShowProperty, &bShowSceneManager,
										 &bShowMaterialEditor, &bShowStatProfiler);
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

	const ImGuiID DockspaceId = ImGui::GetID("EditorDockSpace");
	ImGui::DockSpaceOverViewport(DockspaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
	EnsureDefaultDockLayout(DockspaceId);

	ToolbarWidget.Render(DeltaTime);

	RenderViewportHostWindow();

	if (bShowConsole)
		ConsoleWidget.Render(DeltaTime);
	if (bShowControl)
		ControlWidget.Render(DeltaTime);
	if (bShowMaterialEditor)
		MaterialWidget.Render(DeltaTime);
	if (bShowProperty)
		PropertyWidget.Render(DeltaTime);
	if (bShowSceneManager)
		SceneWidget.Render(DeltaTime);
	if (bShowStatProfiler)
		StatWidget.Render(DeltaTime);
	ViewportOverlayWidget.Render(DeltaTime);

	// 게임 UI는 PIE 중에만 표시합니다. 편집 중에는 씬 작업을 방해하지 않습니다.
	if (EditorEngine && EditorEngine->GetEditorState() == EViewportPlayState::Playing)
	{
		GameUISystem::Get().RenderPanelsOnly(EUIRenderMode::Play);
	}

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void FEditorMainPanel::Update()
{
	ImGuiIO& IO = ImGui::GetIO();

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

	if (bViewportOperationActive)
	{
		IO.ConfigFlags |= ImGuiConfigFlags_NoMouse;
	}
	else
	{
		IO.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
	}

	const bool bPropertyModalBlockingInput = PropertyWidget.IsModalInputBlocking();
	FGuiInputState& GuiState = FInputRouter::GetGuiInputState();
	GuiState.bBlockViewportInput = bPropertyModalBlockingInput;
	GuiState.bUsingMouse = bPropertyModalBlockingInput || (bViewportOperationActive ? false : IO.WantCaptureMouse);
	GuiState.bUsingKeyboard = bPropertyModalBlockingInput || IO.WantCaptureKeyboard;

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
	return RootNode->IsLeafNode();
}

void FEditorMainPanel::EnsureDefaultDockLayout(ImGuiID DockspaceId)
{
	if (bDefaultDockLayoutChecked)
		return;

	bDefaultDockLayoutChecked = true;
	if (!ShouldResetDefaultDockLayout(DockspaceId))
		return;

	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	ImGui::DockBuilderRemoveNode(DockspaceId);
	ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodePos(DockspaceId, Viewport->WorkPos);
	ImGui::DockBuilderSetNodeSize(DockspaceId, Viewport->WorkSize);

	ImGuiID MainNode = DockspaceId;
	ImGuiID RightNode = 0;
	ImGuiID BottomNode = 0;
	ImGuiID LeftNode = 0;
	ImGuiID CenterAndControlNode = 0;
	ImGuiID CenterNode = 0;
	ImGuiID ControlNode = 0;
	ImGuiID RightTopNode = 0;
	ImGuiID RightBottomNode = 0;

	ImGui::DockBuilderSplitNode(MainNode, ImGuiDir_Right, 0.185f, &RightNode, &MainNode);
	ImGui::DockBuilderSplitNode(MainNode, ImGuiDir_Down, 0.22f, &BottomNode, &MainNode);
	ImGui::DockBuilderSplitNode(MainNode, ImGuiDir_Left, 0.17f, &LeftNode, &CenterAndControlNode);
	ImGui::DockBuilderSplitNode(CenterAndControlNode, ImGuiDir_Right, 0.20f, &ControlNode, &CenterNode);
	ImGui::DockBuilderSplitNode(RightNode, ImGuiDir_Up, 0.22f, &RightTopNode, &RightBottomNode);

	ImGui::DockBuilderDockWindow("Viewport Settings", LeftNode);
	ImGui::DockBuilderDockWindow("Viewport", CenterNode);
	ImGui::DockBuilderDockWindow("Jungle Control Panel", ControlNode);
	ImGui::DockBuilderDockWindow("Console", BottomNode);
	ImGui::DockBuilderDockWindow("Scene Manager", RightTopNode);
	ImGui::DockBuilderDockWindow("Stat Profiler", RightTopNode);
	ImGui::DockBuilderDockWindow("Jungle Property Window", RightBottomNode);
	ImGui::DockBuilderDockWindow("Material Editor", RightBottomNode);
	ImGui::DockBuilderDockWindow("ObjViewer Panel", RightBottomNode);

	ImGui::DockBuilderFinish(DockspaceId);
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

				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.05f, 0.75f));
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

	ImGui::TextDisabled("%s | %s | %s",
						GetViewportSlotName(Index),
						GetViewportTypeName(Client->GetViewportType()),
						GetViewModeName(State.ViewMode));
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
		if (Index == 0)
		{
			ImGui::TextDisabled("Viewport 0 is fixed to Perspective.");
			ImGui::Separator();
			ImGui::MenuItem("Perspective", nullptr, true, false);
		}
		else
		{
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
