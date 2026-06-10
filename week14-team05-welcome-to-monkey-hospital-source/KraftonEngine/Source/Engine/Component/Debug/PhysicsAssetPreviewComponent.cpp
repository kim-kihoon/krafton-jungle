#include "PhysicsAssetPreviewComponent.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Math/MathUtils.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetPreviewUtils.h"
#include "Render/Proxy/PhysicsAssetPreviewSceneProxy.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>

namespace
{
	constexpr float MinShapeSize = 0.001f;
	constexpr int32 SphereSlices = 24;
	constexpr int32 SphereStacks = 12;
	constexpr int32 CapsuleSlices = 24;
	constexpr int32 CapsuleHemisphereStacks = 6;
		constexpr int32 ConstraintLimitCircleSegments = 32;
		constexpr int32 ConstraintLimitArcSegments = 24;
		constexpr int32 PhysicsPreviewHitFaceBodyStride = 10000;
		constexpr int32 PhysicsPreviewHitFaceConstraintBase = 100000000;

	FTransform ComposePreviewDebugTransforms(const FTransform& ParentWorld, const FTransform& Local)
	{
		FTransform Result = Local;
		Result.Location = ParentWorld.Location + ParentWorld.Rotation.RotateVector(Local.Location);
		Result.Rotation = (ParentWorld.Rotation * Local.Rotation).GetNormalized();
		Result.Scale = FVector::OneVector;
		return Result;
	}

	FVector4 PhysicsPreviewShapeColor(bool bSelectedBody, bool bSelectedShape)
	{
		if (bSelectedShape)
		{
			return FVector4(1.0f, 0.96f, 0.47f, 0.50f);
		}

		if (bSelectedBody)
		{
			return FVector4(1.0f, 0.58f, 0.18f, 0.35f);
		}

		return FVector4(0.35f, 0.82f, 1.0f, 0.22f);
	}

	FVector4 ConstraintLimitColorFromHex(uint8 R, uint8 G, uint8 B, float Alpha)
	{
		return FVector4(
			static_cast<float>(R) / 255.0f,
			static_cast<float>(G) / 255.0f,
			static_cast<float>(B) / 255.0f,
			Alpha);
	}

	FVector4 PhysicsPreviewSwingLimitColor(bool bSelectedConstraint)
	{
		return bSelectedConstraint
			? ConstraintLimitColorFromHex(0xff, 0xcd, 0x37, 0.66f)
			: ConstraintLimitColorFromHex(0xc5, 0x00, 0x00, 0.42f);
	}

	FVector4 PhysicsPreviewTwistLimitColor(bool bSelectedConstraint)
	{
		return bSelectedConstraint
			? ConstraintLimitColorFromHex(0x50, 0xdc, 0xff, 0.66f)
			: ConstraintLimitColorFromHex(0x00, 0x80, 0x20, 0.42f);
	}

	float ClampLimitDegreesForPreview(float Degrees)
	{
		return FMath::Clamp(Degrees, 0.0f, 85.0f);
	}

	float MotionLimitDegreesForPreview(EConstraintMotion Motion, float LimitedDegrees)
	{
		switch (Motion)
		{
		case EConstraintMotion::Free:
			return 85.0f;
		case EConstraintMotion::Limited:
			return ClampLimitDegreesForPreview(LimitedDegrees);
		case EConstraintMotion::Locked:
		default:
			return 0.0f;
		}
	}

	int32 ConstraintArcSegments(float AngleRangeRadians)
	{
		const float Normalized = (std::max)(std::fabs(AngleRangeRadians) / (2.0f * FMath::Pi), 0.08f);
		const int32 SegmentCount = static_cast<int32>(ceilf(static_cast<float>(ConstraintLimitArcSegments) * Normalized));
		return (std::min)((std::max)(SegmentCount, 4), ConstraintLimitArcSegments);
	}

		float ComputeConstraintLimitSurfaceRadius(const FTransform& ParentFrameWorld, const FTransform& ChildFrameWorld)
		{
			const float FrameDistance = FVector::Distance(ParentFrameWorld.Location, ChildFrameWorld.Location);
			return FMath::Clamp(FrameDistance * 0.25f + 0.15f, 0.15f, 1.2f);
		}

		float ComputeConstraintPickTolerance(float Radius)
		{
			return (std::max)(Radius * 0.08f, 0.03f);
		}

		float DistanceRayToSegment(const FRay& Ray, const FVector& A, const FVector& B, float& OutRayT)
		{
			const FVector U = Ray.Direction;
			const FVector V = B - A;
			const FVector W0 = Ray.Origin - A;
			const float ACoef = U.Dot(U);
			const float BCoef = U.Dot(V);
			const float CCoef = V.Dot(V);
			const float DCoef = U.Dot(W0);
			const float ECoef = V.Dot(W0);
			const float Denom = ACoef * CCoef - BCoef * BCoef;

			float RayT = 0.0f;
			float SegmentT = 0.0f;
			if (Denom > 1e-6f)
			{
				RayT = (BCoef * ECoef - CCoef * DCoef) / Denom;
				SegmentT = (ACoef * ECoef - BCoef * DCoef) / Denom;
			}
			else
			{
				RayT = 0.0f;
				SegmentT = CCoef > 1e-6f ? ECoef / CCoef : 0.0f;
			}

			SegmentT = FMath::Clamp(SegmentT, 0.0f, 1.0f);
			RayT = (std::max)(RayT, 0.0f);

			const FVector ClosestRay = Ray.Origin + U * RayT;
			const FVector ClosestSegment = A + V * SegmentT;
			OutRayT = RayT;
			return FVector::Distance(ClosestRay, ClosestSegment);
		}

		void TestConstraintPickSegment(
			const FRay& Ray,
			const FVector& A,
			const FVector& B,
			float Tolerance,
			int32 ConstraintIndex,
			bool& bOutHit,
			float& InOutClosestT,
			int32& OutConstraintIndex)
		{
			float RayT = 0.0f;
			const float Distance = DistanceRayToSegment(Ray, A, B, RayT);
			if (Distance <= Tolerance && RayT < InOutClosestT)
			{
				bOutHit = true;
				InOutClosestT = RayT;
				OutConstraintIndex = ConstraintIndex;
			}
		}

	FVector ClampHalfExtent(FVector HalfExtent)
	{
		HalfExtent.X = (std::max)(HalfExtent.X, MinShapeSize);
		HalfExtent.Y = (std::max)(HalfExtent.Y, MinShapeSize);
		HalfExtent.Z = (std::max)(HalfExtent.Z, MinShapeSize);
		return HalfExtent;
	}


	uint64 HashCombinePreview(uint64 Seed, uint64 Value)
	{
		return Seed ^ (Value + 0x9e3779b97f4a7c15ull + (Seed << 6) + (Seed >> 2));
	}

