#include "Engine/Camera/PlayerCameraManager.h"

#include "Engine/Component/CameraComponent.h"
#include "Engine/Component/LuaCameraModifierComponent.h"
#include "Engine/Component/PostProcessComponent.h"
#include "Engine/GameFramework/AActor.h"
#include "Engine/Camera/Modifier/LetterBoxCameraModifier.h"
#include "Engine/Camera/Modifier/CameraShakeModifier.h"
#include "Engine/Camera/Modifier/LuaCameraModifier.h"
#include "Engine/Runtime/SceneView.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Engine/Math/Bezier.h"
#include "Engine/Math/Utils.h"
#include "Core/Logger.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
FQuat MakeCameraRotation(const FVector& Forward, const FVector& Right, const FVector& Up)
{
	const FVector XAxis = Forward.GetSafeNormal();
	if (XAxis.IsNearlyZero())
	{
		return FQuat::Identity;
	}

	FVector YAxis = Right - XAxis * FVector::DotProduct(Right, XAxis);
	YAxis = YAxis.GetSafeNormal();
	if (YAxis.IsNearlyZero())
	{
		YAxis = FVector::CrossProduct(Up, XAxis).GetSafeNormal();
	}

	if (YAxis.IsNearlyZero())
	{
		const FVector UpCandidate = std::abs(XAxis.Z) < 0.999f ? FVector::UpVector : FVector::RightVector;
		YAxis = FVector::CrossProduct(UpCandidate, XAxis).GetSafeNormal();
	}

	const FVector ZAxis = FVector::CrossProduct(XAxis, YAxis).GetSafeNormal();
	if (ZAxis.IsNearlyZero())
	{
		return FQuat::Identity;
	}

	YAxis = FVector::CrossProduct(ZAxis, XAxis).GetSafeNormal();

	FMatrix RotationMatrix = FMatrix::Identity;
	RotationMatrix.SetAxes(XAxis, YAxis, ZAxis);

	FQuat Rotation(RotationMatrix);
	Rotation.Normalize();
	return Rotation;
}

bool BuildCameraComponentView(UCameraComponent* Camera, FCameraViewInfo& OutView)
{
	if (Camera == nullptr)
	{
		return false;
	}

	const float Height = Camera->GetHeight();

	OutView.Location = Camera->GetWorldLocation();
	OutView.Rotation = MakeCameraRotation(Camera->GetForwardVector(), Camera->GetRightVector(), Camera->GetUpVector());
	OutView.FOV = Camera->GetFOV();
	OutView.AspectRatio = Height > 0.0f ? Camera->GetWidth() / Height : 16.0f / 9.0f;
	OutView.NearPlane = Camera->GetNearPlane();
	OutView.FarPlane = Camera->GetFarPlane();
	OutView.OrthoWidth = Camera->GetOrthoWidth();
	OutView.OrthoHeight = OutView.AspectRatio > 0.0f ? OutView.OrthoWidth / OutView.AspectRatio : OutView.OrthoWidth;
	OutView.bOrthographic = Camera->IsOrthogonal();
	return true;
}

FString NormalizeCameraShakeName(FString ShakeName)
{
	std::transform(ShakeName.begin(), ShakeName.end(), ShakeName.begin(),
				   [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });

	if (ShakeName == "walk")
	{
		return "Walk";
	}

	if (ShakeName == "falldown" || ShakeName == "fall_down" || ShakeName == "fall-down")
	{
		return "FallDown";
	}

	return "";
}

FString GetCameraShakeAssetPath(const FString& NormalizedName)
{
	if (NormalizedName.empty())
	{
		return "";
	}

	return "Asset/CameraShake/" + NormalizedName + ".lua";
}
}

void APlayerCameraManager::SetViewTarget(UCameraComponent* InCamera)
{
	ViewTarget = InCamera;
	ClearManualCameraView();
	StopCameraTransition();
}

