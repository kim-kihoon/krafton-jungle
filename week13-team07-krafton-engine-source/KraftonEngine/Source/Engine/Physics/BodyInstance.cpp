#include "Physics/BodyInstance.h"

#include "Physics/PhysicsBodyDebug.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/SceneComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "GameFramework/AActor.h"
#include "Math/MathUtils.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Physics/BodySetup/BodySetup.h"
#include "Physics/PhysX/PhysXShapeUtils.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>

using namespace physx;

namespace
{
	PxVec3 ToPxVec3(const FVector& V)
	{
		return PxVec3(V.X, V.Y, V.Z);
	}

	PxQuat ToPxQuat(const FQuat& Q)
	{
		return PxQuat(Q.X, Q.Y, Q.Z, Q.W);
	}

	FVector ToFVector(const PxVec3& V)
	{
		return FVector(V.x, V.y, V.z);
	}

	FQuat ToFQuat(const PxQuat& Q)
	{
		return FQuat(Q.x, Q.y, Q.z, Q.w);
	}

	PxTransform GetPxTransform(UPrimitiveComponent* Comp)
	{
		if (!Comp)
		{
			return PxTransform(PxIdentity);
		}

		const FVector Pos = Comp->GetWorldLocation();
		const FQuat Rot = Comp->GetWorldMatrix().ToQuat();
		return PxTransform(ToPxVec3(Pos), ToPxQuat(Rot));
	}

	float AbsScale(float Value)
	{
		return std::fabs(Value);
	}

	float MaxAbsScale(const FVector& Scale)
	{
		return (std::max)((std::max)(AbsScale(Scale.X), AbsScale(Scale.Y)), AbsScale(Scale.Z));
	}

	FVector ScaleVector(const FVector& Value, const FVector& Scale)
	{
		return FVector(Value.X * Scale.X, Value.Y * Scale.Y, Value.Z * Scale.Z);
	}

	PxTransform GetElemTransform(const FVector& Center, const FRotator& Rotation, const FVector& Scale)
	{
		return PxTransform(ToPxVec3(ScaleVector(Center, Scale)), ToPxQuat(Rotation.ToQuaternion()));
	}
}

FInitBodySpawnParams::FInitBodySpawnParams(const UPrimitiveComponent* PrimComp)
{
	if (PrimComp)
	{
		bStaticPhysics = !PrimComp->GetSimulatePhysics();
	}
}

namespace
{
	bool ResolveSimulatePhysics(
		const UBodySetup* InBodySetup,
		const UPrimitiveComponent* OwnerComponent,
		const FInitBodySpawnParams& SpawnParams)
	{
		if (SpawnParams.bStaticPhysics)
		{
			return false;
		}

		if (SpawnParams.bPhysicsTypeDeterminesSimulation && InBodySetup)
		{
			switch (InBodySetup->GetPhysicsType())
			{
			case EPhysicsType::Kinematic:
				return false;
			case EPhysicsType::Simulated:
				return true;
			case EPhysicsType::Default:
			default:
				break;
			}
		}

		return OwnerComponent ? OwnerComponent->GetSimulatePhysics() : false;
	}

	bool ResolveEnableCollision(const UBodySetup* InBodySetup, const UPrimitiveComponent* OwnerComponent)
	{
		if (InBodySetup && InBodySetup->GetCollisionResponse() == EBodyCollisionResponse::Disabled)
		{
			return false;
		}

		return OwnerComponent
			? (OwnerComponent->IsQueryCollisionEnabled() || OwnerComponent->IsPhysicsCollisionEnabled())
			: true;
	}
}

