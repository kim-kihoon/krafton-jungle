#include "Component/Camera/SpringArmComponent.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Matrix.h"
#include "Math/Rotator.h"
#include <algorithm>
#include <cmath>

namespace
{
	FVector GetSafeNormalizedAxis(const FVector& Axis, const FVector& Fallback)
	{
		const float Length = Axis.Length();
		if (Length <= 1.0e-6f)
		{
			return Fallback;
		}

		return Axis / Length;
	}

	FQuat ExtractRotationWithoutScale(const FMatrix& Matrix)
	{
		const FVector XAxis = GetSafeNormalizedAxis(FVector(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]), FVector(1.0f, 0.0f, 0.0f));
		const FVector YAxis = GetSafeNormalizedAxis(FVector(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]), FVector(0.0f, 1.0f, 0.0f));
		const FVector ZAxis = GetSafeNormalizedAxis(FVector(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]), FVector(0.0f, 0.0f, 1.0f));

		FMatrix RotationMatrix = FMatrix::Identity;
		RotationMatrix.M[0][0] = XAxis.X; RotationMatrix.M[0][1] = XAxis.Y; RotationMatrix.M[0][2] = XAxis.Z;
		RotationMatrix.M[1][0] = YAxis.X; RotationMatrix.M[1][1] = YAxis.Y; RotationMatrix.M[1][2] = YAxis.Z;
		RotationMatrix.M[2][0] = ZAxis.X; RotationMatrix.M[2][1] = ZAxis.Y; RotationMatrix.M[2][2] = ZAxis.Z;
		return RotationMatrix.ToQuat().GetNormalized();
	}

	float CalculateExponentialLagAlpha(float DeltaTime, float Speed)
	{
		if (DeltaTime <= 0.0f || Speed <= 0.0f)
		{
			return 0.0f;
		}

		const float Alpha = 1.0f - static_cast<float>(std::exp(-static_cast<double>(Speed * DeltaTime)));
		return std::min(std::max(Alpha, 0.0f), 1.0f);
	}
}

void USpringArmComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetLagState();
}

void USpringArmComponent::ResetLagState()
{
	bHasPreviousState = false;
}

void USpringArmComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// SpringArm 은 부모가 있어야 의미가 있음. 부모 없으면 spring 동작은 skip 하고
	// SceneComponent 기본 transform 합성에 맡긴다.
	if (!ParentComponent)
	{
		return;
	}

	// (1) 부모 World transform 추출. 두 개 분리:
	//   - ParentActualRot: capsule 의 실제 world rotation (RelativeRotation 환산용 — 불변).
	//   - DesiredParentRot: SpringArm 이 사용할 desired rotation (control rotation 적용 후).
	const FMatrix& ParentWorld = ParentComponent->GetWorldMatrix();
	const FMatrix& ParentWorldInv = ParentComponent->GetWorldInverseMatrix();
	const FVector ParentWorldLoc = ParentComponent->GetWorldLocation();
	const FQuat   ParentActualRot  = ExtractRotationWithoutScale(ParentWorld);
	FQuat         DesiredParentRot = ParentActualRot;

	// bUsePawnControlRotation — capsule rotation 대신 owner APawn 의 ControlRotation 을
	// (선택된 axis 별로) 사용. mouse look 이 capsule 안 건드리고 카메라만 회전하는 패턴.
	if (bUsePawnControlRotation)
	{
		if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
		{
			FRotator Result = DesiredParentRot.ToRotator();
			const FRotator Ctrl = OwnerPawn->GetControlRotation();
			if (bInheritPitch) Result.Pitch = Ctrl.Pitch;
			if (bInheritYaw)   Result.Yaw   = Ctrl.Yaw;
			if (bInheritRoll)  Result.Roll  = Ctrl.Roll;
			DesiredParentRot = Result.ToQuaternion();
		}
	}

	// (2) Desired attach point — parent-space TargetOffset is anchored to the parent,
	// not the look pitch. SocketOffset remains camera/look-space below.
	const FVector DesiredAttachLoc = ParentWorldLoc + ParentActualRot.RotateVector(TargetOffset);
	const FQuat DesiredAttachRot = DesiredParentRot;

	// (3) Lag 적용 — 첫 Tick 은 desired 로 초기화 (아직 비교할 prev 없음).
	if (!bHasPreviousState)
	{
		LaggedAttachRot = DesiredAttachRot;
		LaggedAttachLoc = DesiredAttachLoc;
		bHasPreviousState = true;
	}
	else
	{
		if (bEnableCameraRotationLag && CameraRotationLagSpeed > 0.0f)
		{
			const float Alpha = CalculateExponentialLagAlpha(DeltaTime, CameraRotationLagSpeed);
			LaggedAttachRot = FQuat::Slerp(LaggedAttachRot, DesiredAttachRot, Alpha).GetNormalized();
		}
		else
		{
			LaggedAttachRot = DesiredAttachRot;
		}

		if (bEnableCameraLag && CameraLagSpeed > 0.0f)
		{
			const float Alpha = CalculateExponentialLagAlpha(DeltaTime, CameraLagSpeed);
			FVector NewLoc = LaggedAttachLoc + (DesiredAttachLoc - LaggedAttachLoc) * Alpha;

			// 너무 멀어지면 클램프 — 빠른 텔레포트/리스폰 직후 카메라가 한참 뒤따라오는 현상 방지.
			if (CameraLagMaxDistance > 0.0f)
			{
				const float DistSq = FVector::DistSquared(DesiredAttachLoc, NewLoc);
				const float MaxSq = CameraLagMaxDistance * CameraLagMaxDistance;
				if (DistSq > MaxSq)
				{
					const FVector Diff = DesiredAttachLoc - NewLoc;
					NewLoc = DesiredAttachLoc - Diff.Normalized() * CameraLagMaxDistance;
				}
			}
			LaggedAttachLoc = NewLoc;
		}
		else
		{
			LaggedAttachLoc = DesiredAttachLoc;
		}
	}

	// (4) ArmEnd 계산 — SpringArm 의 World 위치 (자식 카메라가 여기 부착됨).
	//     LaggedAttach 에서 Local -X 방향으로 TargetArmLength 만큼 + SocketOffset.
	const FVector ArmDirWorld = LaggedAttachRot.RotateVector(FVector(-TargetArmLength, 0.0f, 0.0f));
	const FVector SocketWorld = LaggedAttachRot.RotateVector(SocketOffset);
	FVector ArmEndWorld = LaggedAttachLoc + ArmDirWorld + SocketWorld;

	// (4b) Collision test — bDoCollisionTest 가 켜져 있으면 LaggedAttach → ArmEnd 방향으로
	//      ProbeSize > 0 일 때는 sphere sweep 을 수행한다. 카메라 중심선만 검사하던
	//      raycast 와 달리 ProbeSize 를 실제 sweep 반경으로 사용하므로 모서리/문틀/기둥
	//      옆 클리핑에 더 안전하다. ProbeSize 가 0 이면 기존처럼 raycast 로 처리한다.
	if (bDoCollisionTest)
	{
		AActor* Owner = GetOwner();
		UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		if (World)
		{
			const FVector Diff = ArmEndWorld - LaggedAttachLoc;
			const float Distance = Diff.Length();
			if (Distance > 1e-4f)
			{
				const FVector Dir = Diff / Distance;
				FHitResult Hit;
				bool bHit = false;
				if (ProbeSize > 0.0f)
				{
					const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(ProbeSize);
					bHit = World->PhysicsSweep(LaggedAttachLoc, ArmEndWorld, FQuat::Identity, ProbeShape, Hit, ProbeChannel, Owner);
				}
				else
				{
					bHit = World->PhysicsRaycast(LaggedAttachLoc, Dir, Distance, Hit, ProbeChannel, Owner);
				}

				if (bHit)
				{
					const float SafeDist = std::max(Hit.Distance, 0.0f);
					ArmEndWorld = LaggedAttachLoc + Dir * SafeDist;
				}
			}
		}
	}

	// (5) World transform 을 *Relative* 로 환산해서 RelativeTransform 에 set —
	//     SceneComponent 기본 합성 (Parent 실제 × Relative) 이 우리 의도한 World 를 자식에게 전달.
	//     ★ 반드시 ParentActualRot 의 inverse 사용 (DesiredParentRot 아님). 안 그러면
	//       (Desired)^-1 × Lagged 가 Desired ≈ Lagged 일 때 identity 되어 카메라 가 capsule
	//       회전만 따라감 — control rotation 이 무시되는 버그.
	const FQuat ParentInvRot = ParentActualRot.Inverse();
	const FVector RelLoc = ArmEndWorld * ParentWorldInv;
	const FQuat RelRot = (ParentInvRot * LaggedAttachRot).GetNormalized();

	SetRelativeLocation(RelLoc);
	SetRelativeRotation(RelRot);
}