void APlayerCameraManager::SetViewTargetWithBlend(UCameraComponent* InCamera, float BlendTime)
{
	if (InCamera == nullptr || BlendTime <= 0.0f)
	{
		SetViewTarget(InCamera); // give-up blending transition
		return;
	}

	FCameraViewInfo ToView;
	if (!BuildCameraComponentView(InCamera, ToView))
	{
		SetViewTarget(InCamera);
		return;
	}

	FCameraViewInfo FromView;
	if (bHasCachedCameraView)
	{
		FromView = CachedCameraView;
	}
	else if (!BuildBaseCameraView(FromView))
	{
		FromView = ToView;
	}

	ViewTarget = InCamera;
	ClearManualCameraView();
	StartCameraTransition(FromView, ToView, BlendTime);
	Transition.bHoldFinalView = false;
}

void APlayerCameraManager::SetFallbackCamera(FViewportCamera* InCamera)
{
	FallbackCamera = InCamera;
}

void APlayerCameraManager::UpdateCamera(float DeltaTime)
{
	FCameraViewInfo NewView;
	FPostProcessSettings NewPostProcess;
	FCameraOverlaySettings NewOverlay;

	if (!BuildBaseCameraView(NewView))
	{
		bHasCachedCameraView = false;
		return;
	}

	if (Transition.bActive)
	{
		UpdateCameraTransition(DeltaTime, NewView);
	}
	
	UpdateCameraFade(DeltaTime);
	SyncLuaCameraModifierComponents();

	ApplyCameraModifiers(DeltaTime, NewView);
	ApplyPostProcessComponent(NewPostProcess);
	ApplyPostProcessModifiers(DeltaTime, NewPostProcess);
	ApplyOverlayModifiers(DeltaTime, NewOverlay);
	ApplyCameraFade(NewOverlay);

	CachedCameraView = NewView;
	CachedPostProcessSettings = NewPostProcess;
	CachedCameraOverlaySettings = NewOverlay;

	bHasCachedCameraView = true;
}

void APlayerCameraManager::BuildSceneView(FSceneView& OutView, const FViewportRect& ViewRect, EViewMode ViewMode) const
{
	FCameraViewInfo ViewInfo = CachedCameraView;
	if (!bHasCachedCameraView && !BuildBaseCameraView(ViewInfo))
	{
		OutView.ViewRect = ViewRect;
		OutView.ViewMode = ViewMode;
		return;
	}

	FillSceneView(OutView, ViewInfo, ViewRect, ViewMode);
}

void APlayerCameraManager::InitializeDefaultModifiers()
{
	if (LetterBoxCameraModifier == nullptr)
	{
		LetterBoxCameraModifier = AddNewCameraModifier<ULetterBoxCameraModifier>();
	}
}

void APlayerCameraManager::Shutdown()
{
	ClearModifierList();
	LuaCameraModifierComponentBindings.clear();
	NamedLuaCameraShakeModifiers.clear();

	for (UCameraModifier* Modifier : OwnedModifierList)
	{
		UObjectManager::Get().DestroyObject(Modifier);
	}
	OwnedModifierList.clear();

	LetterBoxCameraModifier = nullptr;
	CameraShakeModifier = nullptr;
	ActiveNamedCameraShake.clear();
	LuaCameraModifierComponentBindings.clear();
	ViewTarget = nullptr;
	FallbackCamera = nullptr;
	bHasCachedCameraView = false;
	CachedCameraView = FCameraViewInfo();
	bHasManualCameraView = false;
	ManualCameraView = FCameraViewInfo();
	CachedPostProcessSettings = FPostProcessSettings();
	CachedCameraOverlaySettings = FCameraOverlaySettings();
	Transition = FCameraTransitionState();
	FadeState = FCameraFadeState();
}

void APlayerCameraManager::SetManualCameraView(const FCameraViewInfo& View)
{
	ManualCameraView = View;
	bHasManualCameraView = true;
	CachedCameraView = View;
	bHasCachedCameraView = true;
}

void APlayerCameraManager::ClearManualCameraView()
{
	bHasManualCameraView = false;
	ManualCameraView = FCameraViewInfo();
}

ULetterBoxCameraModifier* APlayerCameraManager::GetLetterBoxCameraModifier()
{
	InitializeDefaultModifiers();
	return LetterBoxCameraModifier;
}

ULuaCameraModifier* APlayerCameraManager::AddLuaCameraModifier(const FString& ScriptPath)
{
	ULuaCameraModifier* Modifier = UObjectManager::Get().CreateObject<ULuaCameraModifier>();
	Modifier->SetScriptPath(ScriptPath);
	OwnedModifierList.push_back(Modifier);
	Modifier->AddedToCamera(this);
	AddCameraModifierToList(Modifier);
	return Modifier;
}

