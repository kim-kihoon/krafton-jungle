#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/UI/EditorChromeConstants.h"
#include "Editor/UI/EditorMainPanelViewportToolbarHelpers.h"
#include "Editor/Viewer/AnimationViewer.h"
#include "Editor/Viewer/EditorViewer.h"
#include "Editor/Viewer/FSkeletalMeshViewer.h"
#include "Editor/Viewport/EditorViewportClient.h"

#include "ImGui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <Viewport/SkeletalMeshViewportClient.h>

namespace
{
FSkeletalViewerShowFlags* GetViewerShowFlags(FEditorViewer* Viewer)
{
	if (FSkeletalMeshViewer* SkeletalViewer = dynamic_cast<FSkeletalMeshViewer*>(Viewer))
	{
		return &SkeletalViewer->GetClient().GetShowFlags();
	}

	if (FAnimationViewer* AnimationViewer = dynamic_cast<FAnimationViewer*>(Viewer))
	{
		return &AnimationViewer->GetClient().GetShowFlags();
	}

	return nullptr;
}
}

void FEditorMainPanel::RenderActiveDocumentToolbar()
{
	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	constexpr float TabStripHeight = FEditorChromeMetrics::TabStripHeight;
	constexpr float ToolbarHeight = FEditorChromeMetrics::DocumentToolbarHeight;
	const ImVec2 ToolbarPos(
		MainViewport->WorkPos.x,
		MainViewport->WorkPos.y + FEditorChromeMetrics::ApplicationTitleBarHeight + TabStripHeight);
	const ImVec2 ToolbarSize(MainViewport->WorkSize.x, ToolbarHeight);

	ImGui::SetNextWindowPos(ToolbarPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ToolbarSize, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.14f, 0.98f));

	constexpr ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking;

	if (ImGui::Begin("##EditorDocumentToolbar", nullptr, Flags))
	{
		const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
		FEditorViewerWindowWidget* ViewerWidget = ActiveTab ? FindViewerWidgetForTab(ActiveTab->Id) : nullptr;
		FEditorViewer* Viewer = ViewerWidget ? ViewerWidget->GetViewer() : nullptr;

		if (Viewer)
		{
			RenderViewerToolbarControls(Viewer);
		}
		else if (ActiveTab && ActiveTab->Id.Kind == EEditorTabKind::RuntimeUIPreview)
		{
			ImGui::TextDisabled("Runtime UI Preview");
			ImGui::SameLine();
			ImGui::TextUnformatted(Widgets.RuntimeUIPreviewWidget.GetPreviewDocumentPath().c_str());
		}
		else
		{
			ImGui::TextDisabled("No active document");
		}
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}

