#include "ActorComponent.h"
#include "Object/ObjectFactory.h"

DEFINE_CLASS(UActorComponent, UObject)
REGISTER_FACTORY(UActorComponent)

void UActorComponent::BeginPlay()
{
    if (bHasBegunPlay)
    {
        return;
    }

    bHasBegunPlay = true;
    if (bAutoActivate)
    {
        Activate();
    }
}

void UActorComponent::EndPlay()
{
    bHasBegunPlay = false;
}

void UActorComponent::Activate()
{
    bCanEverTick = true;
}

void UActorComponent::Deactivate()
{
    bCanEverTick = false;
}

void UActorComponent::ExecuteTick(float DeltaTime)
{
    if (bCanEverTick == false || bIsActive == false)
    {
        return;
    }

    TickComponent(DeltaTime);
}

void UActorComponent::SetActive(bool bNewActive)
{
    if (bNewActive == bIsActive)
    {
        return;
    }

    bIsActive = bNewActive;

    if (bIsActive)
    {
        Activate();
    }
    else
    {
        Deactivate();
    }
}

void UActorComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    // OutProps.push_back({"Active", EPropertyType::Bool, &bIsActive});
    // OutProps.push_back({"Auto Activate", EPropertyType::Bool, &bAutoActivate});
    OutProps.push_back({"Enable Tick", EPropertyType::Bool, &bCanEverTick});
    OutProps.push_back({"Editor Only", EPropertyType::Bool, &bIsEditorOnly});
	// Hidden In Editor 값은 프로퍼티 창에 노출하지 않음
}

void UActorComponent::Serialize(FArchive& Ar)
{
	UObject::Serialize(Ar);
	Ar << "EnableTick" << bCanEverTick;
	Ar << "EditorOnly" << bIsEditorOnly;
	Ar << "HiddenInEditor" << bHiddenInEditor;
}