bool APlayerCameraManager::AddCameraModifierToList(UCameraModifier* NewModifier)
{
	if (NewModifier == nullptr)
	{
		return false;
	}

	if (std::find(ModifierList.begin(), ModifierList.end(), NewModifier) != ModifierList.end())
	{
		return false;
	}

	ModifierList.push_back(NewModifier);
	std::sort(ModifierList.begin(), ModifierList.end(), [](const UCameraModifier* A, const UCameraModifier* B)
			  {
		const int32 APriority = A ? A->GetPriority() : 0;
		const int32 BPriority = B ? B->GetPriority() : 0;
		return APriority < BPriority; });

	return true;
}

void APlayerCameraManager::RemoveCameraModifier(UCameraModifier* Modifier)
{
	if (Modifier == nullptr)
	{
		return;
	}

	const auto OldSize = ModifierList.size();
	ModifierList.erase(std::remove(ModifierList.begin(), ModifierList.end(), Modifier), ModifierList.end());
	if (ModifierList.size() != OldSize)
	{
		Modifier->RemovedFromCamera(this);
	}
}

void APlayerCameraManager::RemoveLuaCameraModifierComponentBinding(size_t BindingIndex)
{
	if (BindingIndex >= LuaCameraModifierComponentBindings.size())
	{
		return;
	}

	ULuaCameraModifier* Modifier = LuaCameraModifierComponentBindings[BindingIndex].Modifier;
	if (Modifier != nullptr)
	{
		RemoveCameraModifier(Modifier);
		OwnedModifierList.erase(std::remove(OwnedModifierList.begin(), OwnedModifierList.end(), Modifier), OwnedModifierList.end());
		UObjectManager::Get().DestroyObject(Modifier);
	}

	LuaCameraModifierComponentBindings.erase(LuaCameraModifierComponentBindings.begin() + BindingIndex);
}

void APlayerCameraManager::ClearModifierList()
{
	for (UCameraModifier* Modifier : ModifierList)
	{
		if (Modifier)
		{
			Modifier->RemovedFromCamera(this);
		}
	}
	ModifierList.clear();
}

void APlayerCameraManager::StartCameraFade(const FVector& Color, float FromAlpha, float ToAlpha, float Duration, bool bHoldWhenFinished)
{
	FadeState.bActive = true;
	FadeState.bHoldWhenFinished = bHoldWhenFinished;
	FadeState.Color = Color;
	FadeState.FromAlpha = MathUtil::Clamp(FromAlpha, 0.0f, 1.0f);
	FadeState.ToAlpha = MathUtil::Clamp(ToAlpha, 0.0f, 1.0f);
	FadeState.CurrentAlpha = FadeState.FromAlpha;
	FadeState.Duration = std::max(Duration, 0.0f);
	FadeState.Elapsed = 0.0f;

	if (FadeState.Duration <= 0.0f)
	{
		FadeState.CurrentAlpha = FadeState.ToAlpha;
		FadeState.bActive = false;

		if (!FadeState.bHoldWhenFinished)
		{
			FadeState.CurrentAlpha = 0.0f;
		}
	}
}

void APlayerCameraManager::SetManualCameraFade(const FVector& Color, float Alpha)
{
	FadeState.bActive = false;
	FadeState.bHoldWhenFinished = true;
	FadeState.Color = Color;
	FadeState.FromAlpha = MathUtil::Clamp(Alpha, 0.0f, 1.0f);
	FadeState.ToAlpha = FadeState.FromAlpha;
	FadeState.CurrentAlpha = FadeState.FromAlpha;
	FadeState.Duration = 0.0f;
	FadeState.Elapsed = 0.0f;
}

void APlayerCameraManager::StopCameraFade()
{
	FadeState = FCameraFadeState();
}

void APlayerCameraManager::StartLetterBox(float TargetRatio, float Duration)
{
	if (ULetterBoxCameraModifier* Modifier = GetLetterBoxCameraModifier())
	{
		Modifier->StartLetterBox(TargetRatio, Duration);
	}
}