void FEditorMainPanel::RenderViewerToolbarControls(FEditorViewer* Viewer)
{
	if (!Viewer)
	{
		ImGui::TextDisabled("No viewer");
		return;
	}

	FEditorViewportClient* Client = Viewer->GetViewportClient();
	if (!Client)
	{
		ImGui::TextDisabled("Viewer is not ready.");
		return;
	}

	FEditorViewportState* ViewportState = Client ? Client->GetViewportState() : nullptr;
	FSkeletalViewerShowFlags* ShowFlags = GetViewerShowFlags(Viewer);

	ImGui::PushID(Viewer);
	if (DrawViewportIconButton(
		"##ViewerSelectModeShared",
		EEditorMainPanelViewportToolIcon::Select,
		"Q",
		"Select (Q / 1)",
		Client->GetTransformMode() == FEditorViewportClient::ETransformMode::Select,
		true))
	{
		Client->RequestSetSelectMode();
	}
	ImGui::SameLine();
	if (DrawViewportIconButton(
		"##ViewerTranslateModeShared",
		EEditorMainPanelViewportToolIcon::Translate,
		"W",
		"Translate (W / 2)",
		Client->GetTransformMode() == FEditorViewportClient::ETransformMode::Translate,
		true))
	{
		Client->RequestSetTranslateMode();
	}
	ImGui::SameLine();
	if (DrawViewportIconButton(
		"##ViewerRotateModeShared",
		EEditorMainPanelViewportToolIcon::Rotate,
		"E",
		"Rotate (E / 3)",
		Client->GetTransformMode() == FEditorViewportClient::ETransformMode::Rotate,
		true))
	{
		Client->RequestSetRotateMode();
	}
	ImGui::SameLine();
	if (DrawViewportIconButton(
		"##ViewerScaleModeShared",
		EEditorMainPanelViewportToolIcon::Scale,
		"R",
		"Scale (R / 4)",
		Client->GetTransformMode() == FEditorViewportClient::ETransformMode::Scale,
		true))
	{
		Client->RequestSetScaleMode();
	}

	ImGui::SameLine(0.0f, 10.0f);
	char TypeButtonLabel[64];
	snprintf(
		TypeButtonLabel,
		sizeof(TypeButtonLabel),
		"%s ▼",
		FEditorMainPanelViewportToolbarHelpers::GetViewportTypeName(Client->GetViewportType()));
	if (DrawViewportTextButton("##ViewerViewportTypeShared", TypeButtonLabel))
	{
		ImGui::OpenPopup("##ViewerViewportTypePopupShared");
	}
	if (ImGui::BeginPopup("##ViewerViewportTypePopupShared"))
	{
		static constexpr EEditorViewportType Types[] = {
			EVT_Perspective,
			EVT_OrthoTop,
			EVT_OrthoBottom,
			EVT_OrthoFront,
			EVT_OrthoBack,
			EVT_OrthoLeft,
			EVT_OrthoRight,
		};
		for (EEditorViewportType Type : Types)
		{
			if (ImGui::MenuItem(
				FEditorMainPanelViewportToolbarHelpers::GetViewportTypeName(Type),
				nullptr,
				Client->GetViewportType() == Type))
			{
				Client->SetViewportType(Type);
				Client->ApplyCameraMode();
			}
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	char CameraButtonLabel[48];
	snprintf(
		CameraButtonLabel,
		sizeof(CameraButtonLabel),
		"Cam %.1fx ▼",
		FEditorMainPanelViewportToolbarHelpers::GetCameraSpeedMultiplier(Client));
	if (DrawViewportTextButton("##ViewerCameraSpeedShared", CameraButtonLabel))
	{
		ImGui::OpenPopup("##ViewerCameraSpeedPopupShared");
	}
	if (ImGui::BeginPopup("##ViewerCameraSpeedPopupShared"))
	{
		float SpeedMultiplier = FEditorMainPanelViewportToolbarHelpers::GetCameraSpeedMultiplier(Client);
		if (ImGui::SliderFloat("Speed", &SpeedMultiplier, 0.01f, FEditorMainPanelViewportToolbarHelpers::MaxCameraSpeedMultiplier, "%.2fx"))
		{
			FEditorMainPanelViewportToolbarHelpers::SetCameraSpeedMultiplier(Client, SpeedMultiplier);
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	const char* ViewModeName = ViewportState
		? FEditorMainPanelViewportToolbarHelpers::GetViewModeName(ViewportState->ViewMode)
		: "View";
	char ViewButtonLabel[80];
	snprintf(ViewButtonLabel, sizeof(ViewButtonLabel), "%s ▼", ViewModeName);
	if (DrawViewportTextButton("##ViewerViewModeShared", ViewButtonLabel))
	{
		ImGui::OpenPopup("##ViewerViewModePopupShared");
	}
	if (ViewportState && ImGui::BeginPopup("##ViewerViewModePopupShared"))
	{
		static constexpr EViewMode Modes[] = {
			EViewMode::Lit_Gouraud,
			EViewMode::Lit_Lambert,
			EViewMode::Lit_BlinnPhong,
			EViewMode::Unlit,
			EViewMode::Heatmap,
			EViewMode::Wireframe,
			EViewMode::Depth,
			EViewMode::Normal,
			EViewMode::IdBuffer,
		};
		for (EViewMode Mode : Modes)
		{
			if (ImGui::MenuItem(
				FEditorMainPanelViewportToolbarHelpers::GetViewModeName(Mode),
				nullptr,
				ViewportState->ViewMode == Mode))
			{
				ViewportState->ViewMode = Mode;
			}
		}
		ImGui::Separator();
		if (ImGui::BeginMenu("Light Culling"))
		{
			static constexpr ELightCullMode CullModes[] = {
				ELightCullMode::Clustered,
				ELightCullMode::Tiled,
				ELightCullMode::None,
			};
			for (ELightCullMode CullMode : CullModes)
			{
				if (ImGui::MenuItem(
					FEditorMainPanelViewportToolbarHelpers::GetLightCullModeName(CullMode),
					nullptr,
					ViewportState->LightCullMode == CullMode))
				{
					ViewportState->LightCullMode = CullMode;
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine(0.0f, 10.0f);
	if (DrawViewportTextButton("##ViewerShowFlagsShared", "Show ▼"))
	{
		ImGui::OpenPopup("##ViewerShowFlagsPopupShared");
	}
	if (ImGui::BeginPopup("##ViewerShowFlagsPopupShared"))
	{
		if (ShowFlags)
		{
			ImGui::MenuItem("Skeletal Mesh", nullptr, &ShowFlags->bShowSkeletalMesh);
			ImGui::MenuItem("Bones", nullptr, &ShowFlags->bShowBones);
			ImGui::BeginDisabled(!ShowFlags->bShowBones);
			ImGui::MenuItem("Selected Bone Only", nullptr, &ShowFlags->bShowOnlySelectedBone);
			ImGui::EndDisabled();
			ImGui::Separator();
			ImGui::MenuItem("Bounding Box", nullptr, &ShowFlags->bShowBoundingBox);
			ImGui::MenuItem("Outline", nullptr, &ShowFlags->bShowOutline);
			ImGui::Separator();
			ImGui::MenuItem("Bone Weight Heatmap", nullptr, &ShowFlags->bShowBoneWeightHeatmap);
			ImGui::BeginDisabled(!ShowFlags->bShowBoneWeightHeatmap);
			if (ImGui::SliderFloat("Opacity", &ShowFlags->BoneWeightHeatmapOpacity, 0.05f, 1.0f, "%.2f"))
			{
				ShowFlags->BoneWeightHeatmapOpacity = std::clamp(ShowFlags->BoneWeightHeatmapOpacity, 0.05f, 1.0f);
			}
			ImGui::EndDisabled();
		}
		else
		{
			ImGui::TextDisabled("No show flags");
		}
		ImGui::EndPopup();
	}

	ImGui::PopID();
}

bool FEditorMainPanel::RenderActiveDocumentMainMenu()
{
	const FEditorTabEntry* ActiveTab = EditorTabs.GetActiveTab();
	if (!ActiveTab || ActiveTab->Id.Kind == EEditorTabKind::LevelEditor || ActiveTab->bDetached)
	{
		return false;
	}

	auto RenderCommonDocumentWindowMenu = [this, ActiveTab]()
	{
		if (!ImGui::BeginMenu("Window"))
		{
			return;
		}

		if (ActiveTab->bCanClose && ImGui::MenuItem("Detach Tab"))
		{
			RequestDetachEditorTab(ActiveTab->Id, true);
		}
		ImGui::MenuItem("Content Browser", "Ctrl+Space", &PanelVisibility.bShowContentBrowser);
		ImGui::EndMenu();
	};

	auto RenderDocumentHelpMenu = []()
	{
		if (!ImGui::BeginMenu("Help"))
		{
			return;
		}
		ImGui::TextDisabled("Document-specific editor context");
		ImGui::EndMenu();
	};

	if (ActiveTab->Id.Kind == EEditorTabKind::AnimationViewer ||
		ActiveTab->Id.Kind == EEditorTabKind::AnimStateMachineGraphViewer ||
		ActiveTab->Id.Kind == EEditorTabKind::SkeletalMeshViewer ||
		ActiveTab->Id.Kind == EEditorTabKind::StaticMeshViewer)
	{
		FEditorViewerWindowWidget* ViewerWidget = FindViewerWidgetForTab(ActiveTab->Id);
		FEditorViewer* Viewer = ViewerWidget ? ViewerWidget->GetViewer() : nullptr;
		const bool bAnimationViewer = ActiveTab->Id.Kind == EEditorTabKind::AnimationViewer;
		const bool bGraphViewer = ActiveTab->Id.Kind == EEditorTabKind::AnimStateMachineGraphViewer;
		const bool bCanSaveAsset = bGraphViewer
			? (ViewerWidget && ViewerWidget->CanSaveGraph())
			: (!bAnimationViewer && ViewerWidget && ViewerWidget->CanSaveMesh());
		const char* SaveLabel = bGraphViewer
			? (ViewerWidget && ViewerWidget->IsGraphDirty() ? "Save State Machine *" : "Save State Machine")
			: (bAnimationViewer
			? "Save Animation"
			: (ViewerWidget && ViewerWidget->IsMeshDirty() ? "Save Mesh *" : "Save Mesh"));

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem(SaveLabel, "Ctrl+S", false, bCanSaveAsset))
			{
				bGraphViewer ? ViewerWidget->RequestSaveGraph() : ViewerWidget->RequestSaveMesh();
			}
			ImGui::Separator();
			if (ActiveTab->bCanClose && ImGui::MenuItem("Close Tab"))
			{
				RequestCloseEditorTab(ActiveTab->Id);
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
			ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, false);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Asset"))
		{
			if (ImGui::MenuItem(SaveLabel, nullptr, false, bCanSaveAsset))
			{
				bGraphViewer ? ViewerWidget->RequestSaveGraph() : ViewerWidget->RequestSaveMesh();
			}
			ImGui::MenuItem(bGraphViewer ? "Validate State Machine" : (bAnimationViewer ? "Reimport Animation" : "Reimport Mesh"), nullptr, false, false);
			if (Viewer)
			{
				ImGui::Separator();
				ImGui::TextDisabled("%s", Viewer->GetFileName().c_str());
			}
			ImGui::EndMenu();
		}

		RenderCommonDocumentWindowMenu();

		if (ImGui::BeginMenu("Tools"))
		{
			if (bGraphViewer)
			{
				ImGui::TextDisabled("Graph editor tools");
			}
			else if (Viewer)
			{
				FEditorViewportClient* Client = Viewer->GetViewportClient();
				FSkeletalViewerShowFlags* ShowFlags = GetViewerShowFlags(Viewer);
				if (!Client)
				{
					ImGui::TextDisabled("Viewer is not ready.");
					ImGui::EndMenu();
					RenderDocumentHelpMenu();
					return true;
				}

				if (ImGui::MenuItem("Select", "Q / 1", Client->GetTransformMode() == FEditorViewportClient::ETransformMode::Select))
				{
					Client->RequestSetSelectMode();
				}
				if (ImGui::MenuItem("Translate", "W / 2", Client->GetTransformMode() == FEditorViewportClient::ETransformMode::Translate))
				{
					Client->RequestSetTranslateMode();
				}
				if (ImGui::MenuItem("Rotate", "E / 3", Client->GetTransformMode() == FEditorViewportClient::ETransformMode::Rotate))
				{
					Client->RequestSetRotateMode();
				}
				if (ImGui::MenuItem("Scale", "R / 4", Client->GetTransformMode() == FEditorViewportClient::ETransformMode::Scale))
				{
					Client->RequestSetScaleMode();
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Reset Preview Camera"))
				{
					Client->ResetCamera();
					Client->ApplyCameraMode();
				}

				if (ShowFlags)
				{
					ImGui::Separator();
					ImGui::MenuItem("Skeletal Mesh", nullptr, &ShowFlags->bShowSkeletalMesh);
					ImGui::MenuItem("Bones", nullptr, &ShowFlags->bShowBones);
					ImGui::BeginDisabled(!ShowFlags->bShowBones);
					ImGui::MenuItem("Selected Bone Only", nullptr, &ShowFlags->bShowOnlySelectedBone);
					ImGui::EndDisabled();
					ImGui::MenuItem("Bounding Box", nullptr, &ShowFlags->bShowBoundingBox);
					ImGui::MenuItem("Outline", nullptr, &ShowFlags->bShowOutline);
					ImGui::Separator();
					ImGui::MenuItem("Bone Weight Heatmap", nullptr, &ShowFlags->bShowBoneWeightHeatmap);
					ImGui::BeginDisabled(!ShowFlags->bShowBoneWeightHeatmap);
					if (ImGui::SliderFloat("Opacity", &ShowFlags->BoneWeightHeatmapOpacity, 0.05f, 1.0f, "%.2f"))
					{
						ShowFlags->BoneWeightHeatmapOpacity = std::clamp(ShowFlags->BoneWeightHeatmapOpacity, 0.05f, 1.0f);
					}
					ImGui::EndDisabled();
				}
			}
			else
			{
				ImGui::TextDisabled("Viewer is not ready.");
			}
			ImGui::EndMenu();
		}

		RenderDocumentHelpMenu();
		return true;
	}

	if (ImGui::BeginMenu("File"))
	{
		ImGui::MenuItem("Save Asset", "Ctrl+S", false, false);
		ImGui::Separator();
		if (ActiveTab->bCanClose && ImGui::MenuItem("Close Tab"))
		{
			RequestCloseEditorTab(ActiveTab->Id);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit"))
	{
		ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
		ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, false);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Asset"))
	{
		switch (ActiveTab->Id.Kind)
		{
		case EEditorTabKind::MaterialEditor:
			ImGui::TextDisabled("Material Editor");
			break;
		case EEditorTabKind::CurveEditor:
			ImGui::TextDisabled("Curve Editor");
			break;
		case EEditorTabKind::ActorSequencer:
			ImGui::TextDisabled("Actor Sequencer");
			break;
		case EEditorTabKind::RuntimeUIPreview:
			ImGui::TextDisabled("Runtime UI Preview");
			break;
		default:
			ImGui::TextDisabled("Document");
			break;
		}
		ImGui::EndMenu();
	}

	RenderCommonDocumentWindowMenu();
	RenderDocumentHelpMenu();
	return true;
}
