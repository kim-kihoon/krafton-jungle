#include "CollisionSystem.h"

#include "Collision/Collision.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Geometry/AABB.h"
#include "Geometry/OBB.h"
#include "Math/Utils.h"
#include "Core/Logger.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Spatial/WorldSpatialIndex.h"

namespace
{
	const char* GetCollisionTypeName(ECollisionType Type)
	{
		switch (Type)
		{
		case ECollisionType::Box:
			return "Box";
		case ECollisionType::Sphere:
			return "Sphere";
		case ECollisionType::Capsule:
			return "Capsule";
		case ECollisionType::Cylinder:
			return "Cylinder";
		default:
			return "None";
		}
	}

	FString GetActorLogName(const AActor* Actor)
	{
		return Actor ? Actor->GetName() : FString("None");
	}

	bool HasCollisionResponse(const UPrimitiveComponent* Component)
	{
		return Component != nullptr && (Component->IsGenerateOverlapEvents() || Component->IsBlockComponent());
	}

	URigidBodyComponent* FindSimulatingRigidBody(AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component);
			if (Body != nullptr && ((Body->IsDynamicBody() && Body->IsSimulatingPhysics()) || Body->IsHeldByPhysicsHandle()))
			{
				return Body;
			}
		}

		return nullptr;
	}

	FHitResult MakeReverseHit(const FHitResult& Hit, UPrimitiveComponent* HitComponent)
	{
		FHitResult Result = Hit;
		Result.HitComponent = HitComponent;
		Result.Normal = Hit.Normal * -1.0f;
		Result.bHit = (HitComponent != nullptr);
		return Result;
	}

	bool TryMakeAABBSeparation(const FAABB& A, const FAABB& B, FVector& OutNormal, float& OutDepth)
	{
		const float OverlapX = std::min(A.Max.X, B.Max.X) - std::max(A.Min.X, B.Min.X);
		const float OverlapY = std::min(A.Max.Y, B.Max.Y) - std::max(A.Min.Y, B.Min.Y);
		const float OverlapZ = std::min(A.Max.Z, B.Max.Z) - std::max(A.Min.Z, B.Min.Z);
		if (OverlapX <= 0.0f || OverlapY <= 0.0f || OverlapZ <= 0.0f)
		{
			return false;
		}

		const FVector Delta = B.GetCenter() - A.GetCenter();
		OutDepth = OverlapX;
		OutNormal = FVector((Delta.X >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);

		if (OverlapY < OutDepth)
		{
			OutDepth = OverlapY;
			OutNormal = FVector(0.0f, (Delta.Y >= 0.0f) ? 1.0f : -1.0f, 0.0f);
		}

		if (OverlapZ < OutDepth)
		{
			OutDepth = OverlapZ;
			OutNormal = FVector(0.0f, 0.0f, (Delta.Z >= 0.0f) ? 1.0f : -1.0f);
		}

		return true;
	}

	FOBB MakeBoxOBB(const UBoxComponent* Box)
	{
		if (Box == nullptr)
		{
			return {};
		}

		const FVector Extent = Box->GetBoxExtent();
		const FVector Scale = Box->GetWorldScale();
		return FOBB(
			Box->GetWorldLocation(),
			FVector(
				MathUtil::Abs(Extent.X) * MathUtil::Abs(Scale.X),
				MathUtil::Abs(Extent.Y) * MathUtil::Abs(Scale.Y),
				MathUtil::Abs(Extent.Z) * MathUtil::Abs(Scale.Z)),
			Box->GetWorldMatrix().GetRotationMatrix());
	}

	void ProjectOBBOnAxis(const TArray<FVector>& Vertices, const FVector& Axis, float& OutMin, float& OutMax)
	{
		OutMin = FLT_MAX;
		OutMax = -FLT_MAX;

		for (const FVector& Vertex : Vertices)
		{
			const float Projection = FVector::DotProduct(Vertex, Axis);
			OutMin = std::min(OutMin, Projection);
			OutMax = std::max(OutMax, Projection);
		}
	}

	bool TestSeparationAxis(
		const TArray<FVector>& VerticesA,
		const TArray<FVector>& VerticesB,
		const FVector& CenterDelta,
		FVector Axis,
		FVector& InOutBestNormal,
		float& InOutBestDepth)
	{
		if (Axis.SizeSquared() < 1e-8f)
		{
			return true;
		}

		Axis.NormalizeSafe();

		float MinA;
		float MaxA;
		float MinB;
		float MaxB;
		ProjectOBBOnAxis(VerticesA, Axis, MinA, MaxA);
		ProjectOBBOnAxis(VerticesB, Axis, MinB, MaxB);

		const float Overlap = std::min(MaxA, MaxB) - std::max(MinA, MinB);
		if (Overlap <= 0.0f)
		{
			return false;
		}

		if (Overlap < InOutBestDepth)
		{
			InOutBestDepth = Overlap;
			InOutBestNormal = FVector::DotProduct(CenterDelta, Axis) >= 0.0f ? Axis : Axis * -1.0f;
		}

		return true;
	}

	bool TryMakeBoxSeparation(const UBoxComponent* A, const UBoxComponent* B, FVector& OutNormal, float& OutDepth)
	{
		if (A == nullptr || B == nullptr)
		{
			return false;
		}

		const FOBB BoxA = MakeBoxOBB(A);
		const FOBB BoxB = MakeBoxOBB(B);

		TArray<FVector> VerticesA;
		TArray<FVector> VerticesB;
		BoxA.GetVertices(VerticesA);
		BoxB.GetVertices(VerticesB);

		FVector AxesA[3];
		FVector AxesB[3];
		BoxA.GetAxes(AxesA[0], AxesA[1], AxesA[2]);
		BoxB.GetAxes(AxesB[0], AxesB[1], AxesB[2]);

		const FVector CenterDelta = BoxB.Center - BoxA.Center;
		OutDepth = FLT_MAX;
		OutNormal = FVector::ZeroVector;

		for (const FVector& Axis : AxesA)
		{
			if (!TestSeparationAxis(VerticesA, VerticesB, CenterDelta, Axis, OutNormal, OutDepth))
			{
				return false;
			}
		}

		for (const FVector& Axis : AxesB)
		{
			if (!TestSeparationAxis(VerticesA, VerticesB, CenterDelta, Axis, OutNormal, OutDepth))
			{
				return false;
			}
		}

		for (const FVector& AxisA : AxesA)
		{
			for (const FVector& AxisB : AxesB)
			{
				if (!TestSeparationAxis(VerticesA, VerticesB, CenterDelta, FVector::CrossProduct(AxisA, AxisB), OutNormal, OutDepth))
				{
					return false;
				}
			}
		}

		return !OutNormal.IsNearlyZero() && OutDepth < FLT_MAX;
	}

	bool TryMakeShapeSeparation(UPrimitiveComponent* A, UPrimitiveComponent* B, FVector& OutNormal, float& OutDepth)
	{
		if (A != nullptr && B != nullptr &&
			A->GetCollisionType() == ECollisionType::Box &&
			B->GetCollisionType() == ECollisionType::Box)
		{
			return TryMakeBoxSeparation(static_cast<UBoxComponent*>(A), static_cast<UBoxComponent*>(B), OutNormal, OutDepth);
		}

		return TryMakeAABBSeparation(A->GetWorldAABB(), B->GetWorldAABB(), OutNormal, OutDepth);
	}
}

