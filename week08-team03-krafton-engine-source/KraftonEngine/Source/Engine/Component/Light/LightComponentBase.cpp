#include "LightComponentBase.h"
#include "Serialization/Archive.h"
#include "Object/ObjectFactory.h"
#include "Engine/Runtime/Engine.h"
#include "Editor/EditorEngine.h"

IMPLEMENT_ABSTRACT_CLASS(ULightComponentBase, USceneComponent)

namespace
{
	void NotifyEditorLightPreviewChanged(ULightComponentBase* LightComponent)
	{
		UEditorEngine* EditorEngine = Cast<UEditorEngine>(GEngine);
		if (!EditorEngine || !LightComponent)
		{
			return;
		}

		EditorEngine->NotifyLightComponentChanged(LightComponent);
	}
}

void ULightComponentBase::OnTransformDirty()
{
	USceneComponent::OnTransformDirty();
	PushToScene();
	NotifyEditorLightPreviewChanged(this);
}

void ULightComponentBase::PostEditProperty(const char* PropertyName)
{
	USceneComponent::PostEditProperty(PropertyName);
	PushToScene();
	NotifyEditorLightPreviewChanged(this);
}

void ULightComponentBase::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	USceneComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Intensity",EPropertyType::Float,&Intensity,0.0f,50.f,0.05f });
	OutProps.push_back({ "Color",EPropertyType::Color4,&LightColor });
	OutProps.push_back({ "Visible",EPropertyType::Bool,&bVisible });
}

void ULightComponentBase::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar << Intensity;
	Ar << LightColor;
	Ar << bVisible;
	Ar << bCastShadow;
}
