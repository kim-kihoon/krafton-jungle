#include "Editor/UI/EditorViewportOverlayWidget.h"

#include "Core/ResourceManager.h"

#include "Editor/EditorEngine.h"
#include "Editor/EditorRenderPipeline.h"
#include "Editor/Settings/EditorSettings.h"

#include "Engine/Slate/SlateApplication.h"
#include "Engine/Object/ObjectIterator.h"
#include "Engine/Asset/StaticMesh.h"
#include "Engine/Asset/StaticMeshTypes.h"
#include "Engine/Component/GizmoComponent.h"
#include "Engine/Object/FName.h"
#include "Engine/Render/Renderer/RenderFlow/LightCullingPass.h"
#include "Engine/Render/Renderer/RenderFlow/ShadowPass.h"
#include "Engine/Render/Renderer/RenderFlow/ShadowAtlasManager.h"
#include "GameFramework/PrimitiveActors.h"

#include "Slate/SSplitterV.h"
#include "Slate/SSplitterH.h"
#include "Slate/SSplitterCross.h"

#include "Viewport/ViewportLayout.h"

#include "Engine/Input/InputRouter.h"

#include "ImGui/imgui.h"
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <algorithm>

// ──────────── 공통 UI 상수 ────────────
namespace
{
	constexpr ImGuiWindowFlags kStatFlags =
		ImGuiWindowFlags_NoDecoration       |
		ImGuiWindowFlags_AlwaysAutoResize   |
		ImGuiWindowFlags_NoSavedSettings    |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav              |
		ImGuiWindowFlags_NoMove             |
		ImGuiWindowFlags_NoInputs;

	constexpr ImGuiWindowFlags kNameTableFlags =
		ImGuiWindowFlags_NoTitleBar         |
		ImGuiWindowFlags_NoResize           |
		ImGuiWindowFlags_NoScrollbar        |
		ImGuiWindowFlags_NoSavedSettings    |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav              |
		ImGuiWindowFlags_NoMove;

	constexpr ImGuiWindowFlags kInteractiveStatFlags =
		ImGuiWindowFlags_NoDecoration       |
		ImGuiWindowFlags_AlwaysAutoResize   |
		ImGuiWindowFlags_NoSavedSettings    |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav              |
		ImGuiWindowFlags_NoMove;

	const ImVec4 ColorWhite  = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	const ImVec4 ColorPaleBlue = ImVec4(0.8f, 0.8f, 1.0f, 1.0f);
	const ImVec4 ColorGreen  = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
	const ImVec4 ColorCyan   = ImVec4(0.25f, 0.9f, 1.0f, 1.0f);
	const ImVec4 ColorOrange = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
	const ImVec4 ColorYellow = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	const ImVec4 ColorPink   = ImVec4(1.0f, 0.5f, 0.5f, 1.0f);
	const ImVec4 ColorRed    = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
	const ImVec4 ColorPurple = ImVec4(0.7f, 0.4f, 1.0f, 1.0f);
	const ImVec4 ColorMint   = ImVec4(0.3f, 1.0f, 0.7f, 1.0f);

	const char* GetViewportTypeName(EEditorViewportType Type);
	const char* GetViewModeName(EViewMode Mode);
	void DrawAtlasGrid(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, uint32 GridDimension);
	const char* GetPointFaceLabel(uint32 FaceIndex);
	bool DrawRoundSelectButton(const char* Id, const char* Label, bool bSelected, const ImVec4& AccentColor);

	template <typename T>
	void SpawnActorAt(UWorld* World, FSelectionManager& SelectionManager, const FVector& Location)
	{
		if (!World)
			return;

		T* Actor = World->SpawnActor<T>();
		if (!Actor)
			return;

		Actor->InitDefaultComponents();
		Actor->SetActorLocation(Location);
		SelectionManager.Select(Actor);
	}

	struct FPlacementActorEntry
	{
		const char* Label;
		void (*Spawn)(UWorld*, FSelectionManager&, const FVector&);
	};

	// 꼭 모든 Actor Types가 저장될 필요는 없습니다. 우클릭으로 생성할 수 있는 액터만 관리합니다.
	static const FPlacementActorEntry PlacementActorTypes[] = {
		{ "Pawn", SpawnActorAt<APawnActor> },
		{ "Scene", SpawnActorAt<ASceneActor> },
		{ "StaticMesh", SpawnActorAt<AStaticMeshActor> },
		{ "TextRender", SpawnActorAt<ATextRenderActor> },
		{ "SubUV", SpawnActorAt<ASubUVActor> },
		{ "Billboard", SpawnActorAt<ABillboardActor> },
		{ "Decal", SpawnActorAt<ADecalActor> },
		{ "Directional Light", SpawnActorAt<ADirectionalLightActor> },
		{ "Ambient Light", SpawnActorAt<AAmbientLightActor> },
		{ "Point Light", SpawnActorAt<APointLightActor> },
		{ "Spot Light", SpawnActorAt<ASpotLightActor> },
		{ "Audio Zone", SpawnActorAt<AAudioZoneActor> },
		{ "Player Start", SpawnActorAt<APlayerStartActor> },
	};
}

// ──────────── FEditorViewPortOverlayWidget의 메인 렌더링 함수입니다. ────────────
void FEditorViewportOverlayWidget::Render(float DeltaTime)
{
	if (bShowViewportSettings)
	{
		RenderViewportSettings(DeltaTime);
	}
	RenderDebugStats(DeltaTime);
	RenderSplitterBar();
	RenderBoxSelectionOverlay();
	RenderActorPlacementPopup();
	RenderShortcutsWindow();
}

