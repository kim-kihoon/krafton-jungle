#include "GameFramework/Camera/CameraShakeCameraModifier.h"
#include "GameFramework/Camera/CameraShakeBase.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "Math/Quat.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/UClass.h"
#include "Render/Types/MinimalViewInfo.h"
#include <algorithm>

namespace
{
	// PlaySpace 별 location offset 변환:
	//   World       — raw 가산
	//   CameraLocal — 현재 POV 회전 기준 회전
	//   UserDefined — UserPlaySpaceRot 기준 회전
	FVector ConvertShakeLocationToWorld(
		const FVector& Location,
		const FRotator& CameraRotation,
		ECameraShakePlaySpace PlaySpace,
		const FRotator& UserPlaySpaceRot)
	{
		switch (PlaySpace)
		{
		case ECameraShakePlaySpace::CameraLocal:
			return CameraRotation.ToQuaternion().RotateVector(Location);
		case ECameraShakePlaySpace::UserDefined:
			return UserPlaySpaceRot.ToQuaternion().RotateVector(Location);
		case ECameraShakePlaySpace::World:
		default:
			return Location;
		}
	}
}

UCameraModifier_CameraShake::UCameraModifier_CameraShake()
{
	// shake 는 비교적 늦게 적용 (다른 효과 위에 흔들림이 얹어지는 게 자연). priority 100.
	Priority = 100.0f;
}


void UCameraModifier_CameraShake::AddReferencedObjects(FReferenceCollector& Collector)
{
	UCameraModifier::AddReferencedObjects(Collector);
	Collector.AddReferencedObjects(ActiveShakes, "UCameraModifier_CameraShake.ActiveShakes");
}


bool UCameraModifier_CameraShake::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	// 베이스가 alpha 페이드 갱신 후 exclusive=false 반환.
	UCameraModifier::ModifyCamera(DeltaTime, InOutPOV);

	// 각 active shake 의 결과를 PlaySpace 기준으로 World offset 으로 변환 후 합산.
	FCameraShakeUpdateResult Sum;
	for (UCameraShakeBase* Shake : ActiveShakes)
	{
		if (!IsValid(Shake) || Shake->IsFinished()) continue;

		FCameraShakeUpdateResult Per;
		Shake->UpdateAndApplyCameraShake(DeltaTime, Per);

		Sum.Location += ConvertShakeLocationToWorld(
			Per.Location,
			InOutPOV.Rotation,
			Shake->GetPlaySpace(),
			Shake->GetUserPlaySpaceRot());
		Sum.Rotation.Pitch += Per.Rotation.Pitch;
		Sum.Rotation.Yaw   += Per.Rotation.Yaw;
		Sum.Rotation.Roll  += Per.Rotation.Roll;
		Sum.FOV            += Per.FOV;
	}

	// 종료된 셰이크 정리 — UpdateAndApplyCameraShake 안에서 IsFinished 토글된 것 포함.
	ActiveShakes.erase(
		std::remove_if(ActiveShakes.begin(), ActiveShakes.end(),
			[](UCameraShakeBase* S) { return !IsValid(S) || S->IsFinished(); }),
		ActiveShakes.end());

	LastAppliedShakeOffset = FCameraShakeUpdateResult();

	// Alpha 가 0 이면 효과 죽음 (Disable 진행 후) — 가산 skip.
	if (Alpha <= 0.0f)
	{
		return false;
	}

	const float AppliedAlpha = Alpha;
	LastAppliedShakeOffset.Location = Sum.Location * AppliedAlpha;
	LastAppliedShakeOffset.Rotation.Pitch = Sum.Rotation.Pitch * AppliedAlpha;
	LastAppliedShakeOffset.Rotation.Yaw = Sum.Rotation.Yaw * AppliedAlpha;
	LastAppliedShakeOffset.Rotation.Roll = Sum.Rotation.Roll * AppliedAlpha;
	LastAppliedShakeOffset.FOV = Sum.FOV * AppliedAlpha;

	InOutPOV.Location       += LastAppliedShakeOffset.Location;
	InOutPOV.Rotation.Pitch += LastAppliedShakeOffset.Rotation.Pitch;
	InOutPOV.Rotation.Yaw   += LastAppliedShakeOffset.Rotation.Yaw;
	InOutPOV.Rotation.Roll  += LastAppliedShakeOffset.Rotation.Roll;
	InOutPOV.FOV            += LastAppliedShakeOffset.FOV;
	return false;
}

UCameraShakeBase* UCameraModifier_CameraShake::StartShake(
	UClass* ShakeClass,
	float Scale,
	ECameraShakePlaySpace PlaySpace,
	FRotator UserPlaySpaceRot)
{
	if (!ShakeClass || !IsValid(CameraOwner)) return nullptr;

	ActiveShakes.erase(
		std::remove_if(ActiveShakes.begin(), ActiveShakes.end(),
			[](UCameraShakeBase* S) { return !IsValid(S) || S->IsFinished(); }),
		ActiveShakes.end());

	// bSingleInstance 처리 — UE 동작 미러: 같은 ShakeClass 의 인스턴스가 이미 활성이고
	// 그 인스턴스가 bSingleInstance=true 면 새로 생성하지 않고 그 인스턴스를 재시작.
	// (CDO 미지원 환경이라 "기존 인스턴스의 플래그" 로 판정 — 첫 인스턴스 이후 효과 적용.)
	for (UCameraShakeBase* Existing : ActiveShakes)
	{
		if (IsValid(Existing) && !Existing->IsFinished()
			&& Existing->bSingleInstance
			&& Existing->GetClass()->IsA(ShakeClass))
		{
			Existing->StartShake(CameraOwner, Scale, PlaySpace, UserPlaySpaceRot);
			return Existing;
		}
	}

	UObject* Obj = FObjectFactory::Get().Create(ShakeClass->GetName(), this);
	UCameraShakeBase* Shake = Cast<UCameraShakeBase>(Obj);
	if (!Shake)
	{
		if (Obj) UObjectManager::Get().DestroyObject(Obj);
		return nullptr;
	}

	Shake->StartShake(CameraOwner, Scale, PlaySpace, UserPlaySpaceRot);
	ActiveShakes.push_back(Shake);
	return Shake;
}

void UCameraModifier_CameraShake::StopShake(UCameraShakeBase* ShakeInstance, bool bImmediately)
{
	if (!IsValid(ShakeInstance)) return;
	ShakeInstance->StopShake(bImmediately);
}

void UCameraModifier_CameraShake::StopAllShakes(bool bImmediately)
{
	for (UCameraShakeBase* Shake : ActiveShakes)
	{
		if (IsValid(Shake)) Shake->StopShake(bImmediately);
	}
	ActiveShakes.erase(
		std::remove_if(ActiveShakes.begin(), ActiveShakes.end(),
			[](UCameraShakeBase* S) { return !IsValid(S) || S->IsFinished(); }),
		ActiveShakes.end());
}

void UCameraModifier_CameraShake::StopAllInstancesOfShake(UClass* ShakeClass, bool bImmediately)
{
	if (!ShakeClass) return;
	for (UCameraShakeBase* Shake : ActiveShakes)
	{
		if (IsValid(Shake) && Shake->GetClass()->IsA(ShakeClass))
		{
			Shake->StopShake(bImmediately);
		}
	}
	ActiveShakes.erase(
		std::remove_if(ActiveShakes.begin(), ActiveShakes.end(),
			[](UCameraShakeBase* S) { return !IsValid(S) || S->IsFinished(); }),
		ActiveShakes.end());
}