void APlayerCameraManager::SetLetterBox(float Ratio)
{
	if (ULetterBoxCameraModifier* Modifier = GetLetterBoxCameraModifier())
	{
		Modifier->SetLetterBox(Ratio);
	}
}

void APlayerCameraManager::ClearLetterBox()
{
	if (ULetterBoxCameraModifier* Modifier = GetLetterBoxCameraModifier())
	{
		Modifier->ClearLetterBox();
	}
}

UCameraShakeModifier* APlayerCameraManager::GetCameraShakeModifier()
{
	if (CameraShakeModifier == nullptr)
	{
		CameraShakeModifier = AddNewCameraModifier<UCameraShakeModifier>();
	}

	return CameraShakeModifier;
}
void APlayerCameraManager::StartCameraShake(const FCameraShakeParams& Params)
{
	ActiveNamedCameraShake.clear();

	UCameraShakeModifier* Modifier = GetCameraShakeModifier();
	if (Modifier == nullptr)
		return;

	Modifier->StartShake(Params);
}

bool APlayerCameraManager::StartCameraShakeByName(const FString& ShakeName)
{
	const FString NormalizedName = NormalizeCameraShakeName(ShakeName);
	const FString ScriptPath = GetCameraShakeAssetPath(NormalizedName);
	if (NormalizedName.empty() || ScriptPath.empty())
	{
		UE_LOG("PlayerCameraManager: unknown camera shake asset '%s'.", ShakeName.c_str());
		return false;
	}

	ULuaCameraModifier* Modifier = nullptr;
	const auto It = NamedLuaCameraShakeModifiers.find(NormalizedName);
	if (It != NamedLuaCameraShakeModifiers.end())
	{
		Modifier = It->second;
	}

	if (Modifier == nullptr)
	{
		Modifier = AddLuaCameraModifier(ScriptPath);
		NamedLuaCameraShakeModifiers[NormalizedName] = Modifier;
	}
	else
	{
		Modifier->SetScriptPath(ScriptPath);
		Modifier->ReloadScript();
	}

	Modifier->SetActionSourceName(NormalizedName);
	if (!Modifier->IsScriptLoaded())
	{
		UE_LOG("PlayerCameraManager: failed to load camera shake asset '%s': %s", ScriptPath.c_str(), Modifier->GetLastScriptError().c_str());
		return false;
	}

	return true;
}

void APlayerCameraManager::StartNamedCameraShake(const FString& ShakeName, const FCameraShakeParams& Params)
{
	ActiveNamedCameraShake = NormalizeCameraShakeName(ShakeName);

	UCameraShakeModifier* Modifier = GetCameraShakeModifier();
	if (Modifier == nullptr)
		return;

	Modifier->StartShake(Params);
}

void APlayerCameraManager::StopCameraShake()
{
	ActiveNamedCameraShake.clear();

	if (CameraShakeModifier)
	{
		CameraShakeModifier->StopShake();
	}
}

void APlayerCameraManager::StopCameraShakeByName(const FString& ShakeName)
{
	const FString NormalizedName = NormalizeCameraShakeName(ShakeName);
	if (!NormalizedName.empty() && ActiveNamedCameraShake == NormalizedName)
	{
		StopCameraShake();
	}
}

bool APlayerCameraManager::IsCameraShaking() const
{
	return CameraShakeModifier && CameraShakeModifier->GetIsShaking();
}

// 카메라 Linear 보간 이동
void APlayerCameraManager::StartCameraTransition(const FCameraViewInfo& From, const FCameraViewInfo& To, float Duration)
{
	Transition.FromView = From;
	Transition.ToView = To;

	Transition.Duration = std::max(Duration, 0.001f);
	Transition.Elapsed = 0.0f;

	Transition.bUseBezierCurve = false;
	Transition.bHoldFinalView = true;
	Transition.bActive = true;
}

// 카메라 Bezier Curve 보간 이동
void APlayerCameraManager::StartCameraTransitionBezier(const FCameraViewInfo& From, const FCameraViewInfo& To, const FVector& ControlPointA, const FVector& ControlPointB, float Duration)
{
	Transition.FromView = From;
	Transition.ToView = To;

	Transition.ControlPointA = ControlPointA;
	Transition.ControlPointB = ControlPointB;

	Transition.Duration = std::max(Duration, 0.001f);
	Transition.Elapsed = 0.0f;

	Transition.bUseBezierCurve = true;
	Transition.bHoldFinalView = true;
	Transition.bActive = true;
}

