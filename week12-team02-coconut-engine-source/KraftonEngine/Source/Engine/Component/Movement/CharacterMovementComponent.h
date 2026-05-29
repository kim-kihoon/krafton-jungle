#pragma once
#include "Core/CollisionTypes.h"
#include "Core/EngineTypes.h"
#include "MovementComponent.h"
#include "CharacterMovementComponent.generated.h"

class UPrimitiveComponent;

/*
 * ACharacter가 사용할 경량 CharacterMovementComponent
 * ACharacter는 이 컴포넌트를 소유 초기화 시 SetUpdatedComponent(RootComponent)를 호출
 * SkeletalMesh는 시각 표현이고, 실제 캐릭터 이동 기준은 Capsule/Primitive
 */
UCLASS()
class UCharacterMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY(UCharacterMovementComponent)

	UCharacterMovementComponent() = default;
	~UCharacterMovementComponent() override = default;

	void			BeginPlay() override;
	void			TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void			PostEditProperty(const char* PropertyName) override;

	UFUNCTION(Lua)
	void			SetMoveInput(float ForwardValue, float RightValue);
	UFUNCTION(Lua)
	void			SetLookInput(float DeltaX, float DeltaY);
	UFUNCTION(Lua)
	void			Jump();
	UFUNCTION(Lua)
	void			StopMovementImmediately();

	const FVector&	GetVelocity() const;
	float			GetControllerDesiredYaw() const;
	float			GetControllerDesiredPitch() const;
	UFUNCTION(Lua)
	float			GetSpeed2D() const;
	UFUNCTION(Lua)
	bool			IsFalling() const;
	UFUNCTION(Lua)
	bool			IsMovingOnGround() const;

