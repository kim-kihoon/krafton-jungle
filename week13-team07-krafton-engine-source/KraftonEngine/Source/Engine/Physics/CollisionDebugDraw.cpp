#include "Physics/CollisionDebugDraw.h"

#include "Physics/CollisionWireUtils.h"
#include "Physics/BodySetup/BodySetup.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsBodyDebug.h"
#include "Physics/PhysX/PhysXShapeUtils.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Primitive/SkinnedMeshComponent.h"
#include "Component/ShapeComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Object/Object.h"
#include "Physics/PhysicsAsset.h"
#include "Render/Geometry/LineGeometry.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Types/FrameContext.h"
#include "Math/Matrix.h"
#include "Math/Rotator.h"

#include <PxPhysicsAPI.h>

#include <cmath>

namespace
{
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

	void ComposeWorldPose(const FVector& ParentLocation, const FQuat& ParentRotation, const FVector& LocalLocation,
		const FQuat& LocalRotation, FVector& OutWorldLocation, FQuat& OutWorldRotation)
	{
		OutWorldLocation = ParentLocation + ParentRotation.RotateVector(LocalLocation);
		OutWorldRotation = (ParentRotation * LocalRotation).GetNormalized();
	}

	FQuat ExtractRotationIgnoringScale(const FMatrix& Matrix)
	{
		constexpr float MatrixDecomposeTolerance = 1.0e-6f;

		const FVector Scale = Matrix.GetScale();
		FMatrix RotationMatrix = Matrix;
		RotationMatrix.M[3][0] = 0.0f;
		RotationMatrix.M[3][1] = 0.0f;
		RotationMatrix.M[3][2] = 0.0f;
		RotationMatrix.M[3][3] = 1.0f;

		if (std::fabs(Scale.X) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[0][0] /= Scale.X;
			RotationMatrix.M[0][1] /= Scale.X;
			RotationMatrix.M[0][2] /= Scale.X;
		}

		if (std::fabs(Scale.Y) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[1][0] /= Scale.Y;
			RotationMatrix.M[1][1] /= Scale.Y;
			RotationMatrix.M[1][2] /= Scale.Y;
		}

		if (std::fabs(Scale.Z) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[2][0] /= Scale.Z;
			RotationMatrix.M[2][1] /= Scale.Z;
			RotationMatrix.M[2][2] /= Scale.Z;
		}

		return RotationMatrix.ToQuat().GetNormalized();
	}

	FVector4 GetCollisionWireColor(const UPrimitiveComponent* Component)
	{
		if (const UShapeComponent* Shape = Cast<UShapeComponent>(Component))
		{
			return Shape->GetShapeColorVec4();
		}

		return FVector4(0.9f, 0.55f, 0.1f, 1.0f);
	}

	bool ShouldDrawComponentCollision(const UPrimitiveComponent* Component)
	{
		if (!IsValid(Component))
		{
			return false;
		}

		if (!Component->IsQueryCollisionEnabled() && !Component->IsPhysicsCollisionEnabled())
		{
			return false;
		}

		if (const UShapeComponent* Shape = Cast<UShapeComponent>(Component))
		{
			if (Shape->IsDrawOnlyIfSelected())
			{
				const FPrimitiveSceneProxy* Proxy = Component->GetSceneProxy();
				if (!Proxy || !Proxy->IsSelected())
				{
					return false;
				}
			}
		}

		return true;
	}

	void EmitWireLines(FLineGeometry& OutLines, const TArray<FWireLine>& Lines, const FVector4& Color)
	{
		for (const FWireLine& Line : Lines)
		{
			OutLines.AddLine(Line.Start, Line.End, Color);
		}
	}