void APlayerCameraManager::StopCameraTransition()
{
	Transition.bActive = false;
}

void APlayerCameraManager::UpdateCameraTransition(float DeltaTime, FCameraViewInfo& InOutView)
{
	if (!Transition.bActive)
	{
		return;
	}

	Transition.Elapsed += DeltaTime;
	float NormalizedTime = MathUtil::Clamp(Transition.Elapsed / Transition.Duration, 0.0f, 1.0f);
	float Alpha = EvaluateTransitionAlpha(NormalizedTime);
	InOutView = BlendCameraView(Alpha);

	if (NormalizedTime >= 1.0f)
	{
		const bool bHoldFinalView = Transition.bHoldFinalView;
		const FCameraViewInfo FinalView = Transition.ToView;
		StopCameraTransition();
		if (bHoldFinalView)
		{
			SetManualCameraView(FinalView);
		}
	}
}
	
// 정규화된 시간 값을 cubic-bezier easing curve의 x, y 값으로 변환합니다.
float APlayerCameraManager::EvaluateTransitionAlpha(float NormalizedTime) const
{
	const float ClampedTime = MathUtil::Clamp(NormalizedTime, 0.0f, 1.0f);
	const float ControlPoints[6] =
	{
		Transition.EaseControlPointA.X,
		Transition.EaseControlPointA.Y,
		Transition.EaseControlPointB.X,
		Transition.EaseControlPointB.Y,
		0.0f,
		1.0f
	};
	const float Alpha = Bezier::EvaluateCubicEasing(ClampedTime, ControlPoints);

	return MathUtil::Clamp(Alpha, 0.0f, 1.0f);
}

// 계산된 Transition Alpha 값을 바탕으로 From, To 카메라에 대한 위치, 회전, FOV 값 보간
FCameraViewInfo APlayerCameraManager::BlendCameraView(float Alpha) const
{
	FCameraViewInfo BlendedView;

	if (Transition.bUseBezierCurve)
	{
		BlendedView.Location = EvaluateBezierPosition(Alpha);
	}
	else
	{
		BlendedView.Location = FVector::Lerp(Transition.FromView.Location, Transition.ToView.Location, Alpha);
	}

	const FCameraViewInfo& From = Transition.FromView;
	const FCameraViewInfo& To = Transition.ToView;

	BlendedView.Rotation = FQuat::Slerp(From.Rotation, To.Rotation, Alpha);
	BlendedView.FOV = MathUtil::Lerp(From.FOV, To.FOV, Alpha);
	BlendedView.AspectRatio = MathUtil::Lerp(From.AspectRatio, To.AspectRatio, Alpha);
	BlendedView.NearPlane = MathUtil::Lerp(From.NearPlane, To.NearPlane, Alpha);
	BlendedView.FarPlane = MathUtil::Lerp(From.FarPlane, To.FarPlane, Alpha);
	BlendedView.OrthoWidth = MathUtil::Lerp(From.OrthoWidth, To.OrthoWidth, Alpha);
	BlendedView.OrthoHeight = MathUtil::Lerp(From.OrthoHeight, To.OrthoHeight, Alpha);
	BlendedView.bOrthographic = Alpha < 0.5f ? From.bOrthographic : To.bOrthographic;

	return BlendedView;
}

FVector APlayerCameraManager::EvaluateBezierPosition(float Alpha) const
{
	float T = Alpha;
	float U = 1.0f - Alpha;

	const FVector& P0 = Transition.FromView.Location;
	const FVector& P1 = Transition.ControlPointA;
	const FVector& P2 = Transition.ControlPointB;
	const FVector& P3 = Transition.ToView.Location;

	FVector Position = (U * U * U * P0) + (3.0f * U * U * T * P1) + (3.0f * U * T * T * P2) + (T * T * T * P3);

	return Position;
}

