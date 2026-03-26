#include "EditorPropertyPanel.h"
#include "Editor.h"
#include "ImGui/imgui.h"
#include "Component/PrimitiveComponent.h"	
#include "Component/SceneComponent.h"
#include "Component/ParticleSubUVComponent.h"
#include "World.h"
#include <windows.h>
#include <commdlg.h>

bool OpenImageFileDialog(wchar_t* outPath, DWORD outPathCount)
{
    OPENFILENAMEW ofn = {};
    outPath[0] = L'\0';

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = outPath;
    ofn.nMaxFile = outPathCount;
    ofn.lpstrFilter =
        L"Image Files\0*.png;*.jpg;*.jpeg;\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    return GetOpenFileNameW(&ofn) == TRUE;
}

EditorPropertyPanel::EditorPropertyPanel(Editor* parent) : editor(parent)
{
}

EditorPropertyPanel::~EditorPropertyPanel()
{
}

void EditorPropertyPanel::DrawMultiTransformProperties(USceneComponent* selectedComponent, const TArray<USceneComponent*>& selectedComponents)
{
	if (!selectedComponent)
	{
		return;
	}

	FVector location = selectedComponent->GetRelativeLocation();
	if (ImGui::DragFloat3("Location", &location.x))
	{
		const FVector delta = location - selectedComponent->GetRelativeLocation();
		for (USceneComponent* component : selectedComponents)
		{
			component->SetRelativeLocation(component->GetRelativeLocation() + delta);
		}
	}

	FRotator rotation = selectedComponent->GetRelativeRotation();
	if (ImGui::DragFloat3("Rotation", &rotation.Roll))
	{
		const FRotator delta = rotation - selectedComponent->GetRelativeRotation();
		for (USceneComponent* component : selectedComponents)
		{
			FRotator nextRotation = component->GetRelativeRotation() + delta;
			component->SetRelativeRotation(nextRotation);
			component->UpdateQuaternionFromRotation();
		}
	}

	FVector scale = selectedComponent->GetRelativeScale3D();
	if (ImGui::DragFloat3("Scale", &scale.x))
	{
		const FVector delta = scale - selectedComponent->GetRelativeScale3D();
		for (USceneComponent* component : selectedComponents)
		{
			component->SetRelativeScale3D(component->GetRelativeScale3D() + delta);
		}
	}
}

void EditorPropertyPanel::Draw(USceneComponent* selectedComponent, const TArray<USceneComponent*>& selectedComponents)
{
    EditorPanelVisibility& visibility = GetEditorPanelVisibility();
    if (!visibility.bShowPropertyPanel)
        return;

    const EditorLayout layout = GetEditorLayout(visibility);
    const ImGuiWindowFlags windowFlags = ApplyFixedWindow(layout.PropertyPanel);
    ImGui::Begin("Jungle Property Panel", nullptr, windowFlags);

    if (selectedComponent != CachedComponent)
    {
        CachedComponent = selectedComponent;

        memset(TextBuffer, 0, sizeof(TextBuffer));
        memset(ImagePathBuffer, 0, sizeof(ImagePathBuffer));

        if (selectedComponent)
        {
            if (selectedComponent->IsA(UParticleSubUVComp::GetClass()))
            {
                UParticleSubUVComp* particle = Cast<UParticleSubUVComp>(selectedComponent);
                size_t convertedChars = 0;
                wcstombs_s(&convertedChars, ImagePathBuffer, sizeof(ImagePathBuffer),
                    particle->GetTexturePath(), _TRUNCATE);
            }
        }
    }

    if (selectedComponent)
    {
        ImGui::PushID(selectedComponent);

        if (selectedComponents.size() > 1)
        {
            ImGui::Text("Primary: %s", selectedComponent->Name.ToString().c_str());
            ImGui::Text("Selected: %d", static_cast<int>(selectedComponents.size()));
            DrawMultiTransformProperties(selectedComponent, selectedComponents);
            ImGui::Separator();
            ImGui::Text("Custom properties are shown for the last selected component.");
        }
        else
        {
            selectedComponent->DrawProperties();
        }

        if (selectedComponent->IsA(UParticleSubUVComp::GetClass()))
        {
            UParticleSubUVComp* particle = Cast<UParticleSubUVComp>(selectedComponent);
            int32 Size[3] = { particle->Columns, particle->Rows, particle->PlayRate };
            ImGui::DragInt3("Col/Row/PlayRate", Size, 1.0f, 1);
            if (Size[0] != particle->Columns) particle->Columns = (std::max)(Size[0], 1);
            if (Size[1] != particle->Rows) particle->Rows = (std::max)(Size[1], 1);
            if (Size[2] != particle->PlayRate) particle->PlayRate = (std::max)(Size[2], 1);

            if (ImGui::Button("Select Image##Particle", { 100, 30 }))
            {
                wchar_t selectedPath[1000] = {};

                if (OpenImageFileDialog(selectedPath, _countof(selectedPath)))
                {
                    particle->SetTexture(selectedPath);
                }
            }
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &(particle->bLoop));
        }

        if (ImGui::Button("Delete##Component", { 100, 30 }))
        {
            RemoveSelected(selectedComponents);
        }

        ImGui::PopID();
    }

    ImGui::End();
}

void EditorPropertyPanel::RemoveSelected(const TArray<USceneComponent*>& selectedComponents)
{
	TArray<USceneComponent*> componentsToDelete = selectedComponents;
	editor->ClearSelection();

	for (USceneComponent* component : componentsToDelete)
	{
		GetWorld().RemoveSceneComponent(component);
	}
}
