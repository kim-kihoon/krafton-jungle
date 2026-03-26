#include "EditorStatPanel.h"
#include "EngineStatics.h"
#include "ImGui/imgui.h"

EditorStatPanel::EditorStatPanel()
{
}

EditorStatPanel::~EditorStatPanel()
{
}

void EditorStatPanel::Draw()
{
    EditorPanelVisibility& visibility = GetEditorPanelVisibility();
    if (!visibility.bShowStatPanel)
        return;

    const EditorLayout layout = GetEditorLayout(visibility);
    const ImGuiWindowFlags windowFlags = ApplyFixedWindow(layout.StatPanel);
    ImGui::Begin("Jungle Stats Panel", nullptr, windowFlags);

	ImGui::Text("Allocation Count: %u", UEngineStatics::TotalAllocationCount);
	ImGui::Text("Allocation Bytes: %u", UEngineStatics::TotalAllocatedBytes);
	ImGui::Text("Draw Calls: %u", UEngineStatics::TotalDrawCalls);

	ImGui::End();
}