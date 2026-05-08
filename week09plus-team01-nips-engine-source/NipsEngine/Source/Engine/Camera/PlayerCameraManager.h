#pragma once

#include "Core/CoreMinimal.h"
#include "Engine/Camera/CameraModifier.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Render/Common/ViewTypes.h"
#include "Math/Color.h"
#include "Object/Object.h"
#include "Viewport/ViewportRect.h"

#include <type_traits>

class FViewportCamera;
class UCameraComponent;
class ULuaCameraModifierComponent;
class ULetterBoxCameraModifier;
class ULuaCameraModifier;
#include "Engine/Camera/Modifier/CameraShakeModifier.h"
struct FSceneView;
enum class EViewMode : int32;

// 최종적인 카메라 데이터를 RenderBus에 전달하기 위한 구조체
struct FCameraViewInfo
{
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;

	float FOV = 3.14159265358979f / 3.0f;
	float AspectRatio = 16.0f / 9.0f;
	float NearPlane = 0.1f;
	float FarPlane = 1000.0f;
	float OrthoWidth = 10.0f;
	float OrthoHeight = 10.0f;
	bool bOrthographic = false;

	FVector GetForwardVector() const { return Rotation.GetForwardVector(); }
	FVector GetRightVector() const { return Rotation.GetRightVector(); }
	FVector GetUpVector() const { return Rotation.GetUpVector(); }
};

struct FCameraTransitionState
{
	bool bActive = false;

	FCameraViewInfo FromView;
	FCameraViewInfo ToView;

	FVector ControlPointA = FVector::ZeroVector;
	FVector ControlPointB = FVector::ZeroVector;
	bool bUseBezierCurve = false;
	bool bHoldFinalView = false;

	float Duration = 0.0f;
	float Elapsed = 0.0f;

	// Cubic Bezier: Ease In/Out: 출발할 때 부드럽게 가속/도착할 때 부드럽게 감속
	FVector2 EaseControlPointA = FVector2(0.25f, 0.0f);
	FVector2 EaseControlPointB = FVector2(0.75f, 1.0f);
};

struct FCameraFadeState
{
	bool bActive = false;
	bool bHoldWhenFinished = false;

	FVector Color = FVector(0.0f, 0.0f, 0.0f);
	float FromAlpha = 0.0f;
	float ToAlpha = 0.0f;
	float CurrentAlpha = 0.0f;
	float Duration = 0.0f;
	float Elapsed = 0.0f;
};

struct FLuaCameraModifierComponentBinding
{
	ULuaCameraModifierComponent* Component = nullptr;
	FString ScriptPath;
	ULuaCameraModifier* Modifier = nullptr;
};

// 게임 내 카메라의 최종 결정자, 최종 위치/회전/FOV 값이 모두 APlayerCameraManager에서 결정
class APlayerCameraManager
{
public:
	APlayerCameraManager() = default;

	void SetViewTarget(UCameraComponent* InCamera);
	void SetViewTargetWithBlend(UCameraComponent* InCamera, float BlendTime = 0.0f);
	UCameraComponent* GetViewTarget() const { return ViewTarget; }

	void SetFallbackCamera(FViewportCamera* InCamera);
	FViewportCamera* GetFallbackCamera() const { return FallbackCamera; }

	void UpdateCamera(float DeltaTime);
	void BuildSceneView(FSceneView& OutView, const FViewportRect& ViewRect, EViewMode ViewMode) const;
	void InitializeDefaultModifiers();
	void Shutdown();

	const FCameraViewInfo& GetCameraView() const { return CachedCameraView; }
	const FPostProcessSettings& GetPostProcessSettings() const { return CachedPostProcessSettings; }
	const FCameraOverlaySettings& GetOverlaySettings() const { return CachedCameraOverlaySettings; }
	void SetManualCameraView(const FCameraViewInfo& View);
	void ClearManualCameraView();
	ULetterBoxCameraModifier* GetLetterBoxCameraModifier();
	const ULetterBoxCameraModifier* GetLetterBoxCameraModifier() const { return LetterBoxCameraModifier; }