void FBodyInstance::InitBody(
	UBodySetup* InBodySetup,
	UPrimitiveComponent* InOwnerComponent,
	const FBodyInstanceInitParams& InitParams,
	const FInitBodySpawnParams& SpawnParams)
{
	TermBody(InitParams);

	BodySetup = InBodySetup;
	OwnerComponent = InOwnerComponent;
	Actor = nullptr;

	if (!OwnerComponent || !InitParams.Physics || !InitParams.Scene)
	{
		return;
	}

	bSimulatePhysics = ResolveSimulatePhysics(BodySetup, OwnerComponent, SpawnParams);
	bEnableCollision = ResolveEnableCollision(BodySetup, OwnerComponent);
	Mass = OwnerComponent->GetMass();

	const PxTransform BodyTransform = GetPxTransform(OwnerComponent);
	Actor = bSimulatePhysics
		? static_cast<PxRigidActor*>(InitParams.Physics->createRigidDynamic(BodyTransform))
		: static_cast<PxRigidActor*>(InitParams.Physics->createRigidStatic(BodyTransform));

	if (!Actor)
	{
		return;
	}

	Actor->userData = this;

	if (bEnableCollision)
	{
		const bool bCreatedComplexStaticMesh = CreateShapesFromStaticMeshComplex(InitParams);
		if (!bCreatedComplexStaticMesh && BodySetup && BodySetup->HasSimpleCollision())
		{
			CreateShapesFromBodySetup(InitParams);
		}
		else if (!bCreatedComplexStaticMesh)
		{
			CreateShapesFromComponent(InitParams);
		}
	}

	if (Actor->getNbShapes() == 0)
	{
		Actor->release();
		Actor = nullptr;
		return;
	}

	if (PxRigidDynamic* DynamicActor = Actor->is<PxRigidDynamic>())
	{
		const float MassKg = Mass > 0.0f ? Mass : 1.0f;
		PxRigidBodyExt::setMassAndUpdateInertia(*DynamicActor, MassKg);
		DynamicActor->setCMassLocalPose(PxTransform(ToPxVec3(OwnerComponent->GetCenterOfMass())));
		DynamicActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !OwnerComponent->GetEnableGravity());
	}

	InitParams.Scene->addActor(*Actor);

	if (PxRigidDynamic* DynamicActor = Actor->is<PxRigidDynamic>())
	{
		DynamicActor->wakeUp();
	}
}

void FBodyInstance::TermBody(const FBodyInstanceInitParams& InitParams)
{
	if (!Actor)
	{
		for (PxTriangleMesh* TriangleMesh : RuntimeTriangleMeshes)
		{
			if (TriangleMesh)
			{
				TriangleMesh->release();
			}
		}
		RuntimeTriangleMeshes.clear();
		return;
	}

	Actor->userData = nullptr;
	if (InitParams.Scene)
	{
		InitParams.Scene->removeActor(*Actor);
	}
	Actor->release();
	Actor = nullptr;
	for (PxTriangleMesh* TriangleMesh : RuntimeTriangleMeshes)
	{
		if (TriangleMesh)
		{
			TriangleMesh->release();
		}
	}
	RuntimeTriangleMeshes.clear();
}