// ViewTarget이 유효하다면 ViewTarget을 기준으로 Base Camera View 생성
bool APlayerCameraManager::BuildBaseCameraView(FCameraViewInfo& OutView) const
{
	if (bHasManualCameraView)
	{
		OutView = ManualCameraView;
		return true;
	}

	if (BuildCameraComponentView(ViewTarget, OutView))
	{
		return true;
	}

	if (FallbackCamera)
	{
		OutView.Location = FallbackCamera->GetLocation();
		OutView.Rotation = FallbackCamera->GetRotation();
		OutView.FOV = FallbackCamera->GetFOV();
		OutView.AspectRatio = FallbackCamera->GetAspectRatio();
		OutView.NearPlane = FallbackCamera->GetNearPlane();
		OutView.FarPlane = FallbackCamera->GetFarPlane();
		OutView.OrthoWidth = FallbackCamera->GetOrthoHeight() * FallbackCamera->GetAspectRatio();
		OutView.OrthoHeight = FallbackCamera->GetOrthoHeight();
		OutView.bOrthographic = FallbackCamera->IsOrthographic();
		return true;
	}

	return false;
}

void APlayerCameraManager::UpdateCameraFade(float DeltaTime)
{
	if (!FadeState.bActive)
	{
		return;
	}

	FadeState.Elapsed += DeltaTime;
	const float Alpha = FadeState.Duration > 0.0f ? MathUtil::Clamp(FadeState.Elapsed / FadeState.Duration, 0.0f, 1.0f) : 1.0f;
	FadeState.CurrentAlpha = MathUtil::Lerp(FadeState.FromAlpha, FadeState.ToAlpha, Alpha);

	if (Alpha >= 1.0f)
	{
		FadeState.CurrentAlpha = FadeState.ToAlpha;
		FadeState.bActive = false;

		if (!FadeState.bHoldWhenFinished)
		{
			FadeState.CurrentAlpha = 0.0f;
		}
	}
}

void APlayerCameraManager::ApplyCameraModifiers(float DeltaTime, FCameraViewInfo& InOutView)
{
	for (UCameraModifier* Modifier : ModifierList)
	{
		if (Modifier == nullptr || !Modifier->IsEnabled())
		{
			continue;
		}

		Modifier->ModifyCamera(DeltaTime, InOutView);
	}
}

void APlayerCameraManager::SyncLuaCameraModifierComponents()
{
	AActor* OwnerActor = ViewTarget ? ViewTarget->GetOwner() : nullptr;

	TArray<ULuaCameraModifierComponent*> ActiveComponents;
	if (OwnerActor != nullptr)
	{
		for (UActorComponent* Component : OwnerActor->GetComponents())
		{
			ULuaCameraModifierComponent* LuaModifierComponent = Cast<ULuaCameraModifierComponent>(Component);
			if (LuaModifierComponent == nullptr || !LuaModifierComponent->IsActive() || LuaModifierComponent->GetScriptPath().empty())
			{
				continue;
			}

			ActiveComponents.push_back(LuaModifierComponent);
		}
	}

	for (size_t BindingIndex = LuaCameraModifierComponentBindings.size(); BindingIndex > 0; --BindingIndex)
	{
		FLuaCameraModifierComponentBinding& Binding = LuaCameraModifierComponentBindings[BindingIndex - 1];
		if (std::find(ActiveComponents.begin(), ActiveComponents.end(), Binding.Component) == ActiveComponents.end())
		{
			RemoveLuaCameraModifierComponentBinding(BindingIndex - 1);
		}
	}

	for (ULuaCameraModifierComponent* Component : ActiveComponents)
	{
		const FString& ScriptPath = Component->GetScriptPath();
		auto ExistingBinding = std::find_if(
			LuaCameraModifierComponentBindings.begin(),
			LuaCameraModifierComponentBindings.end(),
			[Component](const FLuaCameraModifierComponentBinding& Binding)
			{
				return Binding.Component == Component;
			});

		if (ExistingBinding != LuaCameraModifierComponentBindings.end())
		{
			if (ExistingBinding->Modifier != nullptr && ExistingBinding->ScriptPath != ScriptPath)
			{
				ExistingBinding->ScriptPath = ScriptPath;
				ExistingBinding->Modifier->SetScriptPath(ScriptPath);
				ExistingBinding->Modifier->ReloadScript();
			}
			continue;
		}

		ULuaCameraModifier* Modifier = AddLuaCameraModifier(ScriptPath);
		LuaCameraModifierComponentBindings.push_back({ Component, ScriptPath, Modifier });
	}
}