// 뷰포트 설정(표시 플래그, 그리드, 카메라 감도, BVH 관리 정책 등)을 조작하는 창을 렌더링합니다.
void FEditorViewportOverlayWidget::RenderViewportSettings(float DeltaTime)
{
	FEditorSettings& Settings = FEditorSettings::Get();

	if (!ImGui::Begin("Viewport Settings"))
	{
		ImGui::End();
		return;
	}

	// 위젯 너비를 현재 창 콘텐츠 영역의 50%로 설정하는 람다 또는 변수
	float ItemWidth = ImGui::GetContentRegionAvail().x * 0.5f;

	// Show Flags
	ImGui::Text("Show");
	ImGui::Checkbox("Primitives", &Settings.ShowFlags.bPrimitives);
	ImGui::Checkbox("BillboardText", &Settings.ShowFlags.bBillboardText);
	ImGui::Checkbox("Axis", &Settings.ShowFlags.bAxis);
	ImGui::Checkbox("Grid", &Settings.ShowFlags.bGrid);
	ImGui::Checkbox("Gizmo", &Settings.ShowFlags.bGizmo);
	ImGui::Checkbox("Bounding Volume", &Settings.ShowFlags.bBoundingVolume);
	if (Settings.ShowFlags.bBoundingVolume)
	{
		ImGui::Indent();
		ImGui::Checkbox("BVH Bounding Volume", &Settings.ShowFlags.bBVHBoundingVolume);
		ImGui::Unindent();
	}
	ImGui::Checkbox("Audio Range", &Settings.ShowFlags.bAudioRange);
	if (Settings.ShowFlags.bAudioRange)
	{
		ImGui::Indent();
		ImGui::Checkbox("Audio Component Range", &Settings.ShowFlags.bAudioComponentRange);
		ImGui::Checkbox("Audio Zone Range", &Settings.ShowFlags.bAudioZoneRange);
		ImGui::Unindent();
	}
	ImGui::Checkbox("LOD", &Settings.ShowFlags.bEnableLOD);
	ImGui::Checkbox("Decals", &Settings.ShowFlags.bDecals);
	ImGui::Checkbox("Fog", &Settings.ShowFlags.bFog);
	ImGui::Checkbox("Shadow", &Settings.ShowFlags.bShadow);

	ImGui::Separator();

	// Grid Settings
	ImGui::Text("Grid");
	ImGui::SetNextItemWidth(ItemWidth);
	ImGui::SliderFloat("Spacing", &Settings.GridSpacing, 0.1f, 10.0f, "%.1f");
	
	ImGui::SetNextItemWidth(ItemWidth);
	ImGui::SliderInt("Half Line Count", &Settings.GridHalfLineCount, 10, 500);

	ImGui::Separator();
	ImGui::Text("Post Process");
	ImGui::Checkbox("Enable FXAA", &Settings.bEnableFXAA);

	ImGui::Separator();

	// Camera Sensitivity
	ImGui::Text("Camera");

	ImGui::SetNextItemWidth(ItemWidth);
	ImGui::SliderFloat("Move Sensitivity", &Settings.CameraMoveSensitivity, 0.05f, 5.0f, "%.1f");
	
	ImGui::SetNextItemWidth(ItemWidth);
	ImGui::SliderFloat("Rotate Sensitivity", &Settings.CameraRotateSensitivity, 0.05f, 5.0f, "%.1f");

	if (EditorEngine)
	{
		FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
		const int32 FocusedIdx = Layout.GetLastFocusedViewportIndex();
		FEditorViewportClient* FocusedClient = Layout.GetViewportClient(FocusedIdx);

		ImGui::SetNextItemWidth(ItemWidth);
		ImGui::SliderFloat("Zoom Speed", &Settings.CameraZoomSpeed, 0.1f, 100.0f, "%.1f");
	}

	if (Settings.ShowFlags.bBoundingVolume && Settings.ShowFlags.bBVHBoundingVolume)
	{
		ImGui::Separator();
		ImGui::Text("BVH Maintenance");
		bool bPolicyChanged = false;

		ImGui::SetNextItemWidth(ItemWidth);
		bPolicyChanged |= ImGui::SliderInt("Batch Refit Min Dirty", &Settings.SpatialBatchRefitMinDirtyCount, 1, 256);

		ImGui::SetNextItemWidth(ItemWidth);
		bPolicyChanged |= ImGui::SliderInt("Batch Refit Dirty %", &Settings.SpatialBatchRefitDirtyPercentThreshold, 1, 100);

		ImGui::SetNextItemWidth(ItemWidth);
		bPolicyChanged |= ImGui::SliderInt("Rotation Structural Changes", &Settings.SpatialRotationStructuralChangeThreshold, 1, 256);

		ImGui::SetNextItemWidth(ItemWidth);
		bPolicyChanged |= ImGui::SliderInt("Rotation Dirty Count", &Settings.SpatialRotationDirtyCountThreshold, 1, 512);

		ImGui::SetNextItemWidth(ItemWidth);
		bPolicyChanged |= ImGui::SliderInt("Rotation Dirty %", &Settings.SpatialRotationDirtyPercentThreshold, 1, 100);

		if (bPolicyChanged && EditorEngine)
		{
			EditorEngine->ApplySpatialIndexMaintenanceSettings();
		}
	}

	ImGui::End();
}

// 활성화된 뷰포트를 순회하며 설정에 따라 디버그 스탯(FPS, Culling, Memory 등) 오버레이를 화면에 배치하고 렌더링합니다.
void FEditorViewportOverlayWidget::RenderDebugStats(float DeltaTime)
{
	if (!EditorEngine) return;

	FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();

	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		const FEditorViewportState& VS = Layout.GetViewportState(i);
		FViewportRect ViewportRect = Layout.GetSceneViewport(i).GetRect();

		if (!VS.bShowStatFPS &&
			!VS.bShowStatMemory &&
			!VS.bShowStatNameTable &&
			!VS.bShowStatLightCull &&
			!VS.bShowStatShadow &&
			!VS.bShowStatShadowAtlas)
			continue;
		
		if (ViewportRect.Width <= 0 || ViewportRect.Height <= 0) 
			continue;

		ImVec2 CurrentDrawPos(static_cast<float>(ViewportRect.X) + 8.f, static_cast<float>(ViewportRect.Y) + 32.f);

		float GeneralWidth = RenderGeneralStatsWindow(i, VS, CurrentDrawPos, DeltaTime);
		if (GeneralWidth > 0.f)
			CurrentDrawPos.x += GeneralWidth + 8.f;

		float NTWidth = RenderNameTableWindow(i, VS, CurrentDrawPos);
		if (NTWidth > 0.f)
			CurrentDrawPos.x += NTWidth + 8.f;

		float LightCullWidth = RenderLightCullWindow(i, VS, CurrentDrawPos);
		if (LightCullWidth > 0.f)
			CurrentDrawPos.x += LightCullWidth + 8.f;

		float ShadowWidth = RenderShadowWindow(i, VS, CurrentDrawPos);
		if (ShadowWidth > 0.f)
			CurrentDrawPos.x += ShadowWidth + 8.f;

		float ShadowAtlasWidth = RenderShadowAtlasWindow(i, VS, CurrentDrawPos);
		if (ShadowAtlasWidth > 0.f)
			CurrentDrawPos.x += ShadowAtlasWidth + 8.f;
		
		// [중요] 통계를 띄우고 싶을 경우엔 여기에 위의 양식과 똑같이 추가합니다.
	}
}

