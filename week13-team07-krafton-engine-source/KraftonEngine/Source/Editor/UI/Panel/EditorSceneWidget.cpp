#include "Editor/UI/Panel/EditorSceneWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/Settings/EditorSettings.h"

#include "ImGui/imgui.h"
#include "Profiling/Stats/Stats.h"

void FEditorSceneWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
}

void FEditorSceneWidget::Render(float DeltaTime)
{
	if (!EditorEngine)
	{
		return;
	}

	(void)DeltaTime;
	ImGui::SetNextWindowSize(ImVec2(400.0f, 350.0f), ImGuiCond_Once);

	if (!ImGui::Begin("Scene Manager", &FEditorSettings::Get().UI.bScene))
	{
		ImGui::End();
		return;
	}

	// 씬 파일 작업은 상단 메뉴로 옮기고, Scene Manager는 액터 목록만 유지한다.
	RenderActorOutliner();

	ImGui::End();
}

void FEditorSceneWidget::RenderActorOutliner()
{
	SCOPE_STAT_CAT("SceneWidget::ActorOutliner", "5_UI");

	UWorld* World = EditorEngine->GetWorld();
	if (!World) return;

	const TArray<AActor*>& Actors = World->GetActors();

	// null이 아닌 유효 Actor 인덱스만 수집 (Clipper는 연속 인덱스 필요)
	ValidActorIndices.clear();
	ValidActorIndices.reserve(Actors.size());
	for (int32 i = 0; i < static_cast<int32>(Actors.size()); ++i)
	{
		if (Actors[i]) ValidActorIndices.push_back(i);
	}

	ImGui::Text("Actors (%d)", static_cast<int32>(ValidActorIndices.size()));
	ImGui::Separator();

	FSelectionManager& Selection = EditorEngine->GetSelectionManager();

	ImGui::BeginChild("ActorList", ImVec2(0, 0), ImGuiChildFlags_Borders);

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& !ImGui::GetIO().WantTextInput
		&& ImGui::IsKeyPressed(ImGuiKey_F2, false))
	{
		if (AActor* PrimaryActor = Selection.GetPrimarySelection())
		{
			FString CurrentName = PrimaryActor->GetFName().ToString();
			if (CurrentName.empty())
			{
				CurrentName = PrimaryActor->GetClass()->GetName();
			}
			strncpy_s(RenameBuffer, sizeof(RenameBuffer), CurrentName.c_str(), _TRUNCATE);
			bShowDuplicateWarning = false;
			ImGui::OpenPopup("Rename Actor");
		}
	}

	ImGuiListClipper Clipper;
	Clipper.Begin(static_cast<int>(ValidActorIndices.size()));
	while (Clipper.Step())
	{
		for (int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
		{
			AActor* Actor = Actors[ValidActorIndices[Row]];

			const FString& StoredName = Actor->GetFName().ToString();
			const char* DisplayName = StoredName.empty()
				? Actor->GetClass()->GetName()
				: StoredName.c_str();

			bool bIsSelected = Selection.IsSelected(Actor);
			ImGui::PushID(Actor);
			if (ImGui::Selectable(DisplayName, bIsSelected))
			{
				if (ImGui::GetIO().KeyShift)
				{
					Selection.SelectRange(Actor, Actors);
				}
				else if (ImGui::GetIO().KeyCtrl)
				{
					Selection.ToggleSelect(Actor);
				}
				else
				{
					Selection.Select(Actor);
				}
			}

			if (ImGui::IsItemClicked(ImGuiMouseButton_Right)
				&& !ImGui::GetIO().KeyCtrl
				&& !ImGui::GetIO().KeyShift)
			{
				Selection.Select(Actor);
			}

			if (ImGui::BeginPopupContextItem())
			{
				const bool bCanDelete = !Selection.IsEmpty();
				if (!bCanDelete)
				{
					ImGui::BeginDisabled();
				}

				if (ImGui::MenuItem("Delete", "Del"))
				{
					Selection.DeleteSelectedActors();
					if (EditorEngine)
					{
						EditorEngine->InvalidateOcclusionResults();
					}
				}

				if (!bCanDelete)
				{
					ImGui::EndDisabled();
				}

				ImGui::EndPopup();
			}

			ImGui::PopID();
		}
	}

	if (ImGui::BeginPopupModal("Rename Actor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		AActor* PrimaryActor = Selection.GetPrimarySelection();
		if (!PrimaryActor)
		{
			ImGui::CloseCurrentPopup();
		}
		else
		{
			ImGui::TextUnformatted("Rename");
			ImGui::Separator();
			if (ImGui::IsWindowAppearing())
			{
				ImGui::SetKeyboardFocusHere();
			}

			ImGui::SetNextItemWidth(320.0f);
			const bool bSubmit = ImGui::InputText("##ActorName", RenameBuffer, sizeof(RenameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

			if (bShowDuplicateWarning)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "이미 사용 중인 이름입니다.");
			}

			if (bSubmit)
			{
				if (RenameActor(PrimaryActor))
				{
					ImGui::CloseCurrentPopup();
				}
			}

			if (ImGui::Button("OK"))
			{
				if (RenameActor(PrimaryActor))
				{
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				bShowDuplicateWarning = false;
				RenameBuffer[0] = '\0';
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}

	ImGui::EndChild();
}

bool FEditorSceneWidget::RenameActor(AActor* Actor)
{
	if (!Actor)
	{
		return true;
	}

	FString NewName(RenameBuffer);
	FString CurrentName = Actor->GetFName().ToString();
	if (NewName == CurrentName)
	{
		RenameBuffer[0] = '\0';
		bShowDuplicateWarning = false;
		return true;
	}

	bShowDuplicateWarning = false;
	UWorld* World = EditorEngine ? EditorEngine->GetWorld() : nullptr;
	if (World)
	{
		for (AActor* OtherActor : World->GetActors())
		{
			if (OtherActor == Actor) continue;
			if (OtherActor && OtherActor->GetFName().ToString() == NewName)
			{
				bShowDuplicateWarning = true;
				return false;
			}
		}
	}

	Actor->SetFName(FName(NewName));
	RenameBuffer[0] = '\0';
	return true;
}
