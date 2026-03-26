#include "SceneComponent.h"
#include "ImGui/imgui.h"
#include "Editor/Editor.h"

USceneComponent::USceneComponent()
{
	RelativeLocation = FVector(0.0f, 0.0f, 0.0f);
	RelativeRotation = FRotator(0.0f, 0.0f, 0.0f);
	RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	RelativeQuaternion = FQuat::FromEuler(RelativeRotation);
}

USceneComponent::~USceneComponent()
{

}

FMatrix USceneComponent::GetRelativeMatrix()
{
	FMatrix S = FMatrix::ScaleMatrix(RelativeScale3D.x, RelativeScale3D.y, RelativeScale3D.z);
	FMatrix R = RelativeQuaternion.ToMatrix();
	FMatrix T = FMatrix::TranslationMatrix(RelativeLocation.x, RelativeLocation.y, RelativeLocation.z);

	return S * R * T;
}

json::JSON USceneComponent::Serialize()
{
	json::JSON jsonObj;
	jsonObj["Name"] = Name.GetDisplayName();
	jsonObj["NameNumber"] = Name.Number;
	jsonObj["Location"] = RelativeLocation.Serialize();
	jsonObj["Rotation"] = RelativeRotation.Serialize();
	jsonObj["Scale"] = RelativeScale3D.Serialize();
	return jsonObj;
}

void USceneComponent::Deserialize(json::JSON jsonObj)
{
	Name = FName(jsonObj["Name"].ToString().c_str(), jsonObj["NameNumber"].ToInt());
	RelativeLocation.Deserialize(jsonObj["Location"]);
	RelativeRotation.Deserialize(jsonObj["Rotation"]);
	RelativeScale3D.Deserialize(jsonObj["Scale"]);
}

void USceneComponent::DrawProperties()
{
	FRotator eulerRotation = RelativeRotation;

	ImGui::Text(Name.ToString().c_str());

	if (ImGui::DragFloat3("Location", &RelativeLocation.x))
	{
		SetRelativeLocation(RelativeLocation);
	}
	if (ImGui::DragFloat3("Rotation", &eulerRotation.Roll))
	{
		SetRelativeRotation(eulerRotation);
		UpdateQuaternionFromRotation();
	}
	if (ImGui::DragFloat3("Scale", &RelativeScale3D.x))
	{
		SetRelativeScale3D(RelativeScale3D);
	}
}

bool USceneComponent::IsSelected() const
{
	return bIsSelected;
}

REGISTER_CLASS(USceneComponent);