#include "Component/LuaCameraModifierComponent.h"

#include "Object/ObjectFactory.h"

DEFINE_CLASS(ULuaCameraModifierComponent, UActorComponent)
REGISTER_FACTORY(ULuaCameraModifierComponent)

void ULuaCameraModifierComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	Ar << "ScriptPath" << ScriptPath; 
}

void ULuaCameraModifierComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Script Path", EPropertyType::String, &ScriptPath });
}

void ULuaCameraModifierComponent::PostDuplicate(UObject* Original)
{
	UActorComponent::PostDuplicate(Original);

	if (const ULuaCameraModifierComponent* OriginalComponent = Cast<ULuaCameraModifierComponent>(Original))
	{
		ScriptPath = OriginalComponent->ScriptPath;
	}
}

void ULuaCameraModifierComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);
}

void ULuaCameraModifierComponent::SetScriptPath(const FString& InScriptPath)
{
	ScriptPath = InScriptPath;
}