private:
	void			PhysWalking(float DeltaTime);
	void			PhysFalling(float DeltaTime);
	void			UpdateVelocityWalking(float DeltaTime);
	void			UpdateVelocityFalling(float DeltaTime);
	void			UpdateRotation(float DeltaTime);
	FVector			GetCurrentMoveDirection() const;
	bool			FindFloorAtLocation(const FVector& CapsuleCenterLocation, FHitResult& OutFloorHit) const;
	bool			EnsureFloorCenterOffset(const FHitResult& FloorHit, const FVector& CapsuleCenterLocation);
	void			MoveUpdatedComponentWalking(float DeltaTime);
	void			TryLandAfterFalling();
	void			ClearLookInput();
	void			SetMovementMode(EMovementMode NewMovementMode);
	void			LimitVelocity2D(float MaxSpeed);
	void			MoveUpdatedComponentKinematic(float DeltaTime);

	UPrimitiveComponent* UpdatedPrimitive = nullptr;

	// 런타임 이동 상태. 저장/에디터 노출 대상이 아니므로 UPROPERTY로 만들지 않음.
	FVector Velocity = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;

	// 외부 Lua/Controller가 매 프레임 주입하는 입력값.
	float MoveForwardInput = 0.0f;
	float MoveRightInput = 0.0f;
	float LookInputX = 0.0f;
	float LookInputY = 0.0f;

	// 카메라와 컨트롤러 회전 모드에서 사용할 목표 월드 회전.
	float ControllerDesiredYawDegrees = 0.0f;
	float ControllerDesiredPitchDegrees = 0.0f;

	// 현재 캐릭터 이동 상태.
	UPROPERTY(Edit, Category="Character Movement", DisplayName="Movement Mode")
	EMovementMode MovementMode = MOVE_Walking;

	// 지상 이동 시 도달할 수 있는 최대 2D 속도.
	UPROPERTY(Edit, Category="Character Movement|Walking", DisplayName="Max Walk Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float MaxWalkSpeed = 6.0f;

	// 입력이 들어왔을 때 속도를 늘리는 가속도.
	UPROPERTY(Edit, Category="Character Movement|Walking", DisplayName="Max Acceleration", Min=0.0f, Max=100.0f, Speed=0.1f)
	float MaxAcceleration = 20.0f;

	// 이동 입력이 없을 때 지상에서 적용할 감속도.
	UPROPERTY(Edit, Category="Character Movement|Walking", DisplayName="Braking Deceleration Walking", Min=0.0f, Max=100.0f, Speed=0.1f)
	float BrakingDecelerationWalking = 20.0f;

	// 지상 감속에 사용할 마찰 계수.
	UPROPERTY(Edit, Category="Character Movement|Walking", DisplayName="Ground Friction", Min=0.0f, Max=64.0f, Speed=0.1f)
	float GroundFriction = 8.0f;

	// 한 프레임에 자동으로 올라갈 수 있는 최대 단차 높이.
	UPROPERTY(Edit, Category="Character Movement|Step", DisplayName="Max Step Height", Min=0.0f, Max=10.0f, Speed=0.01f)
	float MaxStepHeight = 0.75f;

	// Walking 상태에서 따라 내려갈 수 있는 최대 낮은 단차. 이보다 크면 Falling으로 전환한다.
	UPROPERTY(Edit, Category="Character Movement|Step", DisplayName="Max Step Down Height", Min=0.0f, Max=20.0f, Speed=0.01f)
	float MaxStepDownHeight = 1.25f;

	// 바닥 탐지 raycast 시작점을 캡슐 중심보다 위로 올리는 거리.
	UPROPERTY(Edit, Category="Character Movement|Step", DisplayName="Floor Probe Up", Min=0.0f, Max=10.0f, Speed=0.01f)
	float FloorProbeUp = 0.25f;

	// 바닥 탐지 raycast 길이 중 아래 방향 거리.
	UPROPERTY(Edit, Category="Character Movement|Step", DisplayName="Floor Probe Down", Min=0.0f, Max=100.0f, Speed=0.1f)
	float FloorProbeDown = 8.0f;

	float FloorCenterOffset = 0.0f;
	bool bHasFloorCenterOffset = false;

	// 낙하 중 Z축에 적용할 중력 가속도. Z-up 기준이므로 기본값은 음수
	UPROPERTY(Edit, Category="Character Movement|Falling", DisplayName="Gravity Z", Min=-100.0f, Max=0.0f, Speed=0.1f)
	float GravityZ = -9.8f;

	// 점프 시작 시 Z축 속도에 넣을 값
	UPROPERTY(Edit, Category="Character Movement|Jumping", DisplayName="Jump Z Velocity", Min=0.0f, Max=50.0f, Speed=0.1f)
	float JumpZVelocity = 4.2f;

	// 낙하 중 수평 입력이 속도에 반영되는 비율
	UPROPERTY(Edit, Category="Character Movement|Falling", DisplayName="Air Control", Min=0.0f, Max=1.0f, Speed=0.01f)
	float AirControl = 0.2f;

	// true면 이동 방향을 바라보도록 회전
	UPROPERTY(Edit, Category="Character Movement|Rotation", DisplayName="Orient Rotation To Movement")
	bool bOrientRotationToMovement = true;

	// true면 컨트롤러가 지정한 Yaw를 바라보도록 회전
	UPROPERTY(Edit, Category="Character Movement|Rotation", DisplayName="Use Controller Desired Rotation")
	bool bUseControllerDesiredRotation = false;

	// 초당 회전 가능한 Yaw 각도. 음수면 즉시 회전
	UPROPERTY(Edit, Category="Character Movement|Rotation", DisplayName="Rotation Rate Yaw", Min=-1.0f, Max=3600.0f, Speed=10.0f)
	float RotationRateYaw = 720.0f;

	// Look 입력에 곱할 마우스 감도
	UPROPERTY(Edit, Category="Character Movement|Input", DisplayName="Mouse Sensitivity", Min=0.0f, Max=10.0f, Speed=0.01f)
	float MouseSensitivity = 0.1f;

	UPROPERTY(Edit, Category="Character Movement|Input", DisplayName="Min Controller Pitch", Min=-89.0f, Max=89.0f, Speed=1.0f)
	float MinControllerPitchDegrees = -80.0f;

	UPROPERTY(Edit, Category="Character Movement|Input", DisplayName="Max Controller Pitch", Min=-89.0f, Max=89.0f, Speed=1.0f)
	float MaxControllerPitchDegrees = 45.0f;
};