void FCollisionSystem::UpdateWorldCollision(UWorld* World)
{
	if (World == nullptr)
	{
		return;
	}

	TArray<FCollisionCandidate> Candidates;
	GatherCandidates(World, Candidates);

	for (const FCollisionCandidate& Candidate : Candidates)
	{
		ProcessBroadCollision(World, Candidate);
	}

	ClearStaleCollisions(Candidates);
}

// 월드에서 충돌 처리가 필요한 ShapeComponent 후보를 수집합니다.
void FCollisionSystem::GatherCandidates(UWorld* World, TArray<FCollisionCandidate>& OutCandidates)
{
	OutCandidates.clear();
	if (World == nullptr)
	{
		return;
	}

	FWorldSpatialIndex& SpatialIndex = World->GetSpatialIndex();

	for (AActor* Actor : World->GetActors())
	{
		if (Actor == nullptr || !Actor->IsActive())
		{
			continue;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (Primitive == nullptr || Cast<UShapeComponent>(Primitive) == nullptr)
			{
				continue;
			}

			if (Primitive->GetCollisionType() == ECollisionType::None || !HasCollisionResponse(Primitive))
			{
				continue;
			}

			const int32 ObjectIndex = SpatialIndex.FindObjectIndex(Primitive);
			if (ObjectIndex == FBVH::INDEX_NONE)
			{
				continue;
			}

			OutCandidates.push_back({ Actor, Primitive, ObjectIndex });
		}
	}
}

