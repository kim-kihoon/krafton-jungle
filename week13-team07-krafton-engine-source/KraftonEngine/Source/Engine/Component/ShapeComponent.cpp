// Copyright Epic Games, Inc. All Rights Reserved.
#include "ShapeComponent.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"
#include "Render/Proxy/ShapeSceneProxy.h"
#include "Core/Types/CollisionTypes.h"

#include <cstring>

HIDE_FROM_COMPONENT_LIST(UShapeComponent)

UShapeComponent::UShapeComponent()
{
	bCastShadow = false;

	// Blocking physics primitive defaults (ABoxActor / editor place-actor friendly).
	// Triggers use ATriggerVolumeBase or manual Overlap + GenerateOverlapEvents.
	CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	ObjectType = ECollisionChannel::WorldDynamic;
	ResponseContainer.SetAllChannels(ECollisionResponse::Block);
	bGenerateOverlapEvents = false;
	bSimulatePhysics = true;
	bEnableGravity = true;
}

FPrimitiveSceneProxy* UShapeComponent::CreateSceneProxy()
{
	return new FShapeSceneProxy(this);
}

void UShapeComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "ShapeColor") == 0 || strcmp(PropertyName, "bDrawOnlyIfSelected") == 0
		|| strcmp(PropertyName, "Shape Color") == 0 || strcmp(PropertyName, "Draw Only If Selected") == 0)
	{
		MarkRenderStateDirty();
	}
}