	uint64 HashFloatPreview(float Value)
	{
		return static_cast<uint64>(std::hash<float>{}(Value));
	}

	uint64 HashNamePreview(const FName& Name)
	{
		return static_cast<uint64>(FName::Hash{}(Name));
	}

	uint64 HashVectorPreview(uint64 Seed, const FVector& Value)
	{
		Seed = HashCombinePreview(Seed, HashFloatPreview(Value.X));
		Seed = HashCombinePreview(Seed, HashFloatPreview(Value.Y));
		Seed = HashCombinePreview(Seed, HashFloatPreview(Value.Z));
		return Seed;
	}

	uint64 HashQuatPreview(uint64 Seed, const FQuat& Value)
	{
		Seed = HashCombinePreview(Seed, HashFloatPreview(Value.X));
		Seed = HashCombinePreview(Seed, HashFloatPreview(Value.Y));
		Seed = HashCombinePreview(Seed, HashFloatPreview(Value.Z));
		Seed = HashCombinePreview(Seed, HashFloatPreview(Value.W));
		return Seed;
	}

	uint64 HashTransformPreview(uint64 Seed, const FTransform& Value)
	{
		Seed = HashVectorPreview(Seed, Value.Location);
		Seed = HashQuatPreview(Seed, Value.Rotation);
		Seed = HashVectorPreview(Seed, Value.Scale);
		return Seed;
	}

	uint64 HashPreviewComponentWorld(const USkeletalMeshComponent* Component)
	{
		if (!Component)
		{
			return 0;
		}

		uint64 Hash = 7809847782465536322ull;
		Hash = HashVectorPreview(Hash, Component->GetWorldLocation());
		Hash = HashQuatPreview(Hash, Component->GetWorldMatrix().ToQuat().GetNormalized());
		return Hash;
	}

	uint64 ComputePhysicsAssetPreviewHash(const UPhysicsAsset* Asset)
	{
		if (!Asset)
		{
			return 0;
		}

		uint64 Hash = 1469598103934665603ull;
		const TArray<FPhysicsAssetBodySetup>& Bodies = Asset->GetBodySetups();
		Hash = HashCombinePreview(Hash, static_cast<uint64>(Bodies.size()));
		for (const FPhysicsAssetBodySetup& Body : Bodies)
		{
			Hash = HashCombinePreview(Hash, HashNamePreview(Body.BoneName));
			Hash = HashTransformPreview(Hash, Body.BodyLocalFrame);
			Hash = HashCombinePreview(Hash, static_cast<uint64>(Body.Shapes.size()));
			for (const FPhysicsAssetShapeSetup& Shape : Body.Shapes)
			{
				Hash = HashCombinePreview(Hash, static_cast<uint64>(Shape.Type));
				Hash = HashTransformPreview(Hash, Shape.LocalTransform);
				Hash = HashVectorPreview(Hash, Shape.BoxHalfExtent);
				Hash = HashCombinePreview(Hash, HashFloatPreview(Shape.SphereRadius));
				Hash = HashCombinePreview(Hash, HashFloatPreview(Shape.CapsuleRadius));
				Hash = HashCombinePreview(Hash, HashFloatPreview(Shape.CapsuleHalfHeight));
			}
		}

		const TArray<FPhysicsAssetConstraintSetup>& Constraints = Asset->GetConstraintSetups();
		Hash = HashCombinePreview(Hash, static_cast<uint64>(Constraints.size()));
		for (const FPhysicsAssetConstraintSetup& Constraint : Constraints)
		{
			Hash = HashCombinePreview(Hash, HashNamePreview(Constraint.ParentBoneName));
			Hash = HashCombinePreview(Hash, HashNamePreview(Constraint.ChildBoneName));
			Hash = HashTransformPreview(Hash, Constraint.ParentLocalFrame);
			Hash = HashTransformPreview(Hash, Constraint.ChildLocalFrame);
			Hash = HashCombinePreview(Hash, static_cast<uint64>(Constraint.Limits.LinearX));
			Hash = HashCombinePreview(Hash, static_cast<uint64>(Constraint.Limits.LinearY));
			Hash = HashCombinePreview(Hash, static_cast<uint64>(Constraint.Limits.LinearZ));
			Hash = HashCombinePreview(Hash, static_cast<uint64>(Constraint.Limits.Twist));
			Hash = HashCombinePreview(Hash, static_cast<uint64>(Constraint.Limits.Swing1));
			Hash = HashCombinePreview(Hash, static_cast<uint64>(Constraint.Limits.Swing2));
			Hash = HashCombinePreview(Hash, HashFloatPreview(Constraint.Limits.TwistLimitMinDegrees));
			Hash = HashCombinePreview(Hash, HashFloatPreview(Constraint.Limits.TwistLimitMaxDegrees));
			Hash = HashCombinePreview(Hash, HashFloatPreview(Constraint.Limits.Swing1LimitDegrees));
			Hash = HashCombinePreview(Hash, HashFloatPreview(Constraint.Limits.Swing2LimitDegrees));
			Hash = HashCombinePreview(Hash, Constraint.Limits.bEnableProjection ? 1ull : 0ull);
			Hash = HashCombinePreview(Hash, Constraint.bDisableCollisionBetweenBodies ? 1ull : 0ull);
		}

		return Hash;
	}

	bool IntersectRayLocalBox(const FRay& LocalRay, const FVector& HalfExtent, float& OutT)
	{
		float TMin = -FLT_MAX;
		float TMax = FLT_MAX;
		const float* Origin = &LocalRay.Origin.X;
		const float* Direction = &LocalRay.Direction.X;
		const float MinBounds[3] = { -HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z };
		const float MaxBounds[3] = {  HalfExtent.X,  HalfExtent.Y,  HalfExtent.Z };

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (std::abs(Direction[Axis]) < 1e-6f)
			{
				if (Origin[Axis] < MinBounds[Axis] || Origin[Axis] > MaxBounds[Axis])
				{
					return false;
				}
				continue;
			}

			float T1 = (MinBounds[Axis] - Origin[Axis]) / Direction[Axis];
			float T2 = (MaxBounds[Axis] - Origin[Axis]) / Direction[Axis];
			if (T1 > T2)
			{
				std::swap(T1, T2);
			}

			TMin = (std::max)(TMin, T1);
			TMax = (std::min)(TMax, T2);
			if (TMin > TMax)
			{
				return false;
			}
		}

		const float T = TMin >= 0.0f ? TMin : TMax;
		if (T < 0.0f)
		{
			return false;
		}