// BVH AABB Query를 통해 후보 하나와 충돌 가능성이 있는 Broad Phase 후보들을 찾습니다.
void FCollisionSystem::ProcessBroadCollision(UWorld* World, const FCollisionCandidate& Candidate)
{
	if (World == nullptr || Candidate.Component == nullptr)
	{
		return;
	}

	FWorldSpatialIndex& SpatialIndex = World->GetSpatialIndex();
	FWorldSpatialIndex::FPrimitiveAABBQueryScratch Scratch;
	TArray<UPrimitiveComponent*> BroadCandidates;
	SpatialIndex.AABBQueryPrimitives(Candidate.Component->GetWorldAABB(), BroadCandidates, Scratch);

	for (UPrimitiveComponent* OtherComponent : BroadCandidates)
	{
		if (OtherComponent == nullptr || OtherComponent == Candidate.Component)
		{
			continue;
		}

		const int32 OtherObjectIndex = SpatialIndex.FindObjectIndex(OtherComponent);
		if (OtherObjectIndex == FBVH::INDEX_NONE || Candidate.ObjectIndex >= OtherObjectIndex)
		{
			continue;
		}

		AActor* OtherActor = OtherComponent->GetOwner();
		if (OtherActor == nullptr || OtherActor == Candidate.Actor)
		{
			continue;
		}

		if (Cast<UShapeComponent>(OtherComponent) == nullptr || OtherComponent->GetCollisionType() == ECollisionType::None)
		{
			continue;
		}

		if (!HasCollisionResponse(OtherComponent))
		{
			continue;
		}

		ProcessNarrowCollision(Candidate, { OtherActor, OtherComponent, OtherObjectIndex });
	}
}

// Broad Phase를 통과한 두 ShapeComponent에 대해 실제 도형 기반 Narrow Phase 충돌을 판정합니다.
void FCollisionSystem::ProcessNarrowCollision(const FCollisionCandidate& A, const FCollisionCandidate& B)
{
	if (A.Component == nullptr || B.Component == nullptr || A.Actor == B.Actor)
	{
		return;
	}

	FHitResult Hit;
	const bool bIsOverlapping = FCollision::TestOverlap(A.Component, B.Component, &Hit);

	if (!bIsOverlapping)
	{
		return;
	}

	const bool bShouldBlock = A.Component->IsBlockComponent() || B.Component->IsBlockComponent();
	const bool bWasAOverlapping = A.Component->HasOverlapInfo(B.Actor, B.Component);
	const bool bWasBOverlapping = B.Component->HasOverlapInfo(A.Actor, A.Component);
	const bool bWasABlocking = A.Component->HasBlockingInfo(B.Actor, B.Component);
	const bool bWasBBlocking = B.Component->HasBlockingInfo(A.Actor, A.Component);

	if (bShouldBlock)
	{
		ProcessBlocking(A.Component, B.Component);

		if (bWasAOverlapping)
		{
			FOverlapResult EndOverlapInfo{ B.Actor, B.Component };
			A.Component->RemoveOverlapInfo(B.Actor, B.Component);
			A.Component->OnComponentEndOverlap.Broadcast(EndOverlapInfo);
		}

		if (bWasBOverlapping)
		{
			FOverlapResult EndOverlapInfo{ A.Actor, A.Component };
			B.Component->RemoveOverlapInfo(A.Actor, A.Component);
			B.Component->OnComponentEndOverlap.Broadcast(EndOverlapInfo);
		}

		if (!bWasABlocking)
		{
			A.Component->AddBlockingInfo(B.Actor, B.Component, Hit);
			A.Component->OnComponentHit.Broadcast(Hit);
		}

		if (!bWasBBlocking)
		{
			const FHitResult ReverseHit = MakeReverseHit(Hit, A.Component);
			B.Component->AddBlockingInfo(A.Actor, A.Component, ReverseHit);
			B.Component->OnComponentHit.Broadcast(ReverseHit);
		}

		return;
	}

	if (bWasABlocking)
	{
		A.Component->RemoveBlockingInfo(B.Actor, B.Component);
	}

	if (bWasBBlocking)
	{
		B.Component->RemoveBlockingInfo(A.Actor, A.Component);
	}

	if (A.Component->IsGenerateOverlapEvents() && !bWasAOverlapping)
	{
		FOverlapResult BeginOverlapInfo{ B.Actor, B.Component };
		A.Component->AddOverlapInfo(B.Actor, B.Component);
		A.Component->OnComponentBeginOverlap.Broadcast(BeginOverlapInfo);
	}

	if (B.Component->IsGenerateOverlapEvents() && !bWasBOverlapping)
	{
		FOverlapResult BeginOverlapInfo{ A.Actor, A.Component };
		B.Component->AddOverlapInfo(A.Actor, A.Component);
		B.Component->OnComponentBeginOverlap.Broadcast(BeginOverlapInfo);
	}
}