bool FBodyInstance::CreateShapesFromStaticMeshComplex(const FBodyInstanceInitParams& InitParams)
{
	if (!Actor || !OwnerComponent || !InitParams.Physics || !InitParams.DefaultMaterial || !bEnableCollision || bSimulatePhysics)
	{
		return false;
	}

	UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(OwnerComponent);
	if (!StaticMeshComp)
	{
		return false;
	}

	if (BodySetup && BodySetup->GetCollisionTraceFlag() == ECollisionTraceFlag::UseSimpleAsComplex)
	{
		return false;
	}

	UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();
	FStaticMesh* MeshAsset = StaticMesh ? StaticMesh->GetStaticMeshAsset() : nullptr;
	if (!MeshAsset || MeshAsset->Vertices.empty() || MeshAsset->Indices.size() < 3)
	{
		return false;
	}

	const uint32 TriangleCount = static_cast<uint32>(MeshAsset->Indices.size() / 3);
	if (TriangleCount == 0)
	{
		return false;
	}

	TArray<PxVec3> Points;
	Points.reserve(MeshAsset->Vertices.size());
	for (const FNormalVertex& Vertex : MeshAsset->Vertices)
	{
		Points.push_back(ToPxVec3(Vertex.pos));
	}

	PxTriangleMeshDesc MeshDesc;
	MeshDesc.points.count = static_cast<PxU32>(Points.size());
	MeshDesc.points.stride = sizeof(PxVec3);
	MeshDesc.points.data = Points.data();
	MeshDesc.triangles.count = static_cast<PxU32>(TriangleCount);
	MeshDesc.triangles.stride = sizeof(uint32) * 3;
	MeshDesc.triangles.data = MeshAsset->Indices.data();

	PxCookingParams CookingParams{ PxTolerancesScale() };
	PxCooking* Cooking = PxCreateCooking(PX_PHYSICS_VERSION, InitParams.Physics->getFoundation(), CookingParams);
	if (!Cooking)
	{
		return false;
	}

	PxTriangleMeshCookingResult::Enum CookResult = PxTriangleMeshCookingResult::eFAILURE;
	PxTriangleMesh* TriangleMesh = Cooking->createTriangleMesh(
		MeshDesc,
		InitParams.Physics->getPhysicsInsertionCallback(),
		&CookResult);
	Cooking->release();

	if (!TriangleMesh || CookResult == PxTriangleMeshCookingResult::eFAILURE)
	{
		if (TriangleMesh)
		{
			TriangleMesh->release();
		}
		return false;
	}

	const FVector Scale = OwnerComponent->GetWorldScale();
	const PxVec3 MeshScale(
		(std::max)(0.001f, AbsScale(Scale.X)),
		(std::max)(0.001f, AbsScale(Scale.Y)),
		(std::max)(0.001f, AbsScale(Scale.Z)));
	PxTriangleMeshGeometry Geometry(TriangleMesh, PxMeshScale(MeshScale, PxQuat(PxIdentity)));
	if (!Geometry.isValid())
	{
		TriangleMesh->release();
		return false;
	}

	PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Actor, Geometry, *InitParams.DefaultMaterial);
	if (!Shape)
	{
		TriangleMesh->release();
		return false;
	}

	PhysXShapeUtils::FinalizeShape(Shape, OwnerComponent);
	RuntimeTriangleMeshes.push_back(TriangleMesh);
	return true;
}

void FBodyInstance::CreateShapesFromBodySetup(const FBodyInstanceInitParams& InitParams)
{
	if (!Actor || !BodySetup || !OwnerComponent || !InitParams.DefaultMaterial || !bEnableCollision)
	{
		return;
	}

	const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
	const FVector Scale = OwnerComponent->GetWorldScale();
	const FVector AbsWorldScale(AbsScale(Scale.X), AbsScale(Scale.Y), AbsScale(Scale.Z));
	const float UniformScale = MaxAbsScale(Scale);

	for (const FKBoxElem& Elem : AggGeom.BoxElems)
	{
		const PxVec3 HalfExtents(
			(std::max)(0.001f, Elem.X * 0.5f * AbsWorldScale.X),
			(std::max)(0.001f, Elem.Y * 0.5f * AbsWorldScale.Y),
			(std::max)(0.001f, Elem.Z * 0.5f * AbsWorldScale.Z));

		PxShape* Shape = PxRigidActorExt::createExclusiveShape(
			*Actor,
			PxBoxGeometry(HalfExtents),
			*InitParams.DefaultMaterial);
		if (!Shape)
		{
			continue;
		}

		Shape->setLocalPose(GetElemTransform(Elem.Center, Elem.Rotation, Scale));
		PhysXShapeUtils::FinalizeShape(Shape, OwnerComponent);
	}

	for (const FKSphereElem& Elem : AggGeom.SphereElems)
	{
		const float Radius = (std::max)(0.001f, Elem.Radius * UniformScale);
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(
			*Actor,
			PxSphereGeometry(Radius),
			*InitParams.DefaultMaterial);
		if (!Shape)
		{
			continue;
		}

		Shape->setLocalPose(PxTransform(ToPxVec3(ScaleVector(Elem.Center, Scale)), PxQuat(PxIdentity)));
		PhysXShapeUtils::FinalizeShape(Shape, OwnerComponent);
	}

	for (const FKSphylElem& Elem : AggGeom.SphylElems)
	{
		const float Radius = (std::max)(0.001f, Elem.Radius * UniformScale);
		const float HalfLength = (std::max)(0.001f, Elem.Length * 0.5f * UniformScale);
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(
			*Actor,
			PxCapsuleGeometry(Radius, HalfLength),
			*InitParams.DefaultMaterial);
		if (!Shape)
		{
			continue;
		}

		PxTransform LocalPose = GetElemTransform(Elem.Center, Elem.Rotation, Scale);
		LocalPose.q = LocalPose.q * PhysXShapeUtils::GetCapsuleAxisCorrectionQuat();
		Shape->setLocalPose(LocalPose);
		PhysXShapeUtils::FinalizeShape(Shape, OwnerComponent);
	}
}

