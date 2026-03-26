#include "EditorSceneManager.h"
#include "ImGui/imgui.h"
#include "World.h"
#include "Component/CameraComponent.h"
#include "Logger.h"
#include "Editor/Editor.h"
#include <algorithm>

EditorSceneManager::EditorSceneManager(Editor* editor)
{
	this->editor = editor;
}

EditorSceneManager::~EditorSceneManager()
{
}

void EditorSceneManager::Draw()
{
	EditorPanelVisibility& visibility = GetEditorPanelVisibility();
	if (!visibility.bShowSceneManager)
		return;

	const EditorLayout layout = GetEditorLayout(visibility);
	const ImGuiWindowFlags windowFlags = ApplyFixedWindow(layout.SceneManager);
	ImGui::Begin("Scene Manager", nullptr, windowFlags);

	ImGuiIO& io = ImGui::GetIO();
	const bool bCtrlDown = io.KeyCtrl;
	const bool bShiftDown = io.KeyShift;

	if (ImGui::TreeNodeEx("Primitives", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (USceneComponent* s : GetWorld().GetActiveScene()->SceneComponents)
		{
			ImGui::PushID(s);
			
			if (ImGui::Checkbox("## Visibility", &s->bIsVisible))
			{
				if (UPrimitiveComponent* prim = Cast<UPrimitiveComponent>(s))
				{
					GetWorld().bTextLabelDirty = true;
					prim->MarkRenderStateDirty();
				}

				if (!s->bIsVisible && editor->IsComponentSelected(s))
				{
					editor->RemoveSelection(s);
				}
			}

			ImGui::SameLine();
			
			ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

			if (editor->IsComponentSelected(s))
				nodeFlags |= ImGuiTreeNodeFlags_Selected;

			bool isOpen = ImGui::TreeNodeEx((void*)s, nodeFlags, "%s", s->Name.ToString().c_str());
			(void)isOpen;

			if (ImGui::IsItemClicked())
			{
				if (bShiftDown && AnchorComponent)
				{
					auto& components = GetWorld().GetActiveScene()->SceneComponents;
					int anchorIdx = -1, targetIdx = -1;
					for (int i = 0; i < (int)components.size(); ++i)
					{
						if (components[i] == AnchorComponent) anchorIdx = i;
						if (components[i] == s) targetIdx = i;
					}
					if (anchorIdx != -1 && targetIdx != -1)
					{
						editor->ClearSelection();
						int lo = (std::min)(anchorIdx, targetIdx);
						int hi = (std::max)(anchorIdx, targetIdx);
						for (int i = lo; i <= hi; ++i)
						{
							editor->AddSelection(components[i], i == targetIdx);
						}
					}
				}
				else
				{
					editor->SelectComponent(s, bCtrlDown, false);
					AnchorComponent = s;
				}
			}

			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				UCameraComponent* Camera = UCameraComponent::GetMainCamera();
				if (Camera && s)
				{
					const FVector TargetLocation = s->GetRelativeLocation();
					const FVector Forward = Camera->Direction.GetSafeNormal();

					float Distance = s->GetRelativeScale3D().Length() * 5.0f;
					if (Distance < 10.0f)
						Distance = 10.0f;

					FVector NewCameraLocation = TargetLocation - Forward * Distance;
					Camera->SetRelativeLocation(NewCameraLocation);
				}
			}
			
			ImGui::PopID();
		}
		ImGui::TreePop();
	}

	ImGui::End();
}