	void AppendAggGeomLines(TArray<FWireLine>& OutLines, const FKAggregateGeom& AggGeom, const FMatrix& ComponentWorldMatrix,
		const FVector& ComponentScale)
	{
		const FVector AbsWorldScale(AbsScale(ComponentScale.X), AbsScale(ComponentScale.Y), AbsScale(ComponentScale.Z));
		const float UniformScale = MaxAbsScale(ComponentScale);
		const FVector ComponentLocation = ComponentWorldMatrix.GetLocation();
		const FQuat ComponentRotation = ExtractRotationIgnoringScale(ComponentWorldMatrix);

		const physx::PxQuat PxCorrection = PhysXShapeUtils::GetCapsuleAxisCorrectionQuat();
		const FQuat CapsuleAxisCorrectionF(PxCorrection.x, PxCorrection.y, PxCorrection.z, PxCorrection.w);

		for (const FKBoxElem& Elem : AggGeom.BoxElems)
		{
			const FVector HalfExtents(
				(std::max)(0.001f, Elem.X * 0.5f * AbsWorldScale.X),
				(std::max)(0.001f, Elem.Y * 0.5f * AbsWorldScale.Y),
				(std::max)(0.001f, Elem.Z * 0.5f * AbsWorldScale.Z));
			FVector ElemWorldLocation = FVector::ZeroVector;
			FQuat ElemWorldRotation = FQuat::Identity;
			ComposeWorldPose(
				ComponentLocation,
				ComponentRotation,
				ScaleVector(Elem.Center, ComponentScale),
				Elem.Rotation.ToQuaternion(),
				ElemWorldLocation,
				ElemWorldRotation);
			CollisionWireUtils::BuildBoxLines(OutLines, ElemWorldLocation, HalfExtents, ElemWorldRotation);
		}

		for (const FKSphereElem& Elem : AggGeom.SphereElems)
		{
			const float Radius = (std::max)(0.001f, Elem.Radius * UniformScale);
			FVector ElemWorldLocation = FVector::ZeroVector;
			FQuat ElemWorldRotation = FQuat::Identity;
			ComposeWorldPose(
				ComponentLocation,
				ComponentRotation,
				ScaleVector(Elem.Center, ComponentScale),
				FQuat::Identity,
				ElemWorldLocation,
				ElemWorldRotation);
			CollisionWireUtils::BuildSphereLines(OutLines, ElemWorldLocation, Radius);
		}

		for (const FKSphylElem& Elem : AggGeom.SphylElems)
		{
			const float Radius = (std::max)(0.001f, Elem.Radius * UniformScale);
			const float HalfHeight = (std::max)(0.001f, Elem.Length * 0.5f * UniformScale);
			FVector ElemWorldLocation = FVector::ZeroVector;
			FQuat ElemWorldRotation = FQuat::Identity;
			ComposeWorldPose(
				ComponentLocation,
				ComponentRotation,
				ScaleVector(Elem.Center, ComponentScale),
				Elem.Rotation.ToQuaternion() * CapsuleAxisCorrectionF,
				ElemWorldLocation,
				ElemWorldRotation);
			CollisionWireUtils::BuildCapsuleLinesX(OutLines, ElemWorldLocation, Radius, HalfHeight, ElemWorldRotation);
		}
	}

	bool AppendBodyInstanceDebugLines(const FBodyInstance* BodyInstance, TArray<FWireLine>& OutLines)
	{
		if (!BodyInstance || !BodyInstance->IsValidBodyInstance())
		{
			return false;
		}

		FPhysicsBodyDebugInfo BodyInfo;
		BodyInstance->GatherDebugInfo(BodyInfo);
		if (!BodyInfo.bHasBody || BodyInfo.Shapes.empty())
		{
			return false;
		}

		bool bDrewAny = false;
		for (const FPhysicsShapeDebugInfo& Shape : BodyInfo.Shapes)
		{
			FVector ShapeWorldLocation = FVector::ZeroVector;
			FQuat ShapeWorldRotation = FQuat::Identity;
			ComposeWorldPose(
				BodyInfo.WorldPosition,
				BodyInfo.WorldRotation,
				Shape.LocalPosition,
				Shape.LocalRotation,
				ShapeWorldLocation,
				ShapeWorldRotation);

			switch (Shape.ShapeType)
			{
			case EPhysicsDebugShapeType::Box:
				CollisionWireUtils::BuildBoxLines(OutLines, ShapeWorldLocation, Shape.BoxHalfExtents, ShapeWorldRotation);
				bDrewAny = true;
				break;
			case EPhysicsDebugShapeType::Sphere:
				CollisionWireUtils::BuildSphereLines(OutLines, ShapeWorldLocation, Shape.SphereRadius);
				bDrewAny = true;
				break;
			case EPhysicsDebugShapeType::Capsule:
				CollisionWireUtils::BuildCapsuleLinesX(
					OutLines,
					ShapeWorldLocation,
					Shape.CapsuleRadius,
					Shape.CapsuleHalfHeight + Shape.CapsuleRadius,
					ShapeWorldRotation);
				bDrewAny = true;
				break;
			default:
				break;
			}
		}

		return bDrewAny;
	}