void FBodyInstance::CreateShapesFromComponent(const FBodyInstanceInitParams& InitParams)
{
	if (!Actor || !OwnerComponent || !InitParams.DefaultMaterial || !bEnableCollision)
	{
		return;
	}

	PxQuat ShapeAxisRot = PxQuat(PxIdentity);
	PxGeometryHolder Geom;
	bool bHasGeom = false;

	if (const UBoxComponent* Box = Cast<UBoxComponent>(OwnerComponent))
	{
		const FVector Ext = Box->GetScaledBoxExtent();
		Geom = PxBoxGeometry(
			(std::max)(0.001f, Ext.X),
			(std::max)(0.001f, Ext.Y),
			(std::max)(0.001f, Ext.Z));
		bHasGeom = true;
	}
	else if (const USphereComponent* Sphere = Cast<USphereComponent>(OwnerComponent))
	{
		Geom = PxSphereGeometry((std::max)(0.001f, Sphere->GetScaledSphereRadius()));
		bHasGeom = true;
	}
	else if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(OwnerComponent))
	{
		const float Radius = (std::max)(0.001f, Capsule->GetScaledCapsuleRadius());
		const float HalfHeight = (std::max)(0.001f, Capsule->GetScaledCapsuleHalfHeight());
		Geom = PxCapsuleGeometry(Radius, HalfHeight - Radius);
		ShapeAxisRot = PhysXShapeUtils::GetCapsuleAxisCorrectionQuat();
		bHasGeom = true;
	}

	if (!bHasGeom)
	{
		return;
	}

	PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Actor, Geom.any(), *InitParams.DefaultMaterial);
	if (!Shape)
	{
		return;
	}

	PxTransform LocalPose(PxIdentity);
	LocalPose.q = ShapeAxisRot;
	Shape->setLocalPose(LocalPose);
	PhysXShapeUtils::FinalizeShape(Shape, OwnerComponent);
}

void FBodyInstance::SyncBodyToComponent()
{
	if (!Actor || !OwnerComponent)
	{
		return;
	}

	const PxTransform Pose = Actor->getGlobalPose();
	const FVector NewWorldPos = ToFVector(Pose.p);
	const FQuat NewWorldRot = ToFQuat(Pose.q);

	AActor* OwnerActor = OwnerComponent->GetOwner();
	USceneComponent* Root = OwnerActor ? OwnerActor->GetRootComponent() : nullptr;

	// 시뮬레이션 body가 Root가 아닌 자식(ShapeComponent 등)에 있으면, 해당 컴포넌트만
	// SetWorldLocation 하면 부모 StaticMesh는 제자리에 남고 collider만 떨어져 나간다.
	// PhysX pose delta를 Actor Root에 적용해 attach된 비주얼 전체가 함께 이동하게 한다.
	if (IsValid(OwnerActor) && IsValid(Root) && OwnerComponent != Root)
	{
		const FVector OldWorldPos = OwnerComponent->GetWorldLocation();
		const FQuat OldWorldRot = OwnerComponent->GetWorldMatrix().ToQuat();

		const FVector PosDelta = NewWorldPos - OldWorldPos;
		if (!PosDelta.IsNearlyZero())
		{
			OwnerActor->AddActorWorldOffset(PosDelta);
		}

		const FQuat RotDelta = (NewWorldRot * OldWorldRot.Inverse()).GetNormalized();
		if (std::abs(RotDelta.W) < 0.999f)
		{
			const FVector Pivot = NewWorldPos;
			const FVector RootPos = Root->GetWorldLocation();
			Root->SetWorldLocation(Pivot + RotDelta.RotateVector(RootPos - Pivot));

			const FQuat RootWorldRot = Root->GetWorldMatrix().ToQuat();
			Root->SetRelativeRotation((RotDelta * RootWorldRot).GetNormalized());
		}
		return;
	}

	OwnerComponent->SetWorldLocation(NewWorldPos);
	if (IsValid(Root) && OwnerComponent == Root)
	{
		Root->SetRelativeRotation(NewWorldRot);
	}
	else
	{
		OwnerComponent->SetRelativeRotation(NewWorldRot.ToRotator());
	}
}

