#pragma once

#include "Core/CollisionTypes.h"
#include "Math/Vector.h"

class UPrimitiveComponent;
class UBoxComponent;
class USphereComponent;
class UCapsuleComponent;

/** @brief 순수 수학적 충돌 판정을 다루는 유틸리티 클래스
  * @brief - 구-구, 구-박스, 구-캡슐, 박스-박스, 박스-캡슐, 캡슐-캡슐 충돌 처리
  * @brief - 계산 복잡도는 구 < 캡슐 < 박스 순으로 증가합니다. */
struct FCollision
{
    static bool TestOverlap(UPrimitiveComponent* A, UPrimitiveComponent* B, FHitResult* OutHit = nullptr);

private:
    static bool TestSphereSphere(const USphereComponent* A, const USphereComponent* B, FHitResult* OutHit);
    static bool TestSphereBox(const USphereComponent* Sphere, const UBoxComponent* Box, FHitResult* OutHit);
    static bool TestSphereCapsule(const USphereComponent* Sphere, const UCapsuleComponent* Capsule, FHitResult* OutHit);
    static bool TestBoxBox(const UBoxComponent* A, const UBoxComponent* B, FHitResult* OutHit);
    static bool TestBoxCapsule(const UBoxComponent* Box, const UCapsuleComponent* Capsule, FHitResult* OutHit);
    static bool TestCapsuleCapsule(const UCapsuleComponent* A, const UCapsuleComponent* B, FHitResult* OutHit);

    static void FillHitResult(FHitResult* OutHit, UPrimitiveComponent* HitComponent, const FVector& Location, const FVector& Normal);
};
