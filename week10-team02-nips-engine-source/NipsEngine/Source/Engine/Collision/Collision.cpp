#include "Collision.h"

#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/Collision/SphereComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Geometry/Capsule.h"
#include "Geometry/OBB.h"
#include "Math/Utils.h"
#include "Math/Matrix.h"

#include <algorithm>

namespace
{
    FOBB MakeOBB(const UBoxComponent* Box)
    {
        const FMatrix& WorldMatrix = Box->GetWorldMatrix();
        const FVector Extent = Box->GetBoxExtent();
        const FVector Scale = Box->GetWorldScale();
        return FOBB(
            Box->GetWorldLocation(),
            FVector(
                MathUtil::Abs(Extent.X) * MathUtil::Abs(Scale.X),
                MathUtil::Abs(Extent.Y) * MathUtil::Abs(Scale.Y),
                MathUtil::Abs(Extent.Z) * MathUtil::Abs(Scale.Z)),
            WorldMatrix.GetRotationMatrix());
    }

    float GetSphereWorldRadius(const USphereComponent* Sphere)
    {
        return MathUtil::Abs(Sphere->GetSphereRadius());
    }

    FCapsule MakeCapsule(const UCapsuleComponent* Capsule)
    {
        const FMatrix& WorldMatrix = Capsule->GetWorldMatrix();
        const float Radius = MathUtil::Abs(Capsule->GetCapsuleRadius());
        const float HalfHeight = std::max(MathUtil::Abs(Capsule->GetCapsuleHalfHeight()), Radius);
        const float SegmentHalfLength = std::max(0.0f, HalfHeight - Radius);
        const FVector Up = WorldMatrix.GetUnitAxis(EAxis::Z);
        const FVector Center = Capsule->GetWorldLocation();
        return FCapsule(Center - Up * SegmentHalfLength, Center + Up * SegmentHalfLength, Radius);
    }

    bool AreAABBOverlapping(const FAABB& A, const FAABB& B)
    {
        return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X &&
               A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y &&
               A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
    }
}

bool FCollision::TestOverlap(UPrimitiveComponent* A, UPrimitiveComponent* B, FHitResult* OutHit)
{
    if (A == nullptr || B == nullptr)
    {
        return false;
    }

    const ECollisionType TypeA = A->GetCollisionType();
    const ECollisionType TypeB = B->GetCollisionType();

    if (TypeA == ECollisionType::Sphere && TypeB == ECollisionType::Sphere)
    {
        return TestSphereSphere(static_cast<USphereComponent*>(A), static_cast<USphereComponent*>(B), OutHit);
    }
    if (TypeA == ECollisionType::Sphere && TypeB == ECollisionType::Box)
    {
        return TestSphereBox(static_cast<USphereComponent*>(A), static_cast<UBoxComponent*>(B), OutHit);
    }
    if (TypeA == ECollisionType::Box && TypeB == ECollisionType::Sphere)
    {
        return TestSphereBox(static_cast<USphereComponent*>(B), static_cast<UBoxComponent*>(A), OutHit);
    }
    if (TypeA == ECollisionType::Sphere && TypeB == ECollisionType::Capsule)
    {
        return TestSphereCapsule(static_cast<USphereComponent*>(A), static_cast<UCapsuleComponent*>(B), OutHit);
    }
    if (TypeA == ECollisionType::Capsule && TypeB == ECollisionType::Sphere)
    {
        return TestSphereCapsule(static_cast<USphereComponent*>(B), static_cast<UCapsuleComponent*>(A), OutHit);
    }
    if (TypeA == ECollisionType::Box && TypeB == ECollisionType::Box)
    {
        return TestBoxBox(static_cast<UBoxComponent*>(A), static_cast<UBoxComponent*>(B), OutHit);
    }
    if (TypeA == ECollisionType::Box && TypeB == ECollisionType::Capsule)
    {
        return TestBoxCapsule(static_cast<UBoxComponent*>(A), static_cast<UCapsuleComponent*>(B), OutHit);
    }
    if (TypeA == ECollisionType::Capsule && TypeB == ECollisionType::Box)
    {
        return TestBoxCapsule(static_cast<UBoxComponent*>(B), static_cast<UCapsuleComponent*>(A), OutHit);
    }
    if (TypeA == ECollisionType::Capsule && TypeB == ECollisionType::Capsule)
    {
        return TestCapsuleCapsule(static_cast<UCapsuleComponent*>(A), static_cast<UCapsuleComponent*>(B), OutHit);
    }

    const bool bOverlapping = AreAABBOverlapping(A->GetWorldAABB(), B->GetWorldAABB());
    if (bOverlapping)
    {
        FillHitResult(OutHit, B, B->GetWorldAABB().GetCenter(), FVector::ZeroVector);
    }
    return bOverlapping;
}

// 구의 중심 사이의 거리와 반지름의 합을 비교해 충돌을 판정합니다.
bool FCollision::TestSphereSphere(const USphereComponent* A, const USphereComponent* B, FHitResult* OutHit)
{
    const FVector Delta = B->GetWorldLocation() - A->GetWorldLocation();
    const float RadiusSum = GetSphereWorldRadius(A) + GetSphereWorldRadius(B);
    const bool bOverlapping = Delta.SizeSquared() <= RadiusSum * RadiusSum;
    if (bOverlapping)
    {
        FillHitResult(OutHit, const_cast<USphereComponent*>(B), B->GetWorldLocation(), Delta.GetSafeNormal());
    }
    return bOverlapping;
}

