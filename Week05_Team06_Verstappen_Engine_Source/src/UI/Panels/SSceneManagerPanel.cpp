#include <UI/Panels/SSceneManagerPanel.h>
#include <Scene/SceneManager.h>
#include <Scene/SceneData.h>
#include <imgui.h>

namespace UI
{
    bool SSceneManagerPanel::Initialize(const FEditorContext& InContext)
    {
        (void)InContext;
        return true;
    }

    void SSceneManagerPanel::Update(const FEditorContext& InContext, float InDeltaTime)
    {
        (void)InContext;
        (void)InDeltaTime;
    }

    void SSceneManagerPanel::Draw(const FEditorContext& InContext)
    {
        if (!ImGui::Begin(GetPanelName()))
        {
            ImGui::End();
            return;
        }

        if (!InContext.Dependencies || !InContext.Dependencies->SceneManager)
        {
            ImGui::TextUnformatted("Scene manager unavailable.");
            ImGui::End();
            return;
        }

        Scene::USceneManager* SceneManager = InContext.Dependencies->SceneManager;
        Scene::FSceneDataSOA* SceneData = SceneManager->GetSceneData();
        const Scene::FSceneSelectionData& Selection = SceneManager->GetSelectionData();
        const uint32_t ObjectCount = SceneManager->GetObjectCount();

        ImGui::Text("Actors (%u)", ObjectCount);
        ImGui::Text("Selected: %u", Selection.SelectionCount);
        ImGui::Separator();

        if (ImGui::BeginChild("##SceneObjectList"))
        {
            ImGuiListClipper Clipper;
            Clipper.Begin(static_cast<int>(ObjectCount));
            while (Clipper.Step())
            {
                for (int RowIndex = Clipper.DisplayStart; RowIndex < Clipper.DisplayEnd; ++RowIndex)
                {
                    const uint32_t ObjectIndex = static_cast<uint32_t>(RowIndex);
                    const bool bSelected = SceneManager->IsObjectSelected(ObjectIndex);
                    char Label[96] = {};
                    std::snprintf(
                        Label,
                        sizeof(Label),
                        "Actor %u (Slot %u)  [Mesh %u, Material %u]",
                        SceneData ? SceneData->ObjectIDs[ObjectIndex] : ObjectIndex,
                        ObjectIndex,
                        SceneData ? SceneData->BaseMeshIDs[ObjectIndex] : 0u,
                        SceneData ? SceneData->MaterialIDs[ObjectIndex] : 0u);

                    if (ImGui::Selectable(Label, bSelected))
                    {
                        if (ImGui::GetIO().KeyCtrl)
                        {
                            SceneManager->ToggleObjectSelection(ObjectIndex);
                        }
                        else
                        {
                            SceneManager->SelectObject(ObjectIndex);
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }

    EEditorPanelType SSceneManagerPanel::GetPanelType() const
    {
        return EEditorPanelType::SceneManager;
    }

    const char* SSceneManagerPanel::GetPanelName() const
    {
        return "Scene Outliner";
    }
}