	bool ShouldUseRuntimePhysicsBodyDebug(const USkeletalMeshComponent* SkelComp)
	{
		const UWorld* World = SkelComp ? SkelComp->GetWorld() : nullptr;
		if (!World)
		{
			return false;
		}

		const EWorldType WorldType = World->GetWorldType();
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	void AppendComponentShapeLines(TArray<FWireLine>& OutLines, const UPrimitiveComponent* Component, const FMatrix& ComponentWorldMatrix)
	{
		const FVector ComponentLocation = ComponentWorldMatrix.GetLocation();
		const FQuat ComponentRotation = ComponentWorldMatrix.ToQuat();

		if (const UBoxComponent* Box = Cast<UBoxComponent>(Component))
		{
			CollisionWireUtils::BuildBoxLines(OutLines, ComponentLocation, Box->GetScaledBoxExtent(), ComponentRotation);
		}
		else if (const USphereComponent* Sphere = Cast<USphereComponent>(Component))
		{
			CollisionWireUtils::BuildSphereLines(OutLines, ComponentLocation, Sphere->GetScaledSphereRadius());
		}
		else if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Component))
		{
			CollisionWireUtils::BuildCapsuleLinesZ(
				OutLines,
				ComponentLocation,
				Capsule->GetScaledCapsuleRadius(),
				Capsule->GetScaledCapsuleHalfHeight(),
				ComponentRotation);
		}
	}

	int32 FindBoneIndex(const FSkeletalMesh* Asset, FName BoneName)
	{
		if (!Asset || BoneName.IsNone())
		{
			return -1;
		}

		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (FName(Asset->Bones[BoneIndex].Name) == BoneName)
			{
				return BoneIndex;
			}
		}

		return -1;
	}

	// Runtime worlds prefer physics bodies; editor worlds use asset data at the current component/bone pose.
	bool AppendSkeletalPhysicsAssetLines(USkeletalMeshComponent* SkelComp, TArray<FWireLine>& OutLines)
	{
		UPhysicsAsset* PhysicsAsset = SkelComp->GetPhysicsAsset();
		USkeletalMesh* SkelMesh = SkelComp->GetSkeletalMesh();
		FSkeletalMesh* Asset = SkelMesh ? SkelMesh->GetSkeletalMeshAsset() : nullptr;
		if (!PhysicsAsset || !Asset)
		{
			return false;
		}

		TArray<FMatrix> BoneGlobalMatrices;
		SkelComp->GetCurrentBoneGlobalMatrices(BoneGlobalMatrices);

		const FMatrix ComponentWorldMatrix = SkelComp->GetWorldMatrix();
		const FVector ComponentScale = SkelComp->GetWorldScale();
		const bool bUseRuntimeBodyDebug = ShouldUseRuntimePhysicsBodyDebug(SkelComp);
		bool bDrewAny = false;

		for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->GetBodySetupCount(); ++BodyIndex)
		{
			const USkeletalBodySetup* BodySetup = PhysicsAsset->GetBodySetup(BodyIndex);
			if (!BodySetup || !BodySetup->HasSimpleCollision())
			{
				continue;
			}

			if (bUseRuntimeBodyDebug && AppendBodyInstanceDebugLines(SkelComp->GetBodyInstance(BodyIndex), OutLines))
			{
				bDrewAny = true;
				continue;
			}

			FMatrix BodyWorldMatrix = ComponentWorldMatrix;
			const int32 BoneIndex = FindBoneIndex(Asset, BodySetup->GetBoneName());
			if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(BoneGlobalMatrices.size()))
			{
				BodyWorldMatrix = BoneGlobalMatrices[BoneIndex] * ComponentWorldMatrix;
			}

			AppendAggGeomLines(OutLines, BodySetup->GetAggGeom(), BodyWorldMatrix, ComponentScale);
			bDrewAny = true;
		}

		return bDrewAny;
	}

	void AppendPrimitiveCollisionLines(UPrimitiveComponent* Component, FLineGeometry& OutLines)
	{
		if (!ShouldDrawComponentCollision(Component))
		{
			return;
		}

		const FVector4 WireColor = GetCollisionWireColor(Component);
		const FMatrix ComponentWorldMatrix = Component->GetWorldMatrix();
		const FVector ComponentScale = Component->GetWorldScale();
		TArray<FWireLine> Lines;

		// UE viewport "Collision": simple collision primitives only — not PhysX debug shapes.
		if (Cast<UShapeComponent>(Component))
		{
			AppendComponentShapeLines(Lines, Component, ComponentWorldMatrix);
			EmitWireLines(OutLines, Lines, WireColor);
			return;
		}

		if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Component))
		{
			if (UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh())
			{
				if (UBodySetup* BodySetup = StaticMesh->GetBodySetup())
				{
					if (BodySetup->HasSimpleCollision())
					{
						AppendAggGeomLines(Lines, BodySetup->GetAggGeom(), ComponentWorldMatrix, ComponentScale);
						EmitWireLines(OutLines, Lines, WireColor);
						return;
					}
				}
			}
		}

		if (USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component))
		{
			if (AppendSkeletalPhysicsAssetLines(SkelComp, Lines))
			{
				EmitWireLines(OutLines, Lines, WireColor);
			}
		}
	}
}

void CollisionDebugDraw::AppendCollisionWireframes(UWorld* World, const FFrameContext& Frame, FLineGeometry& OutLines)
{
	if (!World || !Frame.RenderOptions.ShowFlags.bCollision)
	{
		return;
	}

	for (AActor* Actor : World->GetActors())
	{
		if (!IsValid(Actor))
		{
			continue;
		}

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Comp);
			if (!Prim)
			{
				continue;
			}

			AppendPrimitiveCollisionLines(Prim, OutLines);
		}
	}
}