void FBodyInstance::SyncComponentToBody()
{
	if (!Actor || !OwnerComponent)
	{
		return;
	}

	Actor->setGlobalPose(GetPxTransform(OwnerComponent));
}

void FBodyInstance::GatherDebugInfo(FPhysicsBodyDebugInfo& OutInfo) const
{
	OutInfo = FPhysicsBodyDebugInfo{};
	OutInfo.OwnerComponent = OwnerComponent;
	OutInfo.bSimulatePhysics = bSimulatePhysics;

	if (!Actor)
	{
		return;
	}

	OutInfo.PhysXActor = Actor;
	OutInfo.bHasBody = true;

	const PxTransform GlobalPose = Actor->getGlobalPose();
	OutInfo.WorldPosition = ToFVector(GlobalPose.p);
	OutInfo.WorldRotation = ToFQuat(GlobalPose.q);

	if (Actor->is<PxRigidStatic>())
	{
		OutInfo.bIsStatic = true;
	}

	if (PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>())
	{
		OutInfo.bIsDynamic = true;
		OutInfo.bIsKinematic = Dynamic->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC);
	}

	const PxU32 NumShapes = Actor->getNbShapes();
	if (NumShapes == 0)
	{
		return;
	}

	TArray<PxShape*> ShapeBuffer;
	ShapeBuffer.resize(static_cast<size_t>(NumShapes));
	const PxU32 Fetched = Actor->getShapes(ShapeBuffer.data(), NumShapes);
	OutInfo.Shapes.reserve(static_cast<size_t>(Fetched));

	for (PxU32 ShapeIndex = 0; ShapeIndex < Fetched; ++ShapeIndex)
	{
		PxShape* Shape = ShapeBuffer[ShapeIndex];
		if (!Shape)
		{
			continue;
		}

		FPhysicsShapeDebugInfo ShapeInfo;
		const PxTransform LocalPose = Shape->getLocalPose();
		ShapeInfo.LocalPosition = ToFVector(LocalPose.p);
		ShapeInfo.LocalRotation = ToFQuat(LocalPose.q);

		switch (Shape->getGeometryType())
		{
		case PxGeometryType::eBOX:
		{
			PxBoxGeometry Geom;
			if (Shape->getBoxGeometry(Geom))
			{
				ShapeInfo.ShapeType = EPhysicsDebugShapeType::Box;
				ShapeInfo.BoxHalfExtents = ToFVector(Geom.halfExtents);
			}
			break;
		}
		case PxGeometryType::eSPHERE:
		{
			PxSphereGeometry Geom;
			if (Shape->getSphereGeometry(Geom))
			{
				ShapeInfo.ShapeType = EPhysicsDebugShapeType::Sphere;
				ShapeInfo.SphereRadius = Geom.radius;
			}
			break;
		}
		case PxGeometryType::eCAPSULE:
		{
			PxCapsuleGeometry Geom;
			if (Shape->getCapsuleGeometry(Geom))
			{
				ShapeInfo.ShapeType = EPhysicsDebugShapeType::Capsule;
				ShapeInfo.CapsuleRadius = Geom.radius;
				ShapeInfo.CapsuleHalfHeight = Geom.halfHeight;
			}
			break;
		}
		default:
			ShapeInfo.ShapeType = EPhysicsDebugShapeType::Unknown;
			break;
		}

		OutInfo.Shapes.push_back(ShapeInfo);
	}
}
