#include "UStruct.h"
#include "UClass.h"

UClass UStruct::StaticClassInstance(
	"UStruct",
	UObject::StaticClass(),
	sizeof(UStruct),
	CF_None,
	CASTCLASS_UStruct
);

bool UStruct::IsChildOf(const UStruct* Other)
{
	if (Other == nullptr) return false;

	for (const UStruct* TempStruct = this; TempStruct; TempStruct = TempStruct->GetSuperStruct())
	{
		if (TempStruct == Other)
		{
			return true;
		}
	}
	return false;
}

void UStruct::HideInheritedProperty(FString InName)
{
	HiddenProperties.insert(InName);
}

bool UStruct::IsPropertyHidden(FString InName) const
{
	return HiddenProperties.contains(InName);
}

void UStruct::GetEditableProperties(TArray<const FProperty*>& OutProps) const
{
	GetEditablePropertiesFor(OutProps, this);
}

void UStruct::GetNonTransientProperties(TArray<const FProperty*>& OutProps) const
{
	GetNonTransientPropertiesFor(OutProps, this);
}

void UStruct::GetEditablePropertiesFor(TArray<const FProperty*>& OutProps, const UStruct* Target) const
{
	if (SuperStruct) SuperStruct->GetEditablePropertiesFor(OutProps, Target);

	for (uint32 i = 0; i < ChildProperties.size(); i++)
	{
		FProperty* Property = ChildProperties[i];
		if (!Property || Target->IsPropertyHidden(Property->Name)) continue;
		if (Property->PropertyFlag & EPropertyFlags::CPF_Edit) OutProps.push_back(Property);
	}
}

void UStruct::GetNonTransientPropertiesFor(TArray<const FProperty*>& OutProps, const UStruct* Target) const
{
	if (SuperStruct) SuperStruct->GetNonTransientPropertiesFor(OutProps, Target);

	for (uint32 i = 0; i < ChildProperties.size(); i++)
	{
		FProperty* Property = ChildProperties[i];
		if (!Property || Target->IsPropertyHidden(Property->Name)) continue;
		if ((Property->PropertyFlag & EPropertyFlags::CPF_Transient) == 0) OutProps.push_back(Property);
	}
}