void APlayerCameraManager::ApplyPostProcessModifiers(float DeltaTime, FPostProcessSettings& InOutSettings)
{
	for (UCameraModifier* Modifier : ModifierList)
	{
		if (Modifier == nullptr || !Modifier->IsEnabled())
		{
			continue;
		}

		Modifier->ModifyPostProcess(DeltaTime, InOutSettings);
	}
}

void APlayerCameraManager::ApplyOverlayModifiers(float DeltaTime, FCameraOverlaySettings& InOutOverlay)
{
	for (UCameraModifier* Modifier : ModifierList)
	{
		if (Modifier == nullptr || !Modifier->IsEnabled()) continue;
		Modifier->ModifyOverlay(DeltaTime, InOutOverlay);
	}
}

void APlayerCameraManager::ApplyCameraFade(FCameraOverlaySettings& InOutOverlay) const
{
	InOutOverlay.FadeColor = FVector4(FadeState.Color, MathUtil::Clamp(FadeState.CurrentAlpha, 0.0f, 1.0f));
}

// UCameraComponent의 Owner Actor를 찾은 뒤 Owner에게 PostProcessComponent가 있다면 적용합니다.
// 추후 Volume이 있는 UPostProcessComponent를 추가한다면 변경합니다.
void APlayerCameraManager::ApplyPostProcessComponent(FPostProcessSettings& InOutSettings)
{
	if (ViewTarget == nullptr)
	{
		return;
	}

	AActor* OwnerActor = ViewTarget->GetOwner();
	if (OwnerActor == nullptr)
	{
		return;
	}

	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		UPostProcessComponent* PostProcess = Cast<UPostProcessComponent>(Component);
		if (PostProcess == nullptr || !PostProcess->IsActive())
		{
			continue;
		}

		if (PostProcess->IsEnableVignette())
		{
			InOutSettings.VignetteIntensity = PostProcess->GetVignetteIntensity();
			InOutSettings.VignetteRadius = PostProcess->GetVignetteRadius();
			InOutSettings.VignetteSoftness = PostProcess->GetVignetteSoftness();
		}

		if (PostProcess->IsEnableGammaCorrection())
		{
			InOutSettings.Gamma = PostProcess->GetGamma();
		}
		
		break;
	}
}

void APlayerCameraManager::FillSceneView(FSceneView& OutView, const FCameraViewInfo& CameraView, const FViewportRect& ViewRect, EViewMode ViewMode) const
{
	const FVector Forward = CameraView.GetForwardVector().GetSafeNormal();
	const FVector Right = CameraView.GetRightVector().GetSafeNormal();
	const FVector Up = CameraView.GetUpVector().GetSafeNormal();

	OutView.View = FMatrix::MakeViewLookAtLH(CameraView.Location, CameraView.Location + Forward, Up);
	if (CameraView.bOrthographic)
	{
		OutView.Proj = FMatrix::MakeOrthographicLH(
			CameraView.OrthoWidth,
			CameraView.OrthoHeight,
			CameraView.NearPlane,
			CameraView.FarPlane);
	}
	else
	{
		OutView.Proj = FMatrix::MakePerspectiveFovLH(
			CameraView.FOV,
			CameraView.AspectRatio,
			CameraView.NearPlane,
			CameraView.FarPlane);
	}

	OutView.CameraPosition = CameraView.Location;
	OutView.CameraForward = Forward;
	OutView.CameraRight = Right;
	OutView.CameraUp = Up;
	OutView.NearPlane = CameraView.NearPlane;
	OutView.FarPlane = CameraView.FarPlane;
	OutView.bOrthographic = CameraView.bOrthographic;
	OutView.CameraOrthoHeight = CameraView.OrthoHeight;
	OutView.CameraFrustum.UpdateFromCamera(OutView.View, OutView.Proj);
	OutView.ViewRect = ViewRect;
	OutView.ViewMode = ViewMode;
	OutView.PostProcessSettings = CachedPostProcessSettings;
	OutView.CameraOverlaySettings = CachedCameraOverlaySettings;
}