void FCollisionSystem::ProcessBlocking(UPrimitiveComponent* A, UPrimitiveComponent* B)
{
	if (A == nullptr || B == nullptr)
	{
		return;
	}

	FVector Normal;
	float Depth = 0.0f;
	if (!TryMakeShapeSeparation(A, B, Normal, Depth))
	{
		return;
	}

	constexpr float PushOutEpsilon = 0.005f;
	const float PushDistance = Depth + PushOutEpsilon;
	const bool bABlocks = A->IsBlockComponent();
	const bool bBBlocks = B->IsBlockComponent();
	AActor* OwnerA = A->GetOwner();
	AActor* OwnerB = B->GetOwner();
	URigidBodyComponent* BodyA = FindSimulatingRigidBody(OwnerA);
	URigidBodyComponent* BodyB = FindSimulatingRigidBody(OwnerB);
	if ((BodyA != nullptr && BodyA->IsUsingJoltPhysics()) ||
		(BodyB != nullptr && BodyB->IsUsingJoltPhysics()))
	{
		return;
	}

	const bool bAMovable = BodyA != nullptr;
	const bool bBMovable = BodyB != nullptr;

	if (bABlocks && bBBlocks)
	{
		if (bAMovable && bBMovable)
		{
			const FVector PushA = Normal * (-PushDistance * 0.5f);
			const FVector PushB = Normal * ( PushDistance * 0.5f);
			if (OwnerA)
			{
				OwnerA->AddActorWorldOffset(PushA);
				BodyA->NotifyBlockingPushOut(PushA);
			}
			if (OwnerB)
			{
				OwnerB->AddActorWorldOffset(PushB);
				BodyB->NotifyBlockingPushOut(PushB);
			}
			return;
		}

		if (bAMovable && OwnerA)
		{
			const FVector Push = Normal * -PushDistance;
			OwnerA->AddActorWorldOffset(Push);
			BodyA->NotifyBlockingPushOut(Push);
			return;
		}

		if (bBMovable && OwnerB)
		{
			const FVector Push = Normal * PushDistance;
			OwnerB->AddActorWorldOffset(Push);
			BodyB->NotifyBlockingPushOut(Push);
		}
		return;
	}

	if (bABlocks && bBMovable)
	{
		if (OwnerB)
		{
			const FVector Push = Normal * PushDistance;
			OwnerB->AddActorWorldOffset(Push);
			BodyB->NotifyBlockingPushOut(Push);
		}
		return;
	}

	if (bBBlocks && bAMovable)
	{
		if (OwnerA)
		{
			const FVector Push = Normal * -PushDistance;
			OwnerA->AddActorWorldOffset(Push);
			BodyA->NotifyBlockingPushOut(Push);
		}
	}
}

void FCollisionSystem::ClearStaleCollisions(const TArray<FCollisionCandidate>& Candidates)
{
	for (const FCollisionCandidate& C : Candidates)
	{
		UPrimitiveComponent* Comp = C.Component;
		if (Comp == nullptr)
			continue;

		TArray<FOverlapResult> StaleOverlaps;
		for (const FOverlapResult& Info : Comp->GetOverlapInfos())
		{
			if (Info.OtherComp == nullptr || !FCollision::TestOverlap(Comp, Info.OtherComp))
				StaleOverlaps.push_back(Info);
		}
		for (const FOverlapResult& Stale : StaleOverlaps)
		{
			Comp->RemoveOverlapInfo(Stale.OtherActor, Stale.OtherComp);
			Comp->OnComponentEndOverlap.Broadcast(Stale);
		}

		TArray<FBlockingResult> StaleBlockings;
		for (const FBlockingResult& Info : Comp->GetBlockingInfos())
		{
			if (Info.OtherComp == nullptr || !FCollision::TestOverlap(Comp, Info.OtherComp))
				StaleBlockings.push_back(Info);
		}
		for (const FBlockingResult& Stale : StaleBlockings)
		{
			Comp->RemoveBlockingInfo(Stale.OtherActor, Stale.OtherComp);
		}
	}
}