// 다중 뷰포트 모드에서 뷰포트 간의 경계선(Splitter) 및 교차점(Cross)을 드래그 시 강조해 렌더링합니다.
void FEditorViewportOverlayWidget::RenderSplitterBar()
{
	 // 뷰포트를 클릭했거나, 휠 드래그를 하고 있을 때 강조하지 않습니다.
	if (FSlateApplication::Get().GetCapturedWidget() || FInputRouter::GetMiddleDragging())
		 return;

	// 기즈모를 잡고 있을 때 강조하지 않습니다.
	bool bIsHodingGizmo = EditorEngine->GetGizmo()->IsHolding();
	if (bIsHodingGizmo || FInputRouter::GetRightDragging())
	{
		return;
	}

	if (!EditorEngine) return;
	
	FEditorViewportLayout& ViewportLayout = EditorEngine->GetViewportLayout();

	// 뷰포트가 1개일 때 Splitter Bar를 띄우지 않습니다.
	if (!ViewportLayout.IsSingleViewportMode())
	{
		ImDrawList* DrawList = ImGui::GetForegroundDrawList();
		constexpr ImU32 BarColor = IM_COL32(80, 80, 80, 220);
		constexpr ImU32 HoverColor = IM_COL32(140, 180, 255, 255);

		const SWidget* Hovered  = FSlateApplication::Get().GetHoveredWidget();
		const SWidget* Captured = FSlateApplication::Get().GetCapturedWidget();

		const bool bIsDragging = FInputRouter::GetRightDragging();

		SSplitterCross* Cross = ViewportLayout.GetCrossWidget();
		constexpr ImU32 CrossHoverColor = IM_COL32(140, 180, 255, 255);

		const bool bCrossHovered = (Cross && Cross == Hovered);

		SSplitter* Splitters[] = {
			ViewportLayout.GetRootSplitterV(),
			ViewportLayout.GetTopSplitterH(),
			ViewportLayout.GetBotSplitterH()
		};

		for (SSplitter* S : Splitters)
		{
			if (!S) continue;
			const FRect Bar = S->GetBarRect();

			const SSplitter* Linked = S->GetLinkedSplitter();
			const bool bSplitterHover = !bIsDragging
				&& ((S == Hovered || S == Captured)
					|| (Linked && (Linked == Hovered || Linked == Captured)));

			ImU32 Color = BarColor;
			if (bCrossHovered)       Color = CrossHoverColor;
			else if (bSplitterHover) Color = HoverColor;

			DrawList->AddRectFilled(
				ImVec2(Bar.X, Bar.Y),
				ImVec2(Bar.X + Bar.Width, Bar.Y + Bar.Height),
				Color);
		}

		if (Cross)
		{
			const FRect CR = Cross->GetCrossRect();
			DrawList->AddRectFilled(
				ImVec2(CR.X, CR.Y),
				ImVec2(CR.X + CR.Width, CR.Y + CR.Height),
				bCrossHovered ? CrossHoverColor : BarColor);
		}
	}
}

// 마우스 드래그를 통한 다중 선택(Box Selection) 시 뷰포트 위에 반투명한 선택 영역 박스를 렌더링합니다.
void FEditorViewportOverlayWidget::RenderBoxSelectionOverlay()
{
	if (!EditorEngine)
	{
		return;
	}

	FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
	ImDrawList* DrawList = ImGui::GetForegroundDrawList();
	const bool bAdditive = FInputRouter::GetKey(VK_SHIFT);
	const ImU32 RectColor = bAdditive ? IM_COL32(128, 240, 128, 220) : IM_COL32(128, 192, 255, 220);
	const ImU32 FillColor = bAdditive ? IM_COL32(64, 180, 64, 40) : IM_COL32(64, 128, 220, 40);

	for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
	{
		const FEditorViewportState& VS = Layout.GetViewportState(i);
		FViewportRect ViewportRect = Layout.GetSceneViewport(i).GetRect();
		if (ViewportRect.Width <= 0 || ViewportRect.Height <= 0)
		{
			continue;
		}

		const FEditorViewportClient* Client = Layout.GetViewportClient(i);
		if (!Client->IsBoxSelecting())
		{
			continue;
		}

		const POINT Start = Client->GetBoxSelectStart();
		const POINT End = Client->GetBoxSelectEnd();

		const float MinX = static_cast<float>(std::min(Start.x, End.x));
		const float MinY = static_cast<float>(std::min(Start.y, End.y));
		const float MaxX = static_cast<float>(std::max(Start.x, End.x));
		const float MaxY = static_cast<float>(std::max(Start.y, End.y));

		const ImVec2 P0(static_cast<float>(ViewportRect.X) + MinX, static_cast<float>(ViewportRect.Y) + MinY);
		const ImVec2 P1(static_cast<float>(ViewportRect.X) + MaxX, static_cast<float>(ViewportRect.Y) + MaxY);
		DrawList->AddRectFilled(P0, P1, FillColor);
		DrawList->AddRect(P0, P1, RectColor, 0.0f, 0, 1.5f);
	}
}

void FEditorViewportOverlayWidget::RenderActorPlacementPopup()
{
	if (!EditorEngine)
	{
		return;
	}

	FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
	FEditorViewportClient* Client = Layout.GetViewportClient(Layout.GetLastFocusedViewportIndex());
	if (!Client)
	{
		return;
	}

	if (Client->HasPendingActorPlacement() && !bActorPlacementPopupOpened)
	{
		const POINT PopupPos = Client->GetPendingActorPlacementPopupPos();
		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(PopupPos.x), static_cast<float>(PopupPos.y)), ImGuiCond_Always);
		ImGui::OpenPopup("Actor Placement##Popup");
		bActorPlacementPopupOpened = true;
	}

	if (ImGui::BeginPopup("Actor Placement##Popup"))
	{
		ImGui::TextColored(ColorMint, "Spawn Actor");
		ImGui::Separator();

		for (const FPlacementActorEntry& Entry : PlacementActorTypes)
		{
			if (ImGui::Selectable(Entry.Label))
			{
				Entry.Spawn(EditorEngine->GetFocusedWorld(), EditorEngine->GetSelectionManager(), Client->GetPendingActorPlacementLocation());
				Client->ClearPendingActorPlacement();
				bActorPlacementPopupOpened = false;
				ImGui::CloseCurrentPopup();
				break;
			}
		}

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			Client->ClearPendingActorPlacement();
			bActorPlacementPopupOpened = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
	else if (bActorPlacementPopupOpened)
	{
		Client->ClearPendingActorPlacement();
		bActorPlacementPopupOpened = false;
	}
}

