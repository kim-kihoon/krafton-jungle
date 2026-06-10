#include "Materials/MaterialInstanceDynamic.h"

#include "Object/GarbageCollection.h"
#include "Object/Object.h"

UMaterialInstanceDynamic* UMaterialInstanceDynamic::Create(UMaterial* InParent, UObject* InOwner, const FString& DebugName)
{
    if (!InParent)
    {
        return nullptr;
    }

    UMaterialInstanceDynamic* Instance = UObjectManager::Get().CreateObject<UMaterialInstanceDynamic>();
    Instance->Owner                    = InOwner;

    FString Path = DebugName.empty() ? FString("__dynamic_material_instance__") : DebugName;
    Instance->InitializeFromParent(InParent, Path);
    return Instance;
}

void UMaterialInstanceDynamic::AddReferencedObjects(FReferenceCollector& Collector)
{
    UMaterialInstance::AddReferencedObjects(Collector);
    Collector.AddReferencedObject(Owner, "UMaterialInstanceDynamic::Owner");
}