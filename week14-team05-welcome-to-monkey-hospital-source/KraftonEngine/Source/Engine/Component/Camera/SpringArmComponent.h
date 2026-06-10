#pragma once

#include "Component/SceneComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

#include "Source/Engine/Component/Camera/SpringArmComponent.generated.h"
// ============================================================
// USpringArmComponent — 부착 액터 뒤를 부드럽게 따라가는 카메라 부착점.
//
// 사용 패턴: Pawn(Owner) → SpringArm(자식) → Camera(SpringArm 의 자식).
// SpringArm 의 World 는 매 Tick 에 부모의 World 를 따라 갱신되되, lag 옵션이
// 켜져 있으면 부드러운 보간으로 따라온다. Camera 컴포넌트는 SpringArm 의
// World 를 자동 상속하므로 별도 후크 없이 부드럽게 끌려오는 효과가 난다.
//
// 차량/플레이어 뒤를 따라오는 3인칭 카메라, 흔들림 있는 카메라 마운트 등에 사용.
// lag, control rotation, sweep 기반 충돌 회피를 처리한다.
// UE: USpringArmComponent (간소화)
// ============================================================
UCLASS()
class USpringArmComponent : public USceneComponent
{
public:
	GENERATED_BODY()
	USpringArmComponent() = default;
	~USpringArmComponent() override = default;

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	// ─── 튜닝 파라미터 ─────────────────────────────────────────────
	// arm 길이 — 부착점에서 카메라까지의 거리 (Local -X 방향).
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Target Arm Length", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float TargetArmLength = 300.0f;

	// arm 끝점(카메라 위치) 에 추가되는 offset (Lagged 회전 기준 적용).
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Socket Offset", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector SocketOffset = FVector(0.0f, 0.0f, 0.0f);

	// 부착점 자체에 추가되는 offset (Lagged 회전 기준 적용). 보통 머리 위/어깨 높이.
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Target Offset", Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f)
	FVector TargetOffset = FVector(0.0f, 0.0f, 0.0f);

	// Lag 옵션 — 끄면 부모를 즉시 따라감 (lag 없는 부착).
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Enable Camera Lag")
	bool bEnableCameraLag = false;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Enable Rotation Lag")
	bool bEnableCameraRotationLag = false;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Camera Lag Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float CameraLagSpeed = 10.0f;          // 클수록 빠르게 따라옴
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Camera Rotation Lag Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float CameraRotationLagSpeed = 10.0f;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Camera Lag Max Distance", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float CameraLagMaxDistance = 0.0f;     // 0 = 무제한

	// Collision 옵션 — 활성화 시 부착점 → ArmEnd 사이로 sphere sweep 을 수행해
	// 첫 blocking hit 지점까지만 arm 길이를 단축. 카메라가 벽/지형 너머로
	// 빠지는 현상을 방지한다. Owner Pawn 은 ignore.
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Do Collision Test")
	bool bDoCollisionTest = false;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Probe Channel", Enum=ECollisionChannel)
	ECollisionChannel ProbeChannel = ECollisionChannel::WorldStatic;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Probe Size", Min=0.0f, Max=100.0f, Speed=0.01f)
	float ProbeSize = 0.12f;               // sweep 반경. 0이면 raycast fallback

	// Control rotation 사용 옵션 (UE 패턴). true 면 부모 (capsule) 의 world rotation 대신
	// owner APawn 의 ControlRotation 을 desired rotation 으로 사용한다.
	// bInheritPitch/Yaw/Roll 가 각 axis 별로 — false 면 그 axis 는 capsule rotation 사용.
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Use Pawn Control Rotation")
	bool bUsePawnControlRotation = true;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Inherit Pitch")
	bool bInheritPitch           = true;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Inherit Yaw")
	bool bInheritYaw             = true;
	UPROPERTY(Edit, Save, Category="SpringArm", DisplayName="Inherit Roll")
	bool bInheritRoll            = false;

	// 텔레포트/리스폰/재부착처럼 이전 lag 상태를 이어가면 안 되는 시점에 호출한다.
	UFUNCTION(Callable, Category="SpringArm")
	void ResetLagState();

private:
	// 매 Tick 에 갱신되는 보간 상태 — 부착점 (parent + TargetOffset) 위치/회전.
	FVector LaggedAttachLoc = FVector(0.0f, 0.0f, 0.0f);
	FQuat LaggedAttachRot;
	bool bHasPreviousState = false;
};
