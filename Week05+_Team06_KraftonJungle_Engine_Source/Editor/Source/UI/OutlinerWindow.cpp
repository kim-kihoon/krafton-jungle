#include "OutlinerWindow.h"

#include "imgui.h"
#include "EditorEngine.h"
#include "Core/ShowFlags.h"
#include "Core/ViewportClient.h"
#include "Scene/Level.h"
#include "Actor/Actor.h"
#include "Component/SubUVComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/UUIDBillboardComponent.h"

void FOutlinerWindow::Render(FEditorEngine* Engine)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
	const bool bOpen = ImGui::Begin("Outliner");
	ImGui::PopStyleVar();
	if (!bOpen)
	{
		ImGui::End();
		return;
	}
	if (!Engine || !Engine->GetEditorLevel())
	{
		ImGui::End();
		return;
	}


	AActor* SelectedActor = Engine->GetSelectedActor();

	ImGui::SeparatorText("Actors");

	ULevel* Level = Engine->GetEditorLevel();
	const TArray<AActor*>& Actors = Level->GetActors();
	

	for (AActor* Actor : Actors)
	{
;
		if (!Actor || Actor->IsPendingDestroy())
		{
			continue;
		}

		const bool bSelected = (Actor == SelectedActor);
		ImGui::PushID(Actor);
		bool bVisible = Actor->IsVisible();
		if (ImGui::Checkbox("##visible", &bVisible))
		{
			Actor->SetVisible(bVisible);
		}
		ImGui::SameLine();

		if (ImGui::Selectable(Actor->GetName().c_str(), bSelected))
		{
			Engine->SetSelectedActor(Actor);
		}
		ImGui::PopID();
	}

	ImGui::End();

}
