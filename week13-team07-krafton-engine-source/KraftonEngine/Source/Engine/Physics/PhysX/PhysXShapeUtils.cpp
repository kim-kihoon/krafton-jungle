#include "Physics/PhysX/PhysXShapeUtils.h"

#include "Component/PrimitiveComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "Object/Object.h"

#include <PxPhysicsAPI.h>

namespace PhysXShapeUtils
{
	physx::PxQuat GetCapsuleAxisCorrectionQuat()
	{
		// Rotate PhysX +X capsule axis to engine +Z (right-handed, Z-up).
		return physx::PxQuat(-physx::PxHalfPi, physx::PxVec3(0.0f, 1.0f, 0.0f));
	}

	void SetupFilterData(physx::PxShape* Shape, UPrimitiveComponent* Comp)
	{
		if (!Shape || !Comp)
		{
			return;
		}

		physx::PxFilterData Filter;
		Filter.word0 = static_cast<physx::PxU32>(Comp->GetCollisionObjectType());
		Filter.word1 = 0;
		Filter.word2 = 0;
		AActor* Owner = Comp->GetOwner();
		Filter.word3 = Owner ? Owner->GetUUID() : 0;

		for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
		{
			const ECollisionResponse Response = Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Ch));
			if (Response == ECollisionResponse::Block)
			{
				Filter.word1 |= (1u << Ch);
			}
			if (Response == ECollisionResponse::Overlap)
			{
				Filter.word2 |= (1u << Ch);
			}
		}

		// Non-simulated physics shapes are drivable ground (BoxComponent floor, static meshes).
		// Simulated chassis / props are excluded so suspension rays hit the floor only.
		if (Comp->IsPhysicsCollisionEnabled() && !Comp->GetSimulatePhysics())
		{
			Filter.word2 |= static_cast<physx::PxU32>(VehicleDrivableQueryBit);
		}

		Shape->setSimulationFilterData(Filter);
		Shape->setQueryFilterData(Filter);
	}

	bool ShouldUseTriggerShape(UPrimitiveComponent* Comp)
	{
		if (!Comp)
		{
			return false;
		}
		if (Comp->GetGenerateOverlapEvents())
		{
			return true;
		}

		for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
		{
			if (Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Ch)) == ECollisionResponse::Block)
			{
				return false;
			}
		}
		return true;
	}

	void FinalizeShape(physx::PxShape* Shape, UPrimitiveComponent* Comp)
	{
		if (!Shape)
		{
			return;
		}

		SetupFilterData(Shape, Comp);
		if (ShouldUseTriggerShape(Comp))
		{
			Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
			Shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
		}
		Shape->userData = Comp;
	}
}
