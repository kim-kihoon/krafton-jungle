#include "Editor/UI/Panel/EditorReflectionPropertyWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/Selection/SelectionManager.h"

#include "ImGui/imgui.h"

void FEditorReflectionPropertyWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	if (!ImGui::Begin("Reflection Property Window"))
	{
		ImGui::End();
		return;
	}

	if (!EditorEngine)
	{
		ImGui::TextDisabled("No object selected.");
		ImGui::End();
		return;
	}

	Renderer.Render(EditorEngine->GetSelectionManager().GetSelectedDetailTargets());
	ImGui::End();
}