		OutT = T;
		return true;
	}

	bool IntersectRayLocalSphere(const FRay& LocalRay, const FVector& Center, float Radius, float& OutT)
	{
		const FVector O = LocalRay.Origin - Center;
		const FVector D = LocalRay.Direction;
		const float B = O.Dot(D);
		const float C = O.Dot(O) - Radius * Radius;
		const float Discriminant = B * B - C;
		if (Discriminant < 0.0f)
		{
			return false;
		}

		const float SqrtDisc = sqrtf(Discriminant);
		float T = -B - SqrtDisc;
		if (T < 0.0f)
		{
			T = -B + SqrtDisc;
		}
		if (T < 0.0f)
		{
			return false;
		}

		OutT = T;
		return true;
	}

	bool IntersectRayLocalCylinderZ(const FRay& LocalRay, float Radius, float CylinderHalfHeight, float& OutT)
	{
		if (CylinderHalfHeight <= 0.0f)
		{
			return false;
		}

		const float A = LocalRay.Direction.X * LocalRay.Direction.X + LocalRay.Direction.Y * LocalRay.Direction.Y;
		if (A < 1e-6f)
		{
			return false;
		}

		const float B = LocalRay.Origin.X * LocalRay.Direction.X + LocalRay.Origin.Y * LocalRay.Direction.Y;
		const float C = LocalRay.Origin.X * LocalRay.Origin.X + LocalRay.Origin.Y * LocalRay.Origin.Y - Radius * Radius;
		const float Discriminant = B * B - A * C;
		if (Discriminant < 0.0f)
		{
			return false;
		}

		const float SqrtDisc = sqrtf(Discriminant);
		const float Candidates[2] = { (-B - SqrtDisc) / A, (-B + SqrtDisc) / A };
		float BestT = FLT_MAX;
		for (float T : Candidates)
		{
			if (T < 0.0f)
			{
				continue;
			}

			const float Z = LocalRay.Origin.Z + LocalRay.Direction.Z * T;
			if (Z >= -CylinderHalfHeight && Z <= CylinderHalfHeight)
			{
				BestT = (std::min)(BestT, T);
			}
		}

		if (BestT == FLT_MAX)
		{
			return false;
		}

		OutT = BestT;
		return true;
	}

	bool IntersectRayLocalCapsuleZ(const FRay& LocalRay, float Radius, float HalfHeight, float& OutT)
	{
		const float CylinderHalf = (std::max)(0.0f, HalfHeight - Radius);
		float BestT = FLT_MAX;
		float T = 0.0f;

		if (IntersectRayLocalCylinderZ(LocalRay, Radius, CylinderHalf, T))
		{
			BestT = (std::min)(BestT, T);
		}
		if (IntersectRayLocalSphere(LocalRay, FVector(0.0f, 0.0f, CylinderHalf), Radius, T))
		{
			BestT = (std::min)(BestT, T);
		}
		if (IntersectRayLocalSphere(LocalRay, FVector(0.0f, 0.0f, -CylinderHalf), Radius, T))
		{
			BestT = (std::min)(BestT, T);
		}

		if (BestT == FLT_MAX)
		{
			return false;
		}

		OutT = BestT;
		return true;
	}

	FRay MakeShapeLocalRay(const FRay& WorldRay, const FTransform& ShapeWorld)
	{
		FRay LocalRay;
		const FQuat InverseRotation = ShapeWorld.Rotation.GetNormalized().Inverse();
		LocalRay.Origin = InverseRotation.RotateVector(WorldRay.Origin - ShapeWorld.Location);
		LocalRay.Direction = InverseRotation.RotateVector(WorldRay.Direction).Normalized();
		return LocalRay;
	}

}

UPhysicsAssetPreviewComponent::UPhysicsAssetPreviewComponent()
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
}

UPhysicsAssetPreviewComponent::~UPhysicsAssetPreviewComponent()
{
	PreviewMeshBuffer.Release();
}

FPrimitiveSceneProxy* UPhysicsAssetPreviewComponent::CreateSceneProxy()
{
	return new FPhysicsAssetPreviewSceneProxy(this);
}

FMeshBuffer* UPhysicsAssetPreviewComponent::GetMeshBuffer() const
{
	return const_cast<FMeshBuffer*>(&PreviewMeshBuffer);
}

FMeshDataView UPhysicsAssetPreviewComponent::GetMeshDataView() const
{
	return FMeshDataView::FromMeshData(PreviewMeshData);
}

int32 UPhysicsAssetPreviewComponent::EncodeSelectionFaceIndex(int32 BodyIndex, int32 ShapeIndex)
{
	return BodyIndex * PhysicsPreviewHitFaceBodyStride + ShapeIndex;
}

int32 UPhysicsAssetPreviewComponent::EncodeConstraintFaceIndex(int32 ConstraintIndex)
{
	return PhysicsPreviewHitFaceConstraintBase + ConstraintIndex;
}

bool UPhysicsAssetPreviewComponent::DecodeSelectionFaceIndex(int32 FaceIndex, int32& OutBodyIndex, int32& OutShapeIndex)
{
	OutBodyIndex = -1;
	OutShapeIndex = -1;
	if (FaceIndex < 0 || FaceIndex >= PhysicsPreviewHitFaceConstraintBase)
	{
		return false;
	}

	OutBodyIndex = FaceIndex / PhysicsPreviewHitFaceBodyStride;
	OutShapeIndex = FaceIndex % PhysicsPreviewHitFaceBodyStride;
	return OutBodyIndex >= 0 && OutShapeIndex >= 0;
}

bool UPhysicsAssetPreviewComponent::DecodeConstraintFaceIndex(int32 FaceIndex, int32& OutConstraintIndex)
{
	OutConstraintIndex = -1;
	if (FaceIndex < PhysicsPreviewHitFaceConstraintBase)
	{
		return false;
	}

	OutConstraintIndex = FaceIndex - PhysicsPreviewHitFaceConstraintBase;
	return OutConstraintIndex >= 0;
}

