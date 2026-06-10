#include "BoneDebugComponent.h"
#include "Object/GarbageCollection.h"

#include "Object/Reflection/ObjectFactory.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Render/Proxy/BoneDebugSceneProxy.h"

UBoneDebugComponent::UBoneDebugComponent()
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

UBoneDebugComponent::~UBoneDebugComponent()
{
}

FPrimitiveSceneProxy* UBoneDebugComponent::CreateSceneProxy()
{
	return new FBoneDebugSceneProxy(this);
}

void UBoneDebugComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	UPrimitiveComponent::AddReferencedObjects(Collector);
}
