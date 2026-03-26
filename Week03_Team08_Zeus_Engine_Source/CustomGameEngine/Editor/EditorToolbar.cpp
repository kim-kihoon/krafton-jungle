#include "EditorToolbar.h"
#include "Gizmo.h"
#include "Component/GizmoComponent.h"
#include "Editor/Gizmo/GizmoRotationControlStrategy.h"
#include "ImGui/imgui.h"

EditorToolbar::EditorToolbar()
{
}

EditorToolbar::~EditorToolbar()
{
}

void EditorToolbar::Draw()
{
	EditorPanelVisibility& visibility = GetEditorPanelVisibility();
	if (!visibility.bShowToolbar)
		return;

	UGizmoComponent* gizmoController = Gizmo::GetInstance().GetController();
	GizmoControllerType currentType = gizmoController->GetControlStrategyType();

	const EditorLayout layout = GetEditorLayout(visibility);
	const ImGuiWindowFlags windowFlags = ApplyFixedWindow(
		layout.Toolbar,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse
	);

	ImGui::Begin("Toolbar", nullptr, windowFlags);

	auto DrawActiveButton = [](const char* label, bool bActive) -> bool
		{
			if (bActive)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
			}

			bool bClicked = ImGui::Button(label);

			if (bActive)
			{
				ImGui::PopStyleColor(3);
			}

			return bClicked;
		};

	bool bLocalSpace = gizmoController->bLocalSpace;
	bool bScaleMode = currentType == GizmoControllerType::Scale;

	if (!bLocalSpace && !bScaleMode ? ImGui::Button("World") : ImGui::Button("Local"))
	{
		if (currentType != GizmoControllerType::Scale)
		{
			gizmoController->bLocalSpace = !bLocalSpace;
		}
	}

	if (currentType == GizmoControllerType::Rotation)
	{
		GizmoRotationControlStrategy* rotationStrategy =
			dynamic_cast<GizmoRotationControlStrategy*>(gizmoController->GetControlStrategy());

		bool bSnapping = rotationStrategy->bEnableSnap;
		ImGui::SameLine();

		if (bSnapping)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
		}

		if (ImGui::Button("Snapping"))
		{
			rotationStrategy->bEnableSnap = !bSnapping;
		}

		if (bSnapping)
		{
			ImGui::PopStyleColor(3);
		}
	}

	if (DrawActiveButton("Translate (W)", currentType == GizmoControllerType::Translation))
	{
		gizmoController->SetControlStrategy(GizmoControllerType::Translation);
	}

	ImGui::SameLine();

	if (DrawActiveButton("Rotate (E)", currentType == GizmoControllerType::Rotation))
	{
		gizmoController->SetControlStrategy(GizmoControllerType::Rotation);
	}

	ImGui::SameLine();

	if (DrawActiveButton("Scale (R)", currentType == GizmoControllerType::Scale))
	{
		gizmoController->SetControlStrategy(GizmoControllerType::Scale);
	}

	if (ImGui::BeginPopupContextWindow("ToolbarPanelsPopup"))
	{
		ImGui::TextUnformatted("Panels");
		ImGui::Separator();

		ImGui::Checkbox("Stats", &visibility.bShowStatPanel);
		ImGui::Checkbox("Scene Manager", &visibility.bShowSceneManager);
		ImGui::Checkbox("Property", &visibility.bShowPropertyPanel);
		ImGui::Checkbox("Control Panel", &visibility.bShowControlPanel);
		ImGui::Checkbox("Console", &visibility.bShowConsole);

		ImGui::EndPopup();
	}

	ImGui::End();
}