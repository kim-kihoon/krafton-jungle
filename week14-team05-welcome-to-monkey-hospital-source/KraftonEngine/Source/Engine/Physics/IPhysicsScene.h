#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Transform.h"
#include "Math/Quat.h"
#include "Core/Types/RayTypes.h"
#include "Core/Types/CollisionTypes.h"
#include "Physics/Vehicle/VehicleTypes.h"

class UWorld;
class AActor;
class UPrimitiveComponent;
class IPhysicsRuntime;
struct FHitResult;
struct FPhysicsRaycastResult;

// ============================================================
// IPhysicsScene — 물리 시스템 어댑터 인터페이스
//
// World가 소유하며, PrimitiveComponent가 등록/해제.
// 현재 구현체는 PhysX지만, 상위 인터페이스는 backend-neutral 타입만 노출한다.
// ============================================================
class IPhysicsScene
{
public:
	virtual ~IPhysicsScene() = default;

	// --- Lifecycle ---
	virtual void Initialize(UWorld* InWorld) = 0;
	virtual void Shutdown() = 0;

	// --- Body 관리 ---
	virtual void RegisterComponent(UPrimitiveComponent* Comp) = 0;
	virtual void UnregisterComponent(UPrimitiveComponent* Comp) = 0;
	// 컴포넌트의 SimulatePhysics/ObjectType/Response 등이 변경된 경우 호출.
	// backend actor 단위로 unregister + register 한다. compound shape의 다른 컴포넌트도 함께 재등록된다.
	virtual void RebuildBody(UPrimitiveComponent* Comp) = 0;
	// 컴포넌트 transform 변경 후 PhysX body pose만 갱신 (unregister/register 없음).
	virtual void SyncComponentTransform(UPrimitiveComponent* Comp) {}

	// --- 시뮬레이션 ---
	virtual void Tick(float DeltaTime) = 0;

    virtual void SubmitPhysicsFrame(uint64 FrameIndex, float DeltaTime)
    {
        Tick(DeltaTime);
    }

    virtual void WaitPhysicsFrame(uint64 /*FrameIndex*/)
    {}

    // Physics callback에서 수집한 이벤트를 안전한 PostPhysics 경계에서 발화한다.
    virtual void DispatchPendingEvents()
    {}

	// --- 힘/토크 ---
	virtual void AddForce(UPrimitiveComponent* Comp, const FVector& Force) = 0;
	virtual void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) = 0;
	virtual void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) = 0;
	virtual void AddImpulse(UPrimitiveComponent* Comp, const FVector& Impulse) = 0;

	// --- 속도 읽기/쓰기 ---
	virtual FVector GetLinearVelocity(UPrimitiveComponent* Comp) const = 0;
	virtual void SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) = 0;
	virtual FVector GetAngularVelocity(UPrimitiveComponent* Comp) const = 0;
	virtual void SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) = 0;

	// --- Mass / Center of Mass ---
	virtual void SetMass(UPrimitiveComponent* Comp, float Mass) = 0;
	virtual float GetMass(UPrimitiveComponent* Comp) const = 0;
	// CenterOfMass는 RootComponent의 local 좌표계 기준 offset.
	// 차량처럼 mass center를 차체 아래로 내리면 회전 안정성↑.
	virtual void SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) = 0;
	virtual FVector GetCenterOfMass(UPrimitiveComponent* Comp) const = 0;

	virtual void SetLinearLock(UPrimitiveComponent* Comp, bool bX, bool bY, bool bZ) = 0;
	virtual void SetAngularLock(UPrimitiveComponent* Comp, bool bX, bool bY, bool bZ) = 0;

	// --- Raycast ---
	// TraceChannel: shape의 응답이 이 채널에 대해 Block일 때만 hit으로 인정 (UE 패턴).
	//   예: WorldStatic 채널로 trace → 응답이 WorldStatic Block인 shape만 hit.
	//   trigger flag가 set된 shape는 physics backend 측에서 자동 제외됨.
	// IgnoreActor: 자기 자신/소유 액터를 제외할 때 사용.
	virtual bool Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) = 0;

	// ObjectType 기반 Raycast — UE의 LineTraceSingleByObjectType 대응.
	//   ObjectTypeMask: bit i = ECollisionChannel(i)의 shape를 hit 후보로 둘지.
	//                   ObjectTypeBit(ECollisionChannel::WorldStatic) 처럼 헬퍼로 조합.
	// 채널 Raycast 는 "응답이 Block 인 모든 shape" 를 잡지만, 응답은 동적 객체/폰도 기본
	// Block 이라 의도와 어긋나기 쉽다. 본 함수는 shape의 ObjectType 자체를 마스크로 필터.
	//   예: 바닥 detection 은 ObjectTypeBit(WorldStatic) 만 → 다이내믹/폰을 바닥으로 잘못 잡지 않음.
	// Trigger flag shape는 physics query 단계에서 자동 제외된다.
	virtual bool RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) = 0;

	virtual bool RaycastPhysicsByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FPhysicsRaycastResult& OutResult,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) = 0;

    // Shape sweep — Start 위치의 Shape를 End까지 이동시키며 blocking hit을 찾는다.
    // TraceChannel은 "대상 shape가 이 채널을 Block 하는가"를 의미한다.
    // IgnoreActor는 자기 자신/소유 액터를 제외할 때 사용한다.
    virtual bool Sweep(const FVector& Start, const FVector& End, const FQuat& Rotation, const FCollisionShape& Shape, FHitResult& OutHit,
        ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
        const AActor* IgnoreActor = nullptr) = 0;

    // ObjectType 기반 Shape sweep — 대상 shape의 ObjectType이 ObjectTypeMask에 포함될 때만 hit 후보로 둔다.
    virtual bool SweepByObjectTypes(const FVector& Start, const FVector& End, const FQuat& Rotation, const FCollisionShape& Shape, FHitResult& OutHit,
        uint32 ObjectTypeMask,
        const AActor* IgnoreActor = nullptr) = 0;

    virtual uint32 GetComponentGeneration_GameThread(uint32 /*ComponentId*/) const
    {
        return 0;
    }

    virtual FVehicleHandle CreateVehicle(const FVehicleDesc& /*Desc*/)
    {
        return {};
    }

    virtual void DestroyVehicle(FVehicleHandle /*Vehicle*/)
    {}

    virtual void SetVehicleInput(FVehicleHandle /*Vehicle*/, const FVehicleInputState& /*Input*/)
    {}

    virtual void ResetVehicle(FVehicleHandle /*Vehicle*/, const FTransform& /*WorldTransform*/)
    {}

    virtual IPhysicsRuntime* GetRuntime()
    {
        return nullptr;
    }

    virtual const IPhysicsRuntime* GetRuntime() const
    {
        return nullptr;
    }
};