// 현재 에디터에서 사용 가능한 단축키 목록을 보여주는 팝업 창을 렌더링합니다.
void FEditorViewportOverlayWidget::RenderShortcutsWindow()
{
	if (!bShowShortcutsWindow)
	{
		return;
	}

	ImGui::OpenPopup("Shortcuts##Modal");
	ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
	ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f), ImGuiCond_Appearing);

	if (!ImGui::BeginPopupModal("Shortcuts##Modal", &bShowShortcutsWindow, ImGuiWindowFlags_NoResize))
	{
		ImGui::PopStyleColor();
		return;
	}

	if (!bShowShortcutsWindow)
	{
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		ImGui::PopStyleColor();
		return;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
	{
		bShowShortcutsWindow = false;
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		ImGui::PopStyleColor();
		return;
	}

	ImGui::TextUnformatted("Shortcuts");

	ImGui::Separator();
	ImGui::Text("현재 코드상 실제로 동작하는 에디터 단축키만 정리했습니다.");

	auto DrawShortcutTable = [](const char* Header, std::initializer_list<std::pair<const char*, const char*>> Rows)
	{
		if (!ImGui::CollapsingHeader(Header, ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		if (ImGui::BeginTable(Header, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter))
		{
			ImGui::TableSetupColumn("Shortcut");
			ImGui::TableSetupColumn("Action");
			ImGui::TableHeadersRow();

			for (const auto& Row : Rows)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(Row.first);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(Row.second);
			}

			ImGui::EndTable();
		}
	};

	DrawShortcutTable("Viewport Navigation",
	{
		{"Mouse Right Drag", "뷰포트 카메라 회전"},
		{"Mouse Middle Drag", "뷰포트 카메라 팬 이동"},
		{"Alt + Mouse Right Drag", "카메라 돌리 인/아웃"},
		{"Mouse Wheel", "원근 카메라 FOV 또는 직교 카메라 높이 조절"},
		{"Mouse Wheel while rotating", "카메라 이동 속도 조절"},
		{"W / A / S / D / Q / E", "카메라 이동 (회전 중일 때만 적용)"},
		{"F", "현재 선택된 Actor 쪽으로 카메라 포커스"},
	});

	DrawShortcutTable("Selection",
	{
		{"Mouse Left Click", "Actor 단일 선택"},
		{"Shift + Mouse Left Click", "선택 추가"},
		{"Ctrl + Mouse Left Click", "선택 토글"},
		{"Ctrl + A", "전체 Actor 선택"},
		{"Ctrl + Alt + Drag", "박스 선택"},
		{"Ctrl + Alt + Shift + Drag", "기존 선택에 박스 선택 추가"},
	});

	DrawShortcutTable("Actor Placement",
	{
		{"Ctrl + Mouse Right Click", "클릭한 지점에 Ray Casting 후 Actor 생성 메뉴 열기"},
	});

	DrawShortcutTable("Gizmo",
	{
		{"Mouse Left Drag", "기즈모 축 드래그 조작"},
		{"Space", "기즈모 타입 순환"},
		{"X", "월드/로컬 기즈모 모드 전환"},
	});

	DrawShortcutTable("Editor",
	{
		{"Delete", "선택된 Actor 삭제"},
		{"F4", "PIE 중 마우스 캡처/커서 표시 토글"},
	});

	ImGui::Spacing();
	ImGui::TextUnformatted("참고: ImGui 입력창이 키보드를 잡고 있을 때는 일부 단축키가 동작하지 않습니다.");
	ImGui::EndPopup();
	ImGui::PopStyleColor();
}

// 특정 뷰포트의 일반적인 렌더링 통계(FPS, Culling, Decal, Memory) 정보를 출력하는 창을 그립니다.
float FEditorViewportOverlayWidget::RenderGeneralStatsWindow(int32 ViewportIndex, const FEditorViewportState& VS, const ImVec2& Pos, float DeltaTime)
{
	if (!VS.bShowStatFPS && !VS.bShowStatMemory) 
		return 0.f;

	const FEditorRenderPipeline* RenderPipeline = EditorEngine->GetEditorRenderPipeline();

	ImGui::SetNextWindowPos(Pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.3f);

	char WinId[32];
	snprintf(WinId, sizeof(WinId), "##StatOverlay_%d", ViewportIndex);

	float WindowWidth = 0.f;
	if (ImGui::Begin(WinId, nullptr, kStatFlags))
	{
		const FRenderCollector::FCullingStats* CullingStats = 
			(RenderPipeline != nullptr) ? &RenderPipeline->GetViewportCullingStats(ViewportIndex) : nullptr;

		if (VS.bShowStatFPS)
		{
			const float FPS = (DeltaTime > 0.f) ? (1.f / DeltaTime) : 0.f;
			ImGui::TextColored(ColorGreen, "FPS: %.1f (%.2f ms)", FPS, DeltaTime * 1000.f);
		}

		if (CullingStats != nullptr)
		{
			const int32 CulledPrimitiveCount = std::max(0, CullingStats->TotalVisiblePrimitiveCount - (CullingStats->BVHPassedPrimitiveCount + CullingStats->FallbackPassedPrimitiveCount));
			if (VS.bShowStatFPS) ImGui::Separator();

			ImGui::TextColored(ColorCyan, "Culling");
			ImGui::TextColored(ColorPaleBlue, "- Total Visible: %d", CullingStats->TotalVisiblePrimitiveCount);
			ImGui::TextColored(ColorPaleBlue, "- BVH Passed: %d", CullingStats->BVHPassedPrimitiveCount);
			ImGui::TextColored(ColorPaleBlue, "- Fallback Passed: %d", CullingStats->FallbackPassedPrimitiveCount);
			ImGui::TextColored(ColorPaleBlue, "- Culled: %d", CulledPrimitiveCount);
		}

		const FRenderCollector::FDecalStats* DecalStats = 
			(RenderPipeline != nullptr) ? &RenderPipeline->GetViewportDecalStats(ViewportIndex) : nullptr;
		
		if (DecalStats != nullptr)
		{
			if (CullingStats != nullptr || VS.bShowStatFPS) ImGui::Separator();

			ImGui::TextColored(ColorOrange, "Decal");
			ImGui::TextColored(ColorPaleBlue, "- Total Decals: %d", DecalStats->TotalDecalCount);
			ImGui::TextColored(ColorPaleBlue, "- Decal Time: %.4f ms", DecalStats->CollectTimeMS / 1000.f);
		}

		if (VS.bShowStatMemory)
		{
			if (CullingStats != nullptr || VS.bShowStatFPS) ImGui::Separator();

			size_t MeshMemoryBytes = 0;
			for (TObjectIterator<UStaticMesh> It; It; ++It)
			{
				UStaticMesh* Mesh = *It;
				if (Mesh && Mesh->HasValidMeshData())
				{
					MeshMemoryBytes += sizeof(UStaticMesh)
						+ Mesh->GetVertices().size()  * sizeof(FNormalVertex)
						+ Mesh->GetIndices().size()   * sizeof(uint32)
						+ Mesh->GetSections().size()  * sizeof(FStaticMeshSection);
				}
			}

			const size_t MatMemoryBytes = FResourceManager::Get().GetMaterialMemorySize();

			ImGui::TextColored(ColorYellow, "Memory Stat");
			ImGui::TextColored(ColorPaleBlue, "- Mesh: %.2f KB", MeshMemoryBytes / 1024.f);
			ImGui::TextColored(ColorPaleBlue, "- Material: %.2f KB", MatMemoryBytes / 1024.f);
			ImGui::Separator();

			FNamePool& Pool = FNamePool::Get();
			
			ImGui::TextColored(ColorPink, "FName Stat");
			ImGui::TextColored(ColorPaleBlue, "- Entries: %u", Pool.GetEntryCount());
			ImGui::TextColored(ColorPaleBlue, "- Size: %.2f KB", Pool.GetTotalBytes() / 1024.f);
			ImGui::Separator();

			ImGui::TextColored(ColorPaleBlue, "- Total Allocated Counts: %d", EngineStatics::GetTotalAllocationCount());
			ImGui::TextColored(ColorPaleBlue, "- Total Allocated Bytes: %.2f KB", EngineStatics::GetTotalAllocationBytes() / 1024.f);
		}

		WindowWidth = ImGui::GetWindowSize().x; 
	}
	ImGui::End();

	return WindowWidth;
}

// 현재 엔진의 FNamePool에 등록된 전체 문자열 목록을 스크롤 가능한 창으로 렌더링합니다.
float FEditorViewportOverlayWidget::RenderNameTableWindow(int32 ViewportIndex, const FEditorViewportState& VS, const ImVec2& Pos)
{
	if (!VS.bShowStatNameTable) 
		return 0.f;

	ImGui::SetNextWindowPos(Pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.3f);
	ImGui::SetNextWindowSize(ImVec2(280.f, 300.f), ImGuiCond_Always);

	char WinId[32];
	snprintf(WinId, sizeof(WinId), "##NameTableOverlay_%d", ViewportIndex);

	float WindowWidth = 0.f;
	if (ImGui::Begin(WinId, nullptr, kNameTableFlags))
	{
		FNamePool& Pool = FNamePool::Get();
		const uint32 Count = Pool.GetEntryCount();
		const TArray<FString>& Entries = Pool.GetEntries();

		ImGui::TextColored(ColorPurple, "FName Table (%u entries)", Count);
		ImGui::Separator();

		ImGui::BeginChild("##NTScroll", ImVec2(0.f, 0.f), false);
		ImGuiListClipper Clipper;
		Clipper.Begin(static_cast<int>(Count));
		while (Clipper.Step())
		{
			for (int j = Clipper.DisplayStart; j < Clipper.DisplayEnd; ++j)
			{
				ImGui::TextColored(ColorPaleBlue, "[%d] %s", j, Entries[static_cast<uint32>(j)].c_str());
			}
		}
		Clipper.End();
		ImGui::EndChild();
		WindowWidth = ImGui::GetWindowSize().x;
	}
	ImGui::End();

	return WindowWidth;
}

// 라이트 컬링(Light Culling) 패스의 디버그 통계(타일 수, 타일당 라이트 개수 등)를 출력하는 창을 그립니다.
float FEditorViewportOverlayWidget::RenderLightCullWindow(int32 ViewportIndex, const FEditorViewportState& VS, const ImVec2& Pos)
{
	if (!VS.bShowStatLightCull) 
		return 0.f;

	ImGui::SetNextWindowPos(Pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.3f);

	char WinId[32];
	snprintf(WinId, sizeof(WinId), "##LightCullOverlay_%d", ViewportIndex);

	float WindowWidth = 0.f;
	if (ImGui::Begin(WinId, nullptr, kStatFlags))
	{
		const FLightCullingDebugStats& S = FLightCullingPass::GetDebugStats();
		
		ImGui::TextColored(ColorMint, "Light Culling");
		ImGui::Separator();
		
		ImGui::TextColored(ColorPaleBlue, "- Lights: %u", S.LightCount);
		ImGui::TextColored(ColorPaleBlue, "- Tiles:  %ux%u (%u)", S.TileCountX, S.TileCountY, S.TileCount);
		ImGui::TextColored(ColorPaleBlue, "- Non-zero Tiles: %u", S.NonZeroTileCount);
		ImGui::TextColored(ColorPaleBlue, "- Max / Tile: %u", S.MaxLightsInTile);
		ImGui::TextColored(ColorPaleBlue, "- Avg / Tile: %.2f", S.AvgLightsPerTile);
		WindowWidth = ImGui::GetWindowSize().x;
	}
	ImGui::End();

	return WindowWidth;
}

float FEditorViewportOverlayWidget::RenderShadowWindow(int32 ViewportIndex, const FEditorViewportState& VS, const ImVec2& Pos)
{
	if (!VS.bShowStatShadow)
		return 0.f;

	const FEditorRenderPipeline* RenderPipeline = EditorEngine->GetEditorRenderPipeline();
	if (!RenderPipeline) return 0.f;

	const FRenderCollector::FShadowStats& ShadowStats = RenderPipeline->GetViewportShadowStats(ViewportIndex);
	const FDirectionalShadowConstants& SC = ShadowStats.DirectionalShadowConstants;

	ImGui::SetNextWindowPos(Pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.3f);

	char WinId[32];
	snprintf(WinId, sizeof(WinId), "##ShadowOverlay_%d", ViewportIndex);

	float WindowWidth = 0.0f;
	if (ImGui::Begin(WinId, nullptr, kStatFlags))
	{
		ImGui::TextColored(ColorPink, "Shadow Stat");
		ImGui::Separator();

		ImGui::TextColored(ColorOrange, "Light Counts");
		ImGui::TextColored(ColorPaleBlue, "- Directional: %u", ShadowStats.DirectionalLightCount);
		ImGui::TextColored(ColorPaleBlue, "- Point: %u", ShadowStats.PointLightCount);
		ImGui::TextColored(ColorPaleBlue, "- Spot: %u", ShadowStats.SpotLightCount);
		ImGui::TextColored(ColorPaleBlue, "- Ambient: %u", ShadowStats.AmbientLightCount);
		ImGui::Separator();

		ImGui::TextColored(ColorYellow, "Shadow Memory");
		ImGui::TextColored(ColorPaleBlue, "- Directional: %u shadow, %.2f MB", ShadowStats.DirectionalShadowCount, ShadowStats.DirectionalShadowMemoryBytes / (1024.f * 1024.f));
		ImGui::TextColored(ColorPaleBlue, "- Point: %u shadow, %.2f MB", ShadowStats.PointShadowCount, ShadowStats.PointShadowMemoryBytes / (1024.f * 1024.f));
		ImGui::TextColored(ColorPaleBlue, "- Spot: %u shadow, %.2f MB", ShadowStats.SpotShadowCount, ShadowStats.SpotShadowMemoryBytes / (1024.f * 1024.f));
		ImGui::TextColored(ColorPaleBlue, "- Total: %.2f MB", ShadowStats.GetTotalShadowMemoryBytes() / (1024.f * 1024.f));
		ImGui::Separator();

		const uint32 Res = FShadowPass::DirectionalShadowResolution;
		ImGui::TextColored(ColorOrange, "Cascades (Res: %u)", Res);
		for (int32 i = 0; i < MAX_CASCADE_COUNT; ++i)
		{
			const float Split = SC.SplitDistances.XYZW[i];
			const float Radius = SC.CascadeRadius.XYZW[i];
			const float TexelSize = (Radius * 2.0f) / static_cast<float>(Res);

			ImGui::TextColored(ColorPaleBlue, "[%d] Split: %.1f", i, Split);
			ImGui::SameLine(105.f);
			ImGui::TextColored(ColorPaleBlue, "TexSize: %.2f", TexelSize);
		}
		WindowWidth = ImGui::GetWindowSize().x;
	}
	ImGui::End();

	return WindowWidth;
}

float FEditorViewportOverlayWidget::RenderShadowAtlasWindow(int32 ViewportIndex, const FEditorViewportState& VS, const ImVec2& Pos)
{
	if (!VS.bShowStatShadowAtlas || !EditorEngine)
	{
		return 0.0f;
	}

	FSceneViewport& SceneViewport = EditorEngine->GetViewportLayout().GetSceneViewport(ViewportIndex);
	FRenderTargetSet* RenderTargets = SceneViewport.GetRenderTargetSet();

	constexpr float PreviewSize = 256.0f;

	ImGui::SetNextWindowPos(Pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.3f);

	char WinId[40];
	snprintf(WinId, sizeof(WinId), "##ShadowAtlasOverlay_%d", ViewportIndex);

	float WindowWidth = 0.0f;
	if (ImGui::Begin(WinId, nullptr, kInteractiveStatFlags))
	{
		ImGui::TextColored(ColorPink, "Shadow Atlas Stat");
		ImGui::Separator();

		if (DrawRoundSelectButton("##SelectDirectionalShadowAtlas", "Directional", ShadowAtlasPreviewMode == EShadowAtlasPreviewMode::Directional, ColorYellow))
		{
			ShadowAtlasPreviewMode = EShadowAtlasPreviewMode::Directional;
		}
		ImGui::SameLine(0.0f, 14.0f);
		if (DrawRoundSelectButton("##SelectSpotShadowAtlas", "Spot", ShadowAtlasPreviewMode == EShadowAtlasPreviewMode::Spot, ColorOrange))
		{
			ShadowAtlasPreviewMode = EShadowAtlasPreviewMode::Spot;
		}
		ImGui::SameLine(0.0f, 14.0f);
		if (DrawRoundSelectButton("##SelectPointShadowAtlas", "Point", ShadowAtlasPreviewMode == EShadowAtlasPreviewMode::Point, ColorMint))
		{
			ShadowAtlasPreviewMode = EShadowAtlasPreviewMode::Point;
		}
		ImGui::Separator();

		// ──────────── Directional ────────────
		if (ShadowAtlasPreviewMode == EShadowAtlasPreviewMode::Directional)
		{
		ImGui::BeginGroup();
		{
			ImGui::TextColored(ColorYellow, "Directional Shadow Atlas");
			ImGui::TextColored(ColorPaleBlue, "- Cascades: %u", FShadowAtlasManager::DirectionalCascadeCount);
			ImGui::TextColored(ColorPaleBlue, "- Atlas: %ux%u",
				FShadowAtlasManager::DirectionalAtlasResolution,
				FShadowAtlasManager::DirectionalAtlasResolution);

			const bool bHasDirShadow = (RenderTargets != nullptr && RenderTargets->DirectionalShadowSRV != nullptr);
			if (bHasDirShadow)
			{
				ImGui::Image(reinterpret_cast<ImTextureID>(RenderTargets->DirectionalShadowSRV), ImVec2(PreviewSize, PreviewSize));
			}
			else
			{
				ImGui::Dummy(ImVec2(PreviewSize, PreviewSize));
			}

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Max = ImGui::GetItemRectMax();

			if (!bHasDirShadow)
			{
				DrawList->AddRectFilled(Min, Max, IM_COL32(0, 0, 0, 150));
			}
			DrawList->AddRect(Min, Max, IM_COL32(255, 255, 255, 100));
			DrawAtlasGrid(DrawList, Min, Max, FShadowAtlasManager::DirectionalAtlasGridDimension);

			if (bHasDirShadow)
			{
				const TArray<FDirectionalAtlasSlotDesc>& CascadeSlots = FShadowAtlasManager::GetDirectionalCascadeSlots();
				for (const FDirectionalAtlasSlotDesc& Slot : CascadeSlots)
				{
					const float X0 = Min.x + (static_cast<float>(Slot.X) / FShadowAtlasManager::DirectionalAtlasResolution) * PreviewSize;
					const float Y0 = Min.y + (static_cast<float>(Slot.Y) / FShadowAtlasManager::DirectionalAtlasResolution) * PreviewSize;
					const float X1 = Min.x + (static_cast<float>(Slot.X + Slot.Width) / FShadowAtlasManager::DirectionalAtlasResolution) * PreviewSize;
					const float Y1 = Min.y + (static_cast<float>(Slot.Y + Slot.Height) / FShadowAtlasManager::DirectionalAtlasResolution) * PreviewSize;

					DrawList->AddRect(ImVec2(X0, Y0), ImVec2(X1, Y1), IM_COL32(255, 220, 0, 220), 0.0f, 0, 2.0f);

					char Label[16];
					snprintf(Label, sizeof(Label), "C%u", Slot.CascadeIndex);
					DrawList->AddText(ImVec2(X0 + 4.0f, Y0 + 4.0f), IM_COL32(255, 220, 0, 255), Label);
				}
			}
		}
		ImGui::EndGroup();
		}

		// ──────────── Spot ────────────
		if (ShadowAtlasPreviewMode == EShadowAtlasPreviewMode::Spot)
		{
		ImGui::BeginGroup();
		{
			ImGui::TextColored(ColorOrange, "Spot Shadow Atlas");
			ImGui::TextColored(ColorPaleBlue, "- Active Shadows: %u", RenderTargets ? RenderTargets->SpotShadowCount : 0);
			ImGui::TextColored(ColorPaleBlue, "- Atlas: %ux%u",
				FShadowAtlasManager::SpotAtlasResolution,
				FShadowAtlasManager::SpotAtlasResolution);

			const bool bHasSpotShadow = (RenderTargets != nullptr && RenderTargets->SpotShadowSRV != nullptr);
			if (bHasSpotShadow)
			{
				ImGui::Image(reinterpret_cast<ImTextureID>(RenderTargets->SpotShadowSRV), ImVec2(PreviewSize, PreviewSize));
			}
			else
			{
				ImGui::Dummy(ImVec2(PreviewSize, PreviewSize));
			}

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Max = ImGui::GetItemRectMax();

			if (!bHasSpotShadow)
			{
				DrawList->AddRectFilled(Min, Max, IM_COL32(0, 0, 0, 150));
			}
			DrawList->AddRect(Min, Max, IM_COL32(255, 255, 255, 100));

			// Grid
			const float BaseCell = PreviewSize / static_cast<float>(FShadowAtlasManager::SpotAtlasCellsPerRow);
			for (uint32 Line = 1; Line < FShadowAtlasManager::SpotAtlasCellsPerRow; ++Line)
			{
				const float X = Min.x + BaseCell * static_cast<float>(Line);
				const float Y = Min.y + BaseCell * static_cast<float>(Line);
				DrawList->AddLine(ImVec2(X, Min.y), ImVec2(X, Max.y), IM_COL32(255, 255, 255, 35));
				DrawList->AddLine(ImVec2(Min.x, Y), ImVec2(Max.x, Y), IM_COL32(255, 255, 255, 35));
			}

			if (bHasSpotShadow)
			{
				const TArray<FSpotAtlasSlotDesc>& ActiveSlots = FShadowAtlasManager::GetActiveSpotSlots();
				for (const FSpotAtlasSlotDesc& Slot : ActiveSlots)
				{
					const float X0 = Min.x + (static_cast<float>(Slot.X) / FShadowAtlasManager::SpotAtlasResolution) * PreviewSize;
					const float Y0 = Min.y + (static_cast<float>(Slot.Y) / FShadowAtlasManager::SpotAtlasResolution) * PreviewSize;
					const float X1 = Min.x + (static_cast<float>(Slot.X + Slot.Width) / FShadowAtlasManager::SpotAtlasResolution) * PreviewSize;
					const float Y1 = Min.y + (static_cast<float>(Slot.Y + Slot.Height) / FShadowAtlasManager::SpotAtlasResolution) * PreviewSize;

					DrawList->AddRect(ImVec2(X0, Y0), ImVec2(X1, Y1), IM_COL32(0, 255, 120, 220), 0.0f, 0, 2.0f);

					char Label[32];
					if (Slot.DebugLightId >= 0)
					{
						snprintf(Label, sizeof(Label), "%d (%u)", Slot.DebugLightId, Slot.Width);
					}
					else
					{
						// actor 번호를 아직 얻지 못한 경우에만 기존 tile index를 fallback으로 보여줍니다.
						snprintf(Label, sizeof(Label), "%u (%u)", Slot.TileIndex, Slot.Width);
					}
					DrawList->AddText(ImVec2(X0 + 4.0f, Y0 + 4.0f), IM_COL32(0, 255, 120, 255), Label);
				}
			}
		}
		ImGui::EndGroup();
		}

		// ──────────── Point ────────────
		if (ShadowAtlasPreviewMode == EShadowAtlasPreviewMode::Point)
		{
		ImGui::BeginGroup();
		{
			ImGui::TextColored(ColorMint, "Point Shadow Atlas");
			ImGui::TextColored(ColorPaleBlue, "- Active Shadows: %u", RenderTargets ? RenderTargets->PointShadowCount : 0);
			ImGui::TextColored(ColorPaleBlue, "- Atlas: %ux%u",
				FShadowAtlasManager::PointAtlasResolution,
				FShadowAtlasManager::PointAtlasResolution);

			const bool bHasPointShadow = (RenderTargets != nullptr && RenderTargets->PointShadowSRV != nullptr);
			if (bHasPointShadow)
			{
				ImGui::Image(reinterpret_cast<ImTextureID>(RenderTargets->PointShadowSRV), ImVec2(PreviewSize, PreviewSize));
			}
			else
			{
				ImGui::Dummy(ImVec2(PreviewSize, PreviewSize));
			}

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Max = ImGui::GetItemRectMax();

			if (!bHasPointShadow)
			{
				DrawList->AddRectFilled(Min, Max, IM_COL32(0, 0, 0, 150));
			}
			DrawList->AddRect(Min, Max, IM_COL32(255, 255, 255, 100));

			const float BaseCell = PreviewSize / static_cast<float>(FShadowAtlasManager::PointAtlasCellsPerRow);
			for (uint32 Line = 1; Line < FShadowAtlasManager::PointAtlasCellsPerRow; ++Line)
			{
				const float X = Min.x + BaseCell * static_cast<float>(Line);
				const float Y = Min.y + BaseCell * static_cast<float>(Line);
				DrawList->AddLine(ImVec2(X, Min.y), ImVec2(X, Max.y), IM_COL32(255, 255, 255, 35));
				DrawList->AddLine(ImVec2(Min.x, Y), ImVec2(Max.x, Y), IM_COL32(255, 255, 255, 35));
			}

			if (bHasPointShadow)
			{
				ImFont* Font = ImGui::GetFont();
				const float SmallFontSize = ImGui::GetFontSize() * 0.9f;

				const TArray<FPointAtlasSlotDesc>& ActivePointSlots = FShadowAtlasManager::GetActivePointSlots();
				for (const FPointAtlasSlotDesc& Slot : ActivePointSlots)
				{
					for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
					{
						const FVector4& Rect = Slot.FaceAtlasRects[FaceIndex];

						const float X0 = Min.x + Rect.X * PreviewSize;
						const float Y0 = Min.y + Rect.Y * PreviewSize;
						const float X1 = Min.x + (Rect.X + Rect.Z) * PreviewSize;
						const float Y1 = Min.y + (Rect.Y + Rect.W) * PreviewSize;

						DrawList->AddRect(ImVec2(X0, Y0), ImVec2(X1, Y1), IM_COL32(80, 190, 255, 220), 0.0f, 0, 1.5f);
					}
				    
				    const FVector4& FirstRect = Slot.FaceAtlasRects[0];
				    const float LabelX = Min.x + FirstRect.X * PreviewSize + 2.0f;
				    const float LabelY = Min.y + FirstRect.Y * PreviewSize;

				    char Label[32];
				    snprintf(Label, sizeof(Label), "P%u", Slot.CubeIndex);
				    DrawList->AddText(Font, SmallFontSize, ImVec2(LabelX, LabelY), IM_COL32(0, 255, 255, 255), Label);
				}
			}
		}
		ImGui::EndGroup();
		}

		WindowWidth = ImGui::GetWindowSize().x;
	}
	ImGui::End();

	return WindowWidth;
}

namespace
{
	// 뷰포트 타입(Enum)을 UI에 표시할 문자열로 변환합니다.
	const char* GetViewportTypeName(EEditorViewportType Type)
	{
		switch (Type)
		{
		case EVT_Perspective: return "Perspective";
		case EVT_OrthoTop:    return "Top";
		case EVT_OrthoBottom: return "Bottom";
		case EVT_OrthoFront:  return "Front";
		case EVT_OrthoBack:   return "Back";
		case EVT_OrthoLeft:   return "Left";
		case EVT_OrthoRight:  return "Right";
		default:              return "Viewport";
		}
	}

	// 뷰 모드(Enum)를 UI에 표시할 문자열로 변환합니다.
	const char* GetViewModeName(EViewMode Mode)
	{
		switch (Mode)
		{
		case EViewMode::Lit:         return "Lit";
		case EViewMode::Unlit:       return "Unlit";
		case EViewMode::Wireframe:   return "Wireframe";
		case EViewMode::SceneDepth:  return "Scene Depth";
		case EViewMode::WorldNormal: return "World Normal";
		case EViewMode::CascadeShadow: return "Cascade Shadow";
		case EViewMode::DebugCollision: return "Collision";
		default:                     return "Lit";
		}
	}

	// Atlas preview 위에 균등한 격자선을 그립니다.
	void DrawAtlasGrid(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, uint32 GridDimension)
	{
		if (DrawList == nullptr || GridDimension <= 1)
		{
			return;
		}

		const float CellSizeX = (Max.x - Min.x) / static_cast<float>(GridDimension);
		const float CellSizeY = (Max.y - Min.y) / static_cast<float>(GridDimension);

		for (uint32 Line = 1; Line < GridDimension; ++Line)
		{
			const float X = Min.x + CellSizeX * static_cast<float>(Line);
			const float Y = Min.y + CellSizeY * static_cast<float>(Line);
			DrawList->AddLine(ImVec2(X, Min.y), ImVec2(X, Max.y), IM_COL32(255, 255, 255, 35));
			DrawList->AddLine(ImVec2(Min.x, Y), ImVec2(Max.x, Y), IM_COL32(255, 255, 255, 35));
		}
	}

	// Point shadow cubemap face index를 atlas label 문자열로 변환합니다.
	const char* GetPointFaceLabel(uint32 FaceIndex)
	{
		switch (FaceIndex)
		{
		case 0: return "+X";
		case 1: return "-X";
		case 2: return "+Y";
		case 3: return "-Y";
		case 4: return "+Z";
		case 5: return "-Z";
		default: return "?";
		}
	}

	// 원형 선택 버튼을 그려 atlas preview mode를 변경합니다.
	bool DrawRoundSelectButton(const char* Id, const char* Label, bool bSelected, const ImVec4& AccentColor)
	{
		constexpr float Radius = 6.0f;
		constexpr float Diameter = Radius * 2.0f;
		constexpr float LabelGap = 6.0f;

		const ImVec2 Start = ImGui::GetCursorScreenPos();
		const ImVec2 TextSize = ImGui::CalcTextSize(Label);
		const ImVec2 ButtonSize(Diameter + LabelGap + TextSize.x, std::max(Diameter, TextSize.y));

		ImGui::InvisibleButton(Id, ButtonSize);
		const bool bClicked = ImGui::IsItemClicked();

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Center(Start.x + Radius, Start.y + ButtonSize.y * 0.6f);
		const ImU32 BorderColor = ImGui::ColorConvertFloat4ToU32(AccentColor);
		const ImU32 FillColor = bSelected ? BorderColor : IM_COL32(0, 0, 0, 80);

		DrawList->AddCircleFilled(Center, Radius, FillColor, 20);
		DrawList->AddCircle(Center, Radius, BorderColor, 20, 1.5f);
		if (bSelected)
		{
			DrawList->AddCircleFilled(Center, Radius * 0.45f, IM_COL32(255, 255, 255, 240), 16);
		}

		DrawList->AddText(ImVec2(Start.x + Diameter + LabelGap, Start.y + (ButtonSize.y - TextSize.y) * 0.5f),
			IM_COL32(220, 230, 245, 255), Label);

		return bClicked;
	}
}
