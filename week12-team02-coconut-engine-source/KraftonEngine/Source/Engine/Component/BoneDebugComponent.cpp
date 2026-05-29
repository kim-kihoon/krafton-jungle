#include "BoneDebugComponent.h"

#include "SkeletalMeshComponent.h"
#include "Render/Proxy/BoneDebugSceneProxy.h"

UBoneDebugComponent::UBoneDebugComponent()
{
}

UBoneDebugComponent::~UBoneDebugComponent()
{
}

FPrimitiveSceneProxy* UBoneDebugComponent::CreateSceneProxy()
{
	return new FBoneDebugSceneProxy(this);
}