// 구 중심에서 박스 표면까지의 가장 가까운 점을 구한 뒤, 거리가 반지름보다 작은지 판정합니다.
bool FCollision::TestSphereBox(const USphereComponent* Sphere, const UBoxComponent* Box, FHitResult* OutHit)
{
    const FOBB BoxOBB = MakeOBB(Box);
    const FVector SphereCenter = Sphere->GetWorldLocation();
    const FVector ClosestPoint = BoxOBB.ClosestPoint(SphereCenter);
    const FVector Delta = SphereCenter - ClosestPoint;
    const float Radius = GetSphereWorldRadius(Sphere);
    const bool bOverlapping = Delta.SizeSquared() <= Radius * Radius;
    if (bOverlapping)
    {
        FillHitResult(OutHit, const_cast<UBoxComponent*>(Box), ClosestPoint, Delta.GetSafeNormal());
    }
    return bOverlapping;
}

// 캡슐의 중심축에서 구 중심까지의 가장 가까운 점을 구한 뒤, 거리가 (구의 반지름 + 캡슐의 반지름)보다 작은지 판정합니다.
bool FCollision::TestSphereCapsule(const USphereComponent* Sphere, const UCapsuleComponent* Capsule, FHitResult* OutHit)
{
    const FCapsule CapsuleShape = MakeCapsule(Capsule);
    const FVector SphereCenter = Sphere->GetWorldLocation();
    FVector ClosestPoint;
    const float SphereRadius = GetSphereWorldRadius(Sphere);
    const bool bOverlapping = CapsuleShape.IntersectsSphere(SphereCenter, SphereRadius, &ClosestPoint);
    const FVector Delta = SphereCenter - ClosestPoint;
    if (bOverlapping)
    {
        FillHitResult(OutHit, const_cast<UCapsuleComponent*>(Capsule), ClosestPoint, Delta.GetSafeNormal());
    }
    return bOverlapping;
}

// SAT를 활용한 OBB-OBB 충돌 판정 → 분리축이 존재할 경우 충돌이 일어나지 않았다고 판정합니다.
bool FCollision::TestBoxBox(const UBoxComponent* A, const UBoxComponent* B, FHitResult* OutHit)
{
    const FOBB BoxA = MakeOBB(A);
    const FOBB BoxB = MakeOBB(B);
    const bool bOverlapping = BoxA.Intersects(BoxB);
    if (bOverlapping)
    {
        FillHitResult(OutHit, const_cast<UBoxComponent*>(B), BoxB.Center, (BoxB.Center - BoxA.Center).GetSafeNormal());
    }
    return bOverlapping;
}

// 캡슐 선분의 샘플 점 3개를 박스에 투영해 가장 가까운 점을 찾고, 캡슐 반지름과 비교합니다. (구-박스 충돌 판정 연산량의 3배)
// 정확한 박스-캡슐 충돌 판정은 근사 방식에 비해 연산량이 훨씬 증가할 수 있어 근사 처리합니다.
// GJK 알고리즘을 사용할 수도 있는데, 이보다는 연산량이 적으므로 추후 고려할 만합니다.
bool FCollision::TestBoxCapsule(const UBoxComponent* Box, const UCapsuleComponent* Capsule, FHitResult* OutHit)
{
    const FOBB BoxOBB = MakeOBB(Box);
    const FCapsule CapsuleShape = MakeCapsule(Capsule);
    const FVector ClosestToStart = BoxOBB.ClosestPoint(CapsuleShape.Segment.A);
    const FVector ClosestToEnd = BoxOBB.ClosestPoint(CapsuleShape.Segment.B);
    const FVector SegmentMid = CapsuleShape.Segment.Midpoint();
    const FVector ClosestToMid = BoxOBB.ClosestPoint(SegmentMid);

    const float StartDistSq = FVector::DistSquared(CapsuleShape.Segment.A, ClosestToStart);
    const float EndDistSq = FVector::DistSquared(CapsuleShape.Segment.B, ClosestToEnd);
    const float MidDistSq = FVector::DistSquared(SegmentMid, ClosestToMid);
    const float BestDistSq = MathUtil::Min3(StartDistSq, EndDistSq, MidDistSq);
    const FVector BestPoint = (BestDistSq == EndDistSq) ? ClosestToEnd : (BestDistSq == MidDistSq) ? ClosestToMid : ClosestToStart;

    const bool bOverlapping = BestDistSq <= CapsuleShape.Radius * CapsuleShape.Radius;
    if (bOverlapping)
    {
        FillHitResult(OutHit, const_cast<UBoxComponent*>(Box), BestPoint, (SegmentMid - BestPoint).GetSafeNormal());
    }
    return bOverlapping;
}

// 두 선분 사이의 가장 가까운 점 쌍을 구한 뒤, 거리가 반지름 합보다 작은지 확인합니다.
bool FCollision::TestCapsuleCapsule(const UCapsuleComponent* A, const UCapsuleComponent* B, FHitResult* OutHit)
{
    const FCapsule CapsuleA = MakeCapsule(A);
    const FCapsule CapsuleB = MakeCapsule(B);
    FVector ClosestA;
    FVector ClosestB;
    const bool bOverlapping = CapsuleA.Intersects(CapsuleB, &ClosestA, &ClosestB);

    const FVector Delta = ClosestB - ClosestA;
    if (bOverlapping)
    {
        FillHitResult(OutHit, const_cast<UCapsuleComponent*>(B), ClosestB, Delta.GetSafeNormal());
    }
    return bOverlapping;
}

void FCollision::FillHitResult(FHitResult* OutHit, UPrimitiveComponent* HitComponent, const FVector& Location, const FVector& Normal)
{
    if (OutHit == nullptr)
    {
        return;
    }

    OutHit->HitComponent = HitComponent;
    OutHit->Location = Location;
    OutHit->Normal = Normal;
    OutHit->Distance = 0.0f;
    OutHit->bHit = (HitComponent != nullptr);
}