bool UPhysicsAssetPreviewComponent::LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult)
{
	UPhysicsAsset* Asset = PhysicsAsset.Get();
	USkeletalMeshComponent* MeshComponent = PreviewComponent.Get();
	if (!Asset || !MeshComponent || !IsVisible())
	{
		return false;
	}

	FRay WorldRay = Ray;
	WorldRay.Direction.Normalize();
	if (WorldRay.Direction.IsNearlyZero())
	{
		return false;
	}

	FPhysicsAssetPreviewPoseCache PoseCache;
	if (!PoseCache.Initialize(MeshComponent, Asset))
	{
		return false;
	}

	bool bShapeHit = false;
	float ClosestShapeT = FLT_MAX;
	int32 HitBodyIndex = -1;
	int32 HitShapeIndex = -1;

	if (bShowBodies)
	{
		const TArray<FPhysicsAssetBodySetup>& Bodies = Asset->GetBodySetups();
		for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
		{
			FTransform BodyWorld;
			if (!PoseCache.ComputeBodyWorldTransform(BodyIndex, BodyWorld))
			{
				continue;
			}

			const FPhysicsAssetBodySetup& Body = Bodies[BodyIndex];
			for (int32 ShapeIndex = 0; ShapeIndex < static_cast<int32>(Body.Shapes.size()); ++ShapeIndex)
			{
				const FPhysicsAssetShapeSetup& Shape = Body.Shapes[ShapeIndex];
				const FTransform ShapeWorld = ComposePreviewDebugTransforms(BodyWorld, Shape.LocalTransform);
				const FRay LocalRay = MakeShapeLocalRay(WorldRay, ShapeWorld);

				float T = 0.0f;
				bool bCurrentShapeHit = false;
				switch (Shape.Type)
				{
				case EPhysicsAssetShapeType::Box:
					bCurrentShapeHit = IntersectRayLocalBox(LocalRay, ClampHalfExtent(Shape.BoxHalfExtent), T);
					break;
				case EPhysicsAssetShapeType::Sphere:
					bCurrentShapeHit = IntersectRayLocalSphere(LocalRay, FVector::ZeroVector, (std::max)(Shape.SphereRadius, MinShapeSize), T);
					break;
				case EPhysicsAssetShapeType::Capsule:
				{
					const float Radius = (std::max)(Shape.CapsuleRadius, MinShapeSize);
					const float HalfHeight = (std::max)(Shape.CapsuleHalfHeight, Radius);
					bCurrentShapeHit = IntersectRayLocalCapsuleZ(LocalRay, Radius, HalfHeight, T);
					break;
				}
				default:
					break;
				}

				if (bCurrentShapeHit && T >= 0.0f && T < ClosestShapeT)
				{
					bShapeHit = true;
					ClosestShapeT = T;
					HitBodyIndex = BodyIndex;
					HitShapeIndex = ShapeIndex;
				}
			}
		}
	}

	bool bConstraintHit = false;
	float ClosestConstraintT = FLT_MAX;
	int32 HitConstraintIndex = -1;
	if (bShowConstraints && bShowConstraintLimitAngles)
	{
		const TArray<FPhysicsAssetConstraintSetup>& Constraints = Asset->GetConstraintSetups();
		for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(Constraints.size()); ++ConstraintIndex)
		{
			FTransform ParentFrameWorld;
			FTransform ChildFrameWorld;
			if (!PoseCache.ComputeConstraintWorldFrames(ConstraintIndex, ParentFrameWorld, ChildFrameWorld))
			{
				continue;
			}

			const FConstraintLimitDesc& Limits = Constraints[ConstraintIndex].Limits;
			const float Radius = ComputeConstraintLimitSurfaceRadius(ParentFrameWorld, ChildFrameWorld);
			const float Tolerance = ComputeConstraintPickTolerance(Radius);
			const FQuat ParentRotation = ParentFrameWorld.Rotation.GetNormalized();
			const FQuat ChildRotation = ChildFrameWorld.Rotation.GetNormalized();
			const FVector AxisX = ParentRotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
			const FVector AxisY = ParentRotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
			const FVector AxisZ = ParentRotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));

			const FVector SwingCenter = ParentFrameWorld.Location + AxisX * Radius;
			const float Swing1Radians = MotionLimitDegreesForPreview(Limits.Swing1, Limits.Swing1LimitDegrees) * FMath::DegToRad;
			const float Swing2Radians = MotionLimitDegreesForPreview(Limits.Swing2, Limits.Swing2LimitDegrees) * FMath::DegToRad;
			const float DiskRadiusY = (std::max)(Radius * sinf(Swing1Radians), Radius * 0.025f);
			const float DiskRadiusZ = (std::max)(Radius * sinf(Swing2Radians), Radius * 0.025f);

			if (Limits.Swing1 == EConstraintMotion::Locked && Limits.Swing2 == EConstraintMotion::Locked)
			{
				const float CrossSize = Radius * 0.12f;
				TestConstraintPickSegment(WorldRay, SwingCenter + AxisY * CrossSize, SwingCenter - AxisY * CrossSize, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
				TestConstraintPickSegment(WorldRay, SwingCenter + AxisZ * CrossSize, SwingCenter - AxisZ * CrossSize, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
			}
			else
			{
				FVector Prev = SwingCenter + AxisY * DiskRadiusY;
				for (int32 Segment = 1; Segment <= ConstraintLimitCircleSegments; ++Segment)
				{
					const float Angle = 2.0f * FMath::Pi * static_cast<float>(Segment) / static_cast<float>(ConstraintLimitCircleSegments);
					const FVector Next = SwingCenter + AxisY * (cosf(Angle) * DiskRadiusY) + AxisZ * (sinf(Angle) * DiskRadiusZ);
					TestConstraintPickSegment(WorldRay, Prev, Next, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
					Prev = Next;
				}
				TestConstraintPickSegment(WorldRay, SwingCenter, SwingCenter + AxisY * DiskRadiusY, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
				TestConstraintPickSegment(WorldRay, SwingCenter, SwingCenter + AxisZ * DiskRadiusZ, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
			}

			const FVector TwistCenter = ParentFrameWorld.Location + AxisX * (Radius * 0.12f);
			const float RingRadius = Radius * 0.32f;
			if (Limits.Twist == EConstraintMotion::Locked)
			{
				TestConstraintPickSegment(WorldRay, TwistCenter - AxisY * RingRadius, TwistCenter + AxisY * RingRadius, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
				TestConstraintPickSegment(WorldRay, TwistCenter - AxisZ * RingRadius, TwistCenter + AxisZ * RingRadius, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
			}
			else
			{
				float StartAngle = 0.0f;
				float EndAngle = 2.0f * FMath::Pi;
				if (Limits.Twist == EConstraintMotion::Limited)
				{
					StartAngle = Limits.TwistLimitMinDegrees * FMath::DegToRad;
					EndAngle = Limits.TwistLimitMaxDegrees * FMath::DegToRad;
					if (StartAngle > EndAngle)
					{
						std::swap(StartAngle, EndAngle);
					}
				}

				const float AngleRange = EndAngle - StartAngle;
				if (AngleRange > 0.001f)
				{
					const int32 Segments = Limits.Twist == EConstraintMotion::Free
						? ConstraintLimitCircleSegments
						: ConstraintArcSegments(AngleRange);
					FVector Prev = TwistCenter + (AxisY * cosf(StartAngle) + AxisZ * sinf(StartAngle)) * RingRadius;
					for (int32 Segment = 1; Segment <= Segments; ++Segment)
					{
						const float Alpha = static_cast<float>(Segment) / static_cast<float>(Segments);
						const float Angle = StartAngle + AngleRange * Alpha;
						const FVector Next = TwistCenter + (AxisY * cosf(Angle) + AxisZ * sinf(Angle)) * RingRadius;
						TestConstraintPickSegment(WorldRay, Prev, Next, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
						Prev = Next;
					}
					if (Limits.Twist == EConstraintMotion::Limited)
					{
						TestConstraintPickSegment(WorldRay, TwistCenter, TwistCenter + (AxisY * cosf(StartAngle) + AxisZ * sinf(StartAngle)) * RingRadius, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
						TestConstraintPickSegment(WorldRay, TwistCenter, TwistCenter + (AxisY * cosf(EndAngle) + AxisZ * sinf(EndAngle)) * RingRadius, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
					}
				}
			}

			const FVector ChildY = ChildRotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
			TestConstraintPickSegment(WorldRay, TwistCenter, TwistCenter + ChildY * RingRadius, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
			TestConstraintPickSegment(WorldRay, ParentFrameWorld.Location, ChildFrameWorld.Location, Tolerance, ConstraintIndex, bConstraintHit, ClosestConstraintT, HitConstraintIndex);
		}
	}

	if (bConstraintHit && (!bShapeHit || ClosestConstraintT < ClosestShapeT))
	{
		OutHitResult.bHit = true;
		OutHitResult.HitComponent = this;
		OutHitResult.Distance = ClosestConstraintT;
		OutHitResult.WorldHitLocation = WorldRay.Origin + WorldRay.Direction * ClosestConstraintT;
		OutHitResult.FaceIndex = EncodeConstraintFaceIndex(HitConstraintIndex);
		return true;
	}

	if (!bShapeHit)
	{
		return false;
	}

	OutHitResult.bHit = true;
	OutHitResult.HitComponent = this;
	OutHitResult.Distance = ClosestShapeT;
	OutHitResult.WorldHitLocation = WorldRay.Origin + WorldRay.Direction * ClosestShapeT;
	OutHitResult.FaceIndex = EncodeSelectionFaceIndex(HitBodyIndex, HitShapeIndex);
	return true;
}

void UPhysicsAssetPreviewComponent::UpdateWorldAABB() const
{
	if (bHasPreviewBounds && CachedWorldBounds.IsValid())
	{
		WorldAABBMinLocation = CachedWorldBounds.Min;
		WorldAABBMaxLocation = CachedWorldBounds.Max;
		bWorldAABBDirty = false;
		bHasValidWorldAABB = true;
		return;
	}

	UPrimitiveComponent::UpdateWorldAABB();
}

	void UPhysicsAssetPreviewComponent::UpdatePreview(
		UPhysicsAsset* InPhysicsAsset,
		USkeletalMeshComponent* InPreviewComponent,
		int32 InSelectedBodyIndex,
		int32 InSelectedShapeIndex,
		int32 InSelectedConstraintIndex,
		bool bInShowBodies,
		bool bInShowConstraints,
	bool bInShowConstraintLimitAngles,
	bool bInShowConstraintLimitSurfaces,
	bool bInShowOnlySelectedConstraintLimitSurfaces,
	ID3D11Device* Device)
{
		PhysicsAsset = InPhysicsAsset;
		PreviewComponent = InPreviewComponent;
		SelectedBodyIndex = InSelectedBodyIndex;
		SelectedShapeIndex = InSelectedShapeIndex;
		SelectedConstraintIndex = InSelectedConstraintIndex;
		bShowBodies = bInShowBodies;
		bShowConstraints = bInShowConstraints;
	bShowConstraintLimitAngles = bInShowConstraintLimitAngles;
	bShowConstraintLimitSurfaces = bInShowConstraintLimitSurfaces;
	bShowOnlySelectedConstraintLimitSurfaces = bInShowOnlySelectedConstraintLimitSurfaces;

	const bool bCanPickConstraintDebug = bShowConstraints && bShowConstraintLimitAngles;
	if ((!bShowBodies && !bShowConstraintLimitSurfaces && !bCanPickConstraintDebug) || !PhysicsAsset.Get() || !PreviewComponent.Get() || !Device)
	{
		ClearPreview(Device);
		InvalidatePreviewBuildCache();
		return;
	}

	const uint64 CurrentSkinnedRevision = PreviewComponent.Get()->GetSkinnedRevision();
	const uint64 CurrentComponentWorldHash = HashPreviewComponentWorld(PreviewComponent.Get());
	const uint64 CurrentAssetHash = ComputePhysicsAssetPreviewHash(PhysicsAsset.Get());
	if (IsPreviewBuildCacheCurrent(
			PhysicsAsset.Get(),
			PreviewComponent.Get(),
			SelectedBodyIndex,
			SelectedShapeIndex,
			SelectedConstraintIndex,
			bShowBodies,
			bShowConstraints,
			bShowConstraintLimitAngles,
			bShowConstraintLimitSurfaces,
			bShowOnlySelectedConstraintLimitSurfaces,
			CurrentSkinnedRevision,
			CurrentComponentWorldHash,
			CurrentAssetHash))
	{
		SetVisibility(!PreviewMeshData.Vertices.empty() || bCanPickConstraintDebug);
		return;
	}

	RebuildPreviewMesh();
	UploadPreviewMesh(Device);
	StorePreviewBuildCache(
		PhysicsAsset.Get(),
		PreviewComponent.Get(),
		SelectedBodyIndex,
		SelectedShapeIndex,
		SelectedConstraintIndex,
		bShowBodies,
		bShowConstraints,
		bShowConstraintLimitAngles,
		bShowConstraintLimitSurfaces,
		bShowOnlySelectedConstraintLimitSurfaces,
		CurrentSkinnedRevision,
		CurrentComponentWorldHash,
		CurrentAssetHash);
	SetVisibility(!PreviewMeshData.Vertices.empty() || bCanPickConstraintDebug);
}

void UPhysicsAssetPreviewComponent::ClearPreview(ID3D11Device* Device)
{
	(void)Device;
	PreviewMeshData.Vertices.clear();
	PreviewMeshData.Indices.clear();
	CachedWorldBounds = FBoundingBox();
	bHasPreviewBounds = false;
	PreviewMeshBuffer.Release();
	InvalidatePreviewBuildCache();

	SetVisibility(false);
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}


bool UPhysicsAssetPreviewComponent::IsPreviewBuildCacheCurrent(
	UPhysicsAsset* InPhysicsAsset,
	USkeletalMeshComponent* InPreviewComponent,
	int32 InSelectedBodyIndex,
	int32 InSelectedShapeIndex,
	int32 InSelectedConstraintIndex,
	bool bInShowBodies,
	bool bInShowConstraints,
	bool bInShowConstraintLimitAngles,
	bool bInShowConstraintLimitSurfaces,
	bool bInShowOnlySelectedConstraintLimitSurfaces,
	uint64 InSkinnedRevision,
	uint64 InComponentWorldHash,
	uint64 InAssetHash) const
{
	return bHasValidPreviewBuildCache &&
		CachedBuildPhysicsAsset == InPhysicsAsset &&
		CachedBuildPreviewComponent == InPreviewComponent &&
		CachedBuildSelectedBodyIndex == InSelectedBodyIndex &&
		CachedBuildSelectedShapeIndex == InSelectedShapeIndex &&
		CachedBuildSelectedConstraintIndex == InSelectedConstraintIndex &&
		bCachedBuildShowBodies == bInShowBodies &&
		bCachedBuildShowConstraints == bInShowConstraints &&
		bCachedBuildShowConstraintLimitAngles == bInShowConstraintLimitAngles &&
		bCachedBuildShowConstraintLimitSurfaces == bInShowConstraintLimitSurfaces &&
		bCachedBuildShowOnlySelectedConstraintLimitSurfaces == bInShowOnlySelectedConstraintLimitSurfaces &&
		CachedBuildSkinnedRevision == InSkinnedRevision &&
		CachedBuildComponentWorldHash == InComponentWorldHash &&
		CachedBuildAssetHash == InAssetHash;
}

void UPhysicsAssetPreviewComponent::StorePreviewBuildCache(
	UPhysicsAsset* InPhysicsAsset,
	USkeletalMeshComponent* InPreviewComponent,
	int32 InSelectedBodyIndex,
	int32 InSelectedShapeIndex,
	int32 InSelectedConstraintIndex,
	bool bInShowBodies,
	bool bInShowConstraints,
	bool bInShowConstraintLimitAngles,
	bool bInShowConstraintLimitSurfaces,
	bool bInShowOnlySelectedConstraintLimitSurfaces,
	uint64 InSkinnedRevision,
	uint64 InComponentWorldHash,
	uint64 InAssetHash)
{
	CachedBuildPhysicsAsset = InPhysicsAsset;
	CachedBuildPreviewComponent = InPreviewComponent;
	CachedBuildSelectedBodyIndex = InSelectedBodyIndex;
	CachedBuildSelectedShapeIndex = InSelectedShapeIndex;
	CachedBuildSelectedConstraintIndex = InSelectedConstraintIndex;
	bCachedBuildShowBodies = bInShowBodies;
	bCachedBuildShowConstraints = bInShowConstraints;
	bCachedBuildShowConstraintLimitAngles = bInShowConstraintLimitAngles;
	bCachedBuildShowConstraintLimitSurfaces = bInShowConstraintLimitSurfaces;
	bCachedBuildShowOnlySelectedConstraintLimitSurfaces = bInShowOnlySelectedConstraintLimitSurfaces;
	CachedBuildSkinnedRevision = InSkinnedRevision;
	CachedBuildComponentWorldHash = InComponentWorldHash;
	CachedBuildAssetHash = InAssetHash;
	bHasValidPreviewBuildCache = true;
}

void UPhysicsAssetPreviewComponent::InvalidatePreviewBuildCache()
{
	CachedBuildPhysicsAsset = nullptr;
	CachedBuildPreviewComponent = nullptr;
	CachedBuildSelectedBodyIndex = -1;
	CachedBuildSelectedShapeIndex = -1;
	CachedBuildSelectedConstraintIndex = -1;
	bCachedBuildShowBodies = false;
	bCachedBuildShowConstraints = false;
	bCachedBuildShowConstraintLimitAngles = false;
	bCachedBuildShowConstraintLimitSurfaces = false;
	bCachedBuildShowOnlySelectedConstraintLimitSurfaces = false;
	CachedBuildSkinnedRevision = 0;
	CachedBuildComponentWorldHash = 0;
	CachedBuildAssetHash = 0;
	bHasValidPreviewBuildCache = false;
}

void UPhysicsAssetPreviewComponent::RebuildPreviewMesh()
{
	PreviewMeshData.Vertices.clear();
	PreviewMeshData.Indices.clear();
	CachedWorldBounds = FBoundingBox();
	bHasPreviewBounds = false;

	UPhysicsAsset* Asset = PhysicsAsset.Get();
	USkeletalMeshComponent* MeshComponent = PreviewComponent.Get();
	if (!Asset || !MeshComponent)
	{
		return;
	}

	FPhysicsAssetPreviewPoseCache PoseCache;
	if (!PoseCache.Initialize(MeshComponent, Asset))
	{
		return;
	}

	if (bShowBodies)
	{
		const TArray<FPhysicsAssetBodySetup>& Bodies = Asset->GetBodySetups();
		for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
		{
			FTransform BodyWorld;
			if (!PoseCache.ComputeBodyWorldTransform(BodyIndex, BodyWorld))
			{
				continue;
			}

			const FPhysicsAssetBodySetup& Body = Bodies[BodyIndex];
			const bool bSelectedBody = SelectedBodyIndex == BodyIndex && SelectedConstraintIndex < 0;
			for (int32 ShapeIndex = 0; ShapeIndex < static_cast<int32>(Body.Shapes.size()); ++ShapeIndex)
			{
				const FPhysicsAssetShapeSetup& Shape = Body.Shapes[ShapeIndex];
				const FTransform ShapeWorld = ComposePreviewDebugTransforms(BodyWorld, Shape.LocalTransform);
				const bool bSelectedShape = bSelectedBody && SelectedShapeIndex == ShapeIndex;
				const FVector4 Color = PhysicsPreviewShapeColor(bSelectedBody, bSelectedShape);

				switch (Shape.Type)
				{
				case EPhysicsAssetShapeType::Box:
					AppendBox(ShapeWorld, ClampHalfExtent(Shape.BoxHalfExtent), Color);
					break;
				case EPhysicsAssetShapeType::Sphere:
					AppendSphere(ShapeWorld, (std::max)(Shape.SphereRadius, MinShapeSize), Color);
					break;
				case EPhysicsAssetShapeType::Capsule:
				{
					const float Radius = (std::max)(Shape.CapsuleRadius, MinShapeSize);
					const float HalfHeight = (std::max)(Shape.CapsuleHalfHeight, Radius);
					AppendCapsuleZAxis(ShapeWorld, Radius, HalfHeight, Color);
					break;
				}
				default:
					break;
				}
			}
		}
	}

	if (bShowConstraintLimitSurfaces)
	{
		const TArray<FPhysicsAssetConstraintSetup>& Constraints = Asset->GetConstraintSetups();
		for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(Constraints.size()); ++ConstraintIndex)
		{
			if (bShowOnlySelectedConstraintLimitSurfaces && SelectedConstraintIndex != ConstraintIndex)
			{
				continue;
			}
			AppendConstraintLimitSurfaces(ConstraintIndex, PoseCache);
		}
	}

}

void UPhysicsAssetPreviewComponent::UploadPreviewMesh(ID3D11Device* Device)
{
	if (!Device)
	{
		return;
	}

	PreviewMeshBuffer.Create(Device, PreviewMeshData);
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

uint32 UPhysicsAssetPreviewComponent::AddVertexWorld(const FVector& WorldPosition, const FVector4& Color)
{
	const FVector LocalPosition = GetWorldInverseMatrix().TransformPositionWithW(WorldPosition);
	PreviewMeshData.Vertices.push_back({ LocalPosition, Color, 0 });
	ExpandBoundsWorld(WorldPosition);
	return static_cast<uint32>(PreviewMeshData.Vertices.size() - 1);
}

void UPhysicsAssetPreviewComponent::AppendConstraintLimitSurfaces(
	int32 ConstraintIndex,
	const FPhysicsAssetPreviewPoseCache& PoseCache)
{
	UPhysicsAsset* Asset = PhysicsAsset.Get();
	if (!Asset)
	{
		return;
	}

	const TArray<FPhysicsAssetConstraintSetup>& Constraints = Asset->GetConstraintSetups();
	if (ConstraintIndex < 0 || ConstraintIndex >= static_cast<int32>(Constraints.size()))
	{
		return;
	}

	FTransform ParentFrameWorld;
	FTransform ChildFrameWorld;
	if (!PoseCache.ComputeConstraintWorldFrames(ConstraintIndex, ParentFrameWorld, ChildFrameWorld))
	{
		return;
	}

	const bool bSelectedConstraint = SelectedConstraintIndex == ConstraintIndex;
	const float Radius = ComputeConstraintLimitSurfaceRadius(ParentFrameWorld, ChildFrameWorld);
	const FConstraintLimitDesc& Limits = Constraints[ConstraintIndex].Limits;
	AppendSwingLimitSurface(ParentFrameWorld, Limits, Radius, PhysicsPreviewSwingLimitColor(bSelectedConstraint));
	AppendTwistLimitSurface(ParentFrameWorld, Limits, Radius, PhysicsPreviewTwistLimitColor(bSelectedConstraint));
}

void UPhysicsAssetPreviewComponent::AppendSwingLimitSurface(
	const FTransform& ParentFrameWorld,
	const FConstraintLimitDesc& Limits,
	float Radius,
	const FVector4& Color)
{
	if (Radius <= 0.0f)
	{
		return;
	}

	const float Swing1Degrees = MotionLimitDegreesForPreview(Limits.Swing1, Limits.Swing1LimitDegrees);
	const float Swing2Degrees = MotionLimitDegreesForPreview(Limits.Swing2, Limits.Swing2LimitDegrees);
	if (Swing1Degrees <= 0.001f && Swing2Degrees <= 0.001f)
	{
		return;
	}

	const float Swing1Radians = Swing1Degrees * FMath::DegToRad;
	const float Swing2Radians = Swing2Degrees * FMath::DegToRad;

	const FQuat Rotation = ParentFrameWorld.Rotation.GetNormalized();
	const FVector Center = ParentFrameWorld.Location;
	const FVector AxisX = Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
	const FVector AxisY = Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
	const FVector AxisZ = Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));

	const FVector DiskCenter = Center + AxisX * Radius;
	const float DiskRadiusY = (std::max)(Radius * sinf(Swing1Radians), Radius * 0.025f);
	const float DiskRadiusZ = (std::max)(Radius * sinf(Swing2Radians), Radius * 0.025f);

	TArray<uint32> RimIndices;
	RimIndices.reserve(ConstraintLimitCircleSegments + 1);
	for (int32 Segment = 0; Segment <= ConstraintLimitCircleSegments; ++Segment)
	{
		const float Angle = 2.0f * FMath::Pi * static_cast<float>(Segment) / static_cast<float>(ConstraintLimitCircleSegments);
		const FVector Point = DiskCenter + AxisY * (cosf(Angle) * DiskRadiusY) + AxisZ * (sinf(Angle) * DiskRadiusZ);
		RimIndices.push_back(AddVertexWorld(Point, Color));
	}

	const uint32 CenterIndex = AddVertexWorld(DiskCenter, Color);
	for (int32 Segment = 0; Segment < ConstraintLimitCircleSegments; ++Segment)
	{
		PreviewMeshData.Indices.push_back(CenterIndex);
		PreviewMeshData.Indices.push_back(RimIndices[Segment]);
		PreviewMeshData.Indices.push_back(RimIndices[Segment + 1]);
	}
}

void UPhysicsAssetPreviewComponent::AppendTwistLimitSurface(
	const FTransform& ParentFrameWorld,
	const FConstraintLimitDesc& Limits,
	float Radius,
	const FVector4& Color)
{
	if (Radius <= 0.0f || Limits.Twist == EConstraintMotion::Locked)
	{
		return;
	}

	float StartAngle = 0.0f;
	float EndAngle = 2.0f * FMath::Pi;
	if (Limits.Twist == EConstraintMotion::Limited)
	{
		StartAngle = Limits.TwistLimitMinDegrees * FMath::DegToRad;
		EndAngle = Limits.TwistLimitMaxDegrees * FMath::DegToRad;
		if (StartAngle > EndAngle)
		{
			std::swap(StartAngle, EndAngle);
		}
	}

	const float AngleRange = EndAngle - StartAngle;
	if (AngleRange <= 0.001f)
	{
		return;
	}

	const int32 Segments = Limits.Twist == EConstraintMotion::Free
		? ConstraintLimitCircleSegments
		: ConstraintArcSegments(AngleRange);
	const FQuat Rotation = ParentFrameWorld.Rotation.GetNormalized();
	const FVector AxisX = Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
	const FVector AxisY = Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
	const FVector AxisZ = Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));
	const FVector Center = ParentFrameWorld.Location + AxisX * (Radius * 0.12f);
	const float RingRadius = Radius * 0.32f;

	const uint32 CenterIndex = AddVertexWorld(Center, Color);
	uint32 PrevIndex = 0;
	for (int32 Segment = 0; Segment <= Segments; ++Segment)
	{
		const float Alpha = static_cast<float>(Segment) / static_cast<float>(Segments);
		const float Angle = StartAngle + AngleRange * Alpha;
		const FVector Point = Center + (AxisY * cosf(Angle) + AxisZ * sinf(Angle)) * RingRadius;
		const uint32 PointIndex = AddVertexWorld(Point, Color);
		if (Segment > 0)
		{
			PreviewMeshData.Indices.push_back(CenterIndex);
			PreviewMeshData.Indices.push_back(PrevIndex);
			PreviewMeshData.Indices.push_back(PointIndex);
		}
		PrevIndex = PointIndex;
	}
}

void UPhysicsAssetPreviewComponent::ExpandBoundsWorld(const FVector& WorldPosition)
{
	CachedWorldBounds.Expand(WorldPosition);
	bHasPreviewBounds = true;
}

void UPhysicsAssetPreviewComponent::AppendBox(const FTransform& ShapeWorld, const FVector& HalfExtent, const FVector4& Color)
{
	const FQuat Rotation = ShapeWorld.Rotation.GetNormalized();
	const FVector Center = ShapeWorld.Location;

	const FVector AxisX = Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
	const FVector AxisY = Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
	const FVector AxisZ = Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));

	auto Corner = [&](float X, float Y, float Z)
	{
		return Center + AxisX * (X * HalfExtent.X) + AxisY * (Y * HalfExtent.Y) + AxisZ * (Z * HalfExtent.Z);
	};

	auto AddFace = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		const uint32 Base = static_cast<uint32>(PreviewMeshData.Vertices.size());
		AddVertexWorld(A, Color);
		AddVertexWorld(B, Color);
		AddVertexWorld(C, Color);
		AddVertexWorld(D, Color);
		PreviewMeshData.Indices.push_back(Base + 0);
		PreviewMeshData.Indices.push_back(Base + 1);
		PreviewMeshData.Indices.push_back(Base + 2);
		PreviewMeshData.Indices.push_back(Base + 0);
		PreviewMeshData.Indices.push_back(Base + 2);
		PreviewMeshData.Indices.push_back(Base + 3);
	};

	const FVector P000 = Corner(-1.0f, -1.0f, -1.0f);
	const FVector P001 = Corner(-1.0f, -1.0f, 1.0f);
	const FVector P010 = Corner(-1.0f, 1.0f, -1.0f);
	const FVector P011 = Corner(-1.0f, 1.0f, 1.0f);
	const FVector P100 = Corner(1.0f, -1.0f, -1.0f);
	const FVector P101 = Corner(1.0f, -1.0f, 1.0f);
	const FVector P110 = Corner(1.0f, 1.0f, -1.0f);
	const FVector P111 = Corner(1.0f, 1.0f, 1.0f);

	AddFace(P100, P110, P111, P101);
	AddFace(P000, P001, P011, P010);
	AddFace(P010, P011, P111, P110);
	AddFace(P000, P100, P101, P001);
	AddFace(P001, P101, P111, P011);
	AddFace(P000, P010, P110, P100);
}

void UPhysicsAssetPreviewComponent::AppendSphere(const FTransform& ShapeWorld, float Radius, const FVector4& Color)
{
	const FQuat Rotation = ShapeWorld.Rotation.GetNormalized();
	const FVector Center = ShapeWorld.Location;
	const uint32 Base = static_cast<uint32>(PreviewMeshData.Vertices.size());

	for (int32 Stack = 0; Stack <= SphereStacks; ++Stack)
	{
		const float V = static_cast<float>(Stack) / static_cast<float>(SphereStacks);
		const float Phi = -0.5f * FMath::Pi + V * FMath::Pi;
		const float CP = cosf(Phi);
		const float SP = sinf(Phi);

		for (int32 Slice = 0; Slice <= SphereSlices; ++Slice)
		{
			const float U = static_cast<float>(Slice) / static_cast<float>(SphereSlices);
			const float Theta = U * 2.0f * FMath::Pi;
			const FVector LocalNormal(CP * cosf(Theta), CP * sinf(Theta), SP);
			AddVertexWorld(Center + Rotation.RotateVector(LocalNormal * Radius), Color);
		}
	}

	const uint32 RingStride = SphereSlices + 1;
	for (int32 Stack = 0; Stack < SphereStacks; ++Stack)
	{
		for (int32 Slice = 0; Slice < SphereSlices; ++Slice)
		{
			const uint32 I0 = Base + Stack * RingStride + Slice;
			const uint32 I1 = I0 + 1;
			const uint32 I2 = I0 + RingStride;
			const uint32 I3 = I2 + 1;
			PreviewMeshData.Indices.push_back(I0);
			PreviewMeshData.Indices.push_back(I2);
			PreviewMeshData.Indices.push_back(I1);
			PreviewMeshData.Indices.push_back(I1);
			PreviewMeshData.Indices.push_back(I2);
			PreviewMeshData.Indices.push_back(I3);
		}
	}
}

void UPhysicsAssetPreviewComponent::AppendCapsuleZAxis(
	const FTransform& ShapeWorld,
	float Radius,
	float HalfHeight,
	const FVector4& Color)
{
	Radius = (std::max)(Radius, MinShapeSize);
	HalfHeight = (std::max)(HalfHeight, Radius);
	const float CylinderHalf = (std::max)(0.0f, HalfHeight - Radius);

	const FQuat Rotation = ShapeWorld.Rotation.GetNormalized();
	const FVector Center = ShapeWorld.Location;
	const FVector AxisX = Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
	const FVector AxisY = Rotation.RotateVector(FVector(0.0f, 1.0f, 0.0f));
	const FVector AxisZ = Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));

	TArray<uint32> RingBases;
	auto AddRing = [&](float Z, float RingRadius)
	{
		RingBases.push_back(static_cast<uint32>(PreviewMeshData.Vertices.size()));
		for (int32 Slice = 0; Slice <= CapsuleSlices; ++Slice)
		{
			const float U = static_cast<float>(Slice) / static_cast<float>(CapsuleSlices);
			const float Theta = U * 2.0f * FMath::Pi;
			const float X = cosf(Theta) * RingRadius;
			const float Y = sinf(Theta) * RingRadius;
			AddVertexWorld(Center + AxisX * X + AxisY * Y + AxisZ * Z, Color);
		}
	};

	AddRing(-CylinderHalf - Radius, 0.0f);

	for (int32 Stack = 1; Stack <= CapsuleHemisphereStacks; ++Stack)
	{
		const float T = static_cast<float>(Stack) / static_cast<float>(CapsuleHemisphereStacks);
		const float Angle = -0.5f * FMath::Pi + T * 0.5f * FMath::Pi;
		AddRing(-CylinderHalf + sinf(Angle) * Radius, cosf(Angle) * Radius);
	}

	AddRing(CylinderHalf, Radius);

	for (int32 Stack = 1; Stack < CapsuleHemisphereStacks; ++Stack)
	{
		const float T = static_cast<float>(Stack) / static_cast<float>(CapsuleHemisphereStacks);
		const float Angle = T * 0.5f * FMath::Pi;
		AddRing(CylinderHalf + sinf(Angle) * Radius, cosf(Angle) * Radius);
	}

	AddRing(CylinderHalf + Radius, 0.0f);

	for (int32 Ring = 0; Ring + 1 < static_cast<int32>(RingBases.size()); ++Ring)
	{
		const uint32 R0 = RingBases[Ring];
		const uint32 R1 = RingBases[Ring + 1];
		for (int32 Slice = 0; Slice < CapsuleSlices; ++Slice)
		{
			const uint32 I0 = R0 + Slice;
			const uint32 I1 = R0 + Slice + 1;
			const uint32 I2 = R1 + Slice;
			const uint32 I3 = R1 + Slice + 1;
			PreviewMeshData.Indices.push_back(I0);
			PreviewMeshData.Indices.push_back(I2);
			PreviewMeshData.Indices.push_back(I1);
			PreviewMeshData.Indices.push_back(I1);
			PreviewMeshData.Indices.push_back(I2);
			PreviewMeshData.Indices.push_back(I3);
		}
	}
}