	// Fade
	void StartCameraFade(const FVector& Color, float FromAlpha, float ToAlpha, float Duration, bool bHoldWhenFinished = false);
	void SetManualCameraFade(const FVector& Color, float Alpha);
	void StopCameraFade();
	bool IsCameraFading() const { return FadeState.bActive; }

	// Letterbox
	void StartLetterBox(float TargetRatio, float Duration);
	void SetLetterBox(float Ratio);
	void ClearLetterBox();

	// Camera shake
	UCameraShakeModifier* GetCameraShakeModifier();
	const UCameraShakeModifier* GetCameraShakeModifier() const { return CameraShakeModifier; }
	void StartCameraShake(const FCameraShakeParams& Params);
	bool StartCameraShakeByName(const FString& ShakeName);
	void StartNamedCameraShake(const FString& ShakeName, const FCameraShakeParams& Params);
	void StopCameraShake();
	void StopCameraShakeByName(const FString& ShakeName);
	bool IsCameraShaking() const;

	// Modifier
	template <typename TModifier>
	TModifier* AddNewCameraModifier();
	ULuaCameraModifier* AddLuaCameraModifier(const FString& ScriptPath);
	void RemoveCameraModifier(UCameraModifier* Modifier);

	// Transition
	void StartCameraTransition(const FCameraViewInfo& From, const FCameraViewInfo& To, float Duration);
	void StartCameraTransitionBezier(const FCameraViewInfo& From, const FCameraViewInfo& To, const FVector& ControlPointA, const FVector& ControlPointB, float Duration);
	void StopCameraTransition();
	void UpdateCameraTransition(float DeltaTime, FCameraViewInfo& InOutView);
	float EvaluateTransitionAlpha(float NormalizedTime) const;
	FCameraViewInfo BlendCameraView(float Alpha) const;
	FVector EvaluateBezierPosition(float Alpha) const;

private:
	bool BuildBaseCameraView(FCameraViewInfo& OutView) const;
	void UpdateCameraFade(float DeltaTime);

	bool AddCameraModifierToList(UCameraModifier* NewModifier);
	void ClearModifierList();

	void ApplyCameraModifiers(float DeltaTime, FCameraViewInfo& InOutView);
	void ApplyPostProcessModifiers(float DeltaTime, FPostProcessSettings& InOutSettings);
	void ApplyOverlayModifiers(float DeltaTime, FCameraOverlaySettings& InOutOverlay);
	void ApplyPostProcessComponent(FPostProcessSettings& InOutSettings);
	void ApplyCameraFade(FCameraOverlaySettings& InOutOverlay) const;
	void SyncLuaCameraModifierComponents();
	void RemoveLuaCameraModifierComponentBinding(size_t BindingIndex);

	void FillSceneView(FSceneView& OutView, const FCameraViewInfo& CameraView, const FViewportRect& ViewRect, EViewMode ViewMode) const;

private:
	UCameraComponent* ViewTarget = nullptr;
	FViewportCamera* FallbackCamera = nullptr;
	TArray<UCameraModifier*> ModifierList;
	TArray<UCameraModifier*> OwnedModifierList;
	TArray<FLuaCameraModifierComponentBinding> LuaCameraModifierComponentBindings;
	TMap<FString, ULuaCameraModifier*> NamedLuaCameraShakeModifiers;
	ULetterBoxCameraModifier* LetterBoxCameraModifier = nullptr;
	UCameraShakeModifier* CameraShakeModifier = nullptr;
	FString ActiveNamedCameraShake;
	
	bool bHasCachedCameraView = false;
	FCameraViewInfo CachedCameraView;
	bool bHasManualCameraView = false;
	FCameraViewInfo ManualCameraView;
	FPostProcessSettings CachedPostProcessSettings;
	FCameraOverlaySettings CachedCameraOverlaySettings;

	FCameraTransitionState Transition;
	FCameraFadeState FadeState;
};

template <typename TModifier>
TModifier* APlayerCameraManager::AddNewCameraModifier()
{
	static_assert(std::is_base_of_v<UCameraModifier, TModifier>, "TModifier must derive from UCameraModifier");

	TModifier* Modifier = UObjectManager::Get().CreateObject<TModifier>();
	OwnedModifierList.push_back(Modifier);
	Modifier->AddedToCamera(this);
	AddCameraModifierToList(Modifier);
	return Modifier;
}
