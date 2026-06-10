#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Core/Types/RayTypes.h"
#include "Core/Types/CollisionTypes.h"
#include "Physics/BodyInstance.h"

class UWorld;
class AActor;
class UPrimitiveComponent;
class UFourWheeledVehicleMovementComponent;
struct FHitResult;
struct FCollisionShape;

struct FFourWheeledVehicleRuntimeParams
{
	UPrimitiveComponent* ChassisComponent = nullptr;
	float ThrottleInput = 0.0f;
	float BrakeInput = 0.0f;
	float SteerInput = 0.0f;
	bool bUseManualGears = true;
	bool bGearShiftUpPressed = false;
	bool bGearShiftDownPressed = false;
	bool bGearNeutralPressed = false;
	float WheelRadius = 0.36f;
	float WheelWidth = 0.30f;
	float ChassisMass = 800.0f;
	float MaxSteerAngleDeg = 28.0f;
	float EnginePeakTorque = 750.0f;
	float EngineMaxRPM = 15000.0f;
	float BrakeTorque = 1500.0f;
	bool bEnableDownforce = true;
	float DownforceCoeff = 5.0f;
	float MaxDownforceMultiplier = 3.5f;
	bool bUseRearWheelDrive = true;
	float TireFrictionMultiplier = 1.85f;
	float TireLatGripScale = 2.8f;
	FVector CenterOfMassOffset = FVector(0.0f, 0.0f, -0.35f);
	FVector WheelCenterOffsets[4] = {
		FVector(-0.80f, -1.45f, -0.35f),
		FVector(0.80f, -1.45f, -0.35f),
		FVector(-0.80f, 1.45f, -0.35f),
		FVector(0.80f, 1.45f, -0.35f)
	};
};

struct FFourWheeledVehicleRuntimeState
{
	bool bValid = false;
	float ForwardSpeed = 0.0f;
	float EngineRPM = 0.0f;
	float EngineRPMRatio = 0.0f;
	int32 CurrentGear = 0;
	FString GearDisplay = "N";
	float WheelRotationDeg[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float WheelSteerDeg[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

// 물리 백엔드 선택
enum class EPhysicsBackend : uint8
{
	Native,		// Legacy hand-written collision (query/overlap; UE 기본은 PhysX)
	PhysX,		// NVIDIA PhysX 4.1 (UE 정렬 기본 백엔드)
};

// ============================================================
// IPhysicsScene — 물리 시스템 어댑터 인터페이스
//
// World가 소유하며, PrimitiveComponent가 등록/해제.
// Native 또는 PhysX로 교체 가능.
// ============================================================
class IPhysicsScene
{
public:
	virtual ~IPhysicsScene() = default;

	// --- Lifecycle ---
	virtual void Initialize(UWorld* InWorld) = 0;
	virtual void Shutdown() = 0;

	// --- Body 관리 ---
	virtual FBodyInstanceInitParams MakeBodyInstanceInitParams() const { return FBodyInstanceInitParams(); }
	virtual void RegisterComponent(UPrimitiveComponent* Comp) = 0;
	virtual void UnregisterComponent(UPrimitiveComponent* Comp) = 0;
	// 컴포넌트의 SimulatePhysics/ObjectType/Response 등이 변경된 경우 호출.
	// PhysX/Native 모두 해당 컴포넌트 body만 unregister + register.
	virtual void RebuildBody(UPrimitiveComponent* Comp) = 0;

	// --- 시뮬레이션 ---
	virtual void Tick(float DeltaTime) = 0;

	// --- Vehicles ---
	virtual void RegisterVehicle(UFourWheeledVehicleMovementComponent* VehicleComp, const FFourWheeledVehicleRuntimeParams& Params) {}
	virtual void UnregisterVehicle(UFourWheeledVehicleMovementComponent* VehicleComp) {}
	virtual void UpdateVehicle(UFourWheeledVehicleMovementComponent* VehicleComp, const FFourWheeledVehicleRuntimeParams& Params) {}
	virtual bool GetVehicleState(const UFourWheeledVehicleMovementComponent* VehicleComp, FFourWheeledVehicleRuntimeState& OutState) const { return false; }

	// --- 힘/토크 ---
	virtual void AddForce(UPrimitiveComponent* Comp, const FVector& Force) = 0;
	virtual void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) = 0;
	virtual void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) = 0;

	// --- 속도 읽기/쓰기 ---
	virtual FVector GetLinearVelocity(UPrimitiveComponent* Comp) const = 0;
	virtual void SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) = 0;
	virtual FVector GetAngularVelocity(UPrimitiveComponent* Comp) const = 0;
	virtual void SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) = 0;

	// --- Gravity ---
	virtual void SetEnableGravity(UPrimitiveComponent* Comp, bool bEnable) = 0;

	// --- Mass / Center of Mass ---
	virtual void SetMass(UPrimitiveComponent* Comp, float Mass) = 0;
	virtual float GetMass(UPrimitiveComponent* Comp) const = 0;
	// CenterOfMass는 RootComponent의 local 좌표계 기준 offset.
	// 차량처럼 mass center를 차체 아래로 내리면 회전 안정성↑.
	virtual void SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) = 0;
	virtual FVector GetCenterOfMass(UPrimitiveComponent* Comp) const = 0;

	// --- Raycast ---
	// TraceChannel: shape의 응답이 이 채널에 대해 Block일 때만 hit으로 인정 (UE 패턴).
	//   예: WorldStatic 채널로 trace → 응답이 WorldStatic Block인 shape만 hit.
	//   trigger flag가 set된 shape는 PhysX 측에서 자동 제외됨.
	// IgnoreActor: 자기 자신/소유 액터를 제외할 때 사용.
	virtual bool Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const = 0;

	virtual bool Sweep(const FVector& Start, const FVector& Dir, float MaxDist,
		const FCollisionShape& Shape, const FQuat& ShapeRot, FHitResult& OutHit,
		ECollisionChannel TraceChannel, const AActor* IgnoreActor) const = 0;

	// ObjectType 기반 Raycast — UE의 LineTraceSingleByObjectType 대응.
	//   ObjectTypeMask: bit i = ECollisionChannel(i)의 shape를 hit 후보로 둘지.
	//                   ObjectTypeBit(ECollisionChannel::WorldStatic) 처럼 헬퍼로 조합.
	// 채널 Raycast 는 "응답이 Block 인 모든 shape" 를 잡지만, 응답은 동적 객체/폰도 기본
	// Block 이라 의도와 어긋나기 쉽다. 본 함수는 shape의 ObjectType 자체를 마스크로 필터.
	//   예: 바닥 detection 은 ObjectTypeBit(WorldStatic) 만 → 다이내믹/폰을 바닥으로 잘못 잡지 않음.
	// Trigger flag shape 는 백엔드 별 정책에 따라 자동 제외 (PhysX 는 query 단계, Native 는 ObjectType 마스크에서 빠짐).
	virtual bool RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) const = 0;
};
