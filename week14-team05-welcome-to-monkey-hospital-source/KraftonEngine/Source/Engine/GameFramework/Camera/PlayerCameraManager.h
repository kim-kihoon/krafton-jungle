#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Types/EngineTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Camera/CameraTypes.h"
#include "Render/Types/MinimalViewInfo.h"

#include "Source/Engine/GameFramework/Camera/PlayerCameraManager.generated.h"
class UCameraComponent;
class UCameraModifier;
class UCameraModifier_CameraShake;
class UCameraShakeAsset;
class UCameraShakeBase;
class UClass;

// UE: APlayerCameraManager — PC 가 소유하는 카메라 매니저. AActor 기반.
// E.1: UCameraManager 에서 변환. 호출자 시그니처/인터페이스 동일, 베이스만 AActor.
UCLASS()
class APlayerCameraManager : public AActor
{
public:
	GENERATED_BODY()
	APlayerCameraManager() = default;
	~APlayerCameraManager() override = default;

	// ─── Camera 등록 / Active / Possess ────────────────────────────
	UFUNCTION(Callable, Category="Camera")
	void RegisterCamera(UCameraComponent* Camera);
	UFUNCTION(Callable, Category="Camera")
	void UnregisterCamera(UCameraComponent* Camera);

	UFUNCTION(Callable, Category="Camera")
	void AutoPossessDefaultCamera();
	// BlendTime > 0 이면 새 ActiveCamera 로 부드럽게 보간 전환 (PossessedCamera 는 즉시 swap).
	UFUNCTION(Callable, Category="Camera")
	bool ToggleActiveCameraForActor(const FString& ActorName, float BlendTime = 0.0f);
	UFUNCTION(Callable, Category="Camera")
	bool ToggleActiveCameraForActor(const AActor* Actor, float BlendTime = 0.0f);

	UFUNCTION(Pure, Category="Camera")
	UCameraComponent* GetActiveCamera() const;
	UFUNCTION(Callable, Category="Camera")
	void SetActiveCamera(UCameraComponent* NewCamera);

	// ActiveCamera 단위 blend — 같은 액터의 다른 카메라 컴포넌트 사이 부드럽게 전환.
	// (UE 의 SetViewTargetWithBlend 가 Actor 단위인 것과 별개로, 본 엔진은 Pawn 안에 여러
	//  카메라 컴포넌트를 두는 구조라 컴포넌트 단위 blend 가 필요.)
	// BlendTime <= 0 이면 즉시 swap, > 0 이면 ActiveCamera 는 그대로 두고 PendingActiveCamera
	// 로 보관해 GetCameraView/UpdateCamera 가 보간.
	UFUNCTION(Callable, Category="Camera")
	void SetActiveCameraWithBlend(
		UCameraComponent* NewCamera,
		float BlendTime,
		EViewTargetBlendFunction BlendFunction = EViewTargetBlendFunction::VTBlend_Linear);

	UFUNCTION(Pure, Category="Camera")
	UCameraComponent* GetPossessedCamera() const;
	UFUNCTION(Callable, Category="Camera")
	void Possess(UCameraComponent* NewCamera);

	// ─── View Target ──────────────────────────────────────────────
	// PlayerController::SetViewTargetWithBlend 가 위임. 현재 view target → New 로 전환.
	// UE: APlayerCameraManager::SetViewTarget
	UFUNCTION(Callable, Category="Camera")
	virtual void SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams());

	UFUNCTION(Pure, Category="Camera")
	AActor* GetViewTarget() const;
	UFUNCTION(Pure, Category="Camera")
	AActor* GetPendingViewTarget() const;

	// ─── Camera Shake ─────────────────────────────────────────────
	// UE: APlayerCameraManager::StartCameraShake
	UFUNCTION(Callable, Category="Camera|Shake")
	virtual UCameraShakeBase* StartCameraShake(
		UClass* ShakeClass,
		float Scale = 1.0f,
		ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal,
		FRotator UserPlaySpaceRot = FRotator());

	UFUNCTION(Callable, Category="Camera|Shake")
	virtual UCameraShakeBase* StartCameraShakeAsset(
		UCameraShakeAsset* ShakeAsset,
		float Scale = 1.0f,
		ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal,
		FRotator UserPlaySpaceRot = FRotator());

	UFUNCTION(Callable, Category="Camera|Shake")
	virtual UCameraShakeBase* StartCameraShakeAsset(
		const FString& ShakeAssetPath,
		float Scale = 1.0f,
		ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal,
		FRotator UserPlaySpaceRot = FRotator());

	// 템플릿 헬퍼 — TSubclassOf 대용. 호출부: CameraMgr->StartCameraShake<UMyShake>(1.0f);
	template<typename T>
	T* StartCameraShake(
		float Scale = 1.0f,
		ECameraShakePlaySpace PlaySpace = ECameraShakePlaySpace::CameraLocal,
		FRotator UserPlaySpaceRot = FRotator())
	{
		return static_cast<T*>(StartCameraShake(T::StaticClass(), Scale, PlaySpace, UserPlaySpaceRot));
	}

	// UE: StopCameraShake
	UFUNCTION(Callable, Category="Camera|Shake")
	virtual void StopCameraShake(UCameraShakeBase* ShakeInstance, bool bImmediately = true);
	UFUNCTION(Callable, Category="Camera|Shake")
	virtual void StopAllCameraShakes(bool bImmediately = true);
	UFUNCTION(Callable, Category="Camera|Shake")
	virtual void StopAllInstancesOfCameraShake(UClass* ShakeClass, bool bImmediately = true);

	UFUNCTION(Pure, Category="Camera|Shake")
	FCameraShakeUpdateResult GetLastCameraShakeOffset() const;

	// ─── Camera Modifier ─────────────────────────────────────────
	// UE: APlayerCameraManager::AddNewCameraModifier — 우선순위 정렬 삽입.
	// shake/aim assist/hit reaction 등 카메라 효과 단위 추상화. 추가 후엔 UpdateCamera
	// 마다 ApplyCameraModifiers 가 priority 순서로 ModifyCamera 호출.
	UFUNCTION(Callable, Category="Camera|Modifier")
	UCameraModifier* AddNewCameraModifier(UClass* ModifierClass);

	template<typename T>
	T* AddNewCameraModifier()
	{
		return static_cast<T*>(AddNewCameraModifier(T::StaticClass()));
	}

	UFUNCTION(Callable, Category="Camera|Modifier")
	void RemoveCameraModifier(UCameraModifier* Modifier);
	UFUNCTION(Pure, Category="Camera|Modifier")
	UCameraModifier* FindCameraModifier(UClass* ModifierClass) const;

	// ─── Camera Fade ──────────────────────────────────────────────
	// UE: APlayerCameraManager::StartCameraFade
	UFUNCTION(Callable, Category="Camera|Fade")
	virtual void StartCameraFade(
		float FromAlpha,
		float ToAlpha,
		float Duration,
		FLinearColor Color,
		bool bShouldFadeAudio = false,
		bool bHoldWhenFinished = false);

	UFUNCTION(Callable, Category="Camera|Fade")
	virtual void StopCameraFade();

	// UE: SetManualCameraFade — 외부에서 매 프레임 수동으로 알파를 갱신할 때.
	UFUNCTION(Callable, Category="Camera|Fade")
	virtual void SetManualCameraFade(float InFadeAmount, FLinearColor Color, bool bInFadeAudio);

	// 현재 fade 알파 (0..1) — RenderPass(B) 가 PostProcess 에 전달.
	UFUNCTION(Pure, Category="Camera|Fade")
	float GetFadeAmount() const { return FadeAmount; }
	UFUNCTION(Pure, Category="Camera|Fade")
	FLinearColor GetFadeColor() const { return FadeColor; }
	UFUNCTION(Pure, Category="Camera|Fade")
	bool IsFadeEnabled() const { return bEnableFading; }

	// ─── Camera Vignette ──────────────────────────────────────────────
	UFUNCTION(Callable, Category="Camera|Vignette")
	virtual void SetCameraVignette(float Intensity, float Radius, float Softness, FLinearColor Color);
	UFUNCTION(Callable, Category="Camera|Vignette")
	virtual void ClearCameraVignette();

	// 현재 vignette 상태 — RenderPass(B) 가 PostProcess 에 전달.
	UFUNCTION(Pure, Category="Camera|Vignette")
	bool IsVignetteEnabled() const { return bEnableVignette; }
	UFUNCTION(Pure, Category="Camera|Vignette")
	float GetVignetteIntensity() const { return VignetteIntensity; }
	UFUNCTION(Pure, Category="Camera|Vignette")
	float GetVignetteRadius() const { return VignetteRadius; }
	UFUNCTION(Pure, Category="Camera|Vignette")
	float GetVignetteSoftness() const { return VignetteSoftness; }
	UFUNCTION(Pure, Category="Camera|Vignette")
	FLinearColor GetVignetteColor() const { return VignetteColor; }

	// ─── Camera Depth of Field / Bokeh ──────────────────────────────
	UFUNCTION(Callable, Category="Camera|PostProcess")
	virtual void SetDepthOfField(float FocusDistance, float FocusRange, float MaxBlurRadius);
	UFUNCTION(Callable, Category="Camera|PostProcess")
	virtual void SetBokeh(float RadiusThreshold, float LumaThreshold, float Intensity);
	UFUNCTION(Callable, Category="Camera|PostProcess")
	virtual void ClearDepthOfField();

	UFUNCTION(Pure, Category="Camera|PostProcess")
	bool IsDepthOfFieldEnabled() const { return bEnableDepthOfField; }
	UFUNCTION(Pure, Category="Camera|PostProcess")
	float GetDoFFocusDistance() const { return DoFFocusDistance; }
	UFUNCTION(Pure, Category="Camera|PostProcess")
	float GetDoFFocusRange() const { return DoFFocusRange; }
	UFUNCTION(Pure, Category="Camera|PostProcess")
	float GetDoFMaxBlurRadius() const { return DoFMaxBlurRadius; }
	UFUNCTION(Pure, Category="Camera|PostProcess")
	float GetDoFBokehRadiusThreshold() const { return DoFBokehRadiusThreshold; }
	UFUNCTION(Pure, Category="Camera|PostProcess")
	float GetDoFBokehLumaThreshold() const { return DoFBokehLumaThreshold; }
	UFUNCTION(Pure, Category="Camera|PostProcess")
	float GetDoFBokehIntensity() const { return DoFBokehIntensity; }

	// ─── Camera Blend ──────────────────────────────────────────────
	bool GetCameraView(FMinimalViewInfo& OutPOV) const;

	// ─── Tick ─────────────────────────────────────────────────────
	// World::Tick 에서 매 프레임 호출. ActiveCamera base POV 산출 → Shake 누적 →
	// CameraCachePOV 에 저장. Fade / ViewTarget blend 도 같이 갱신.
	virtual void UpdateCamera(float DeltaTime);
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	// ─── POV Cache ────────────────────────────────────────────────
	// UE: APlayerCameraManager::GetCameraCachePOV. UpdateCamera 가 매 프레임 채운
	// 최종(shake 적용된) POV 를 반환. World::GetActivePOV / 외부 호출자가 이걸 사용.
	// false 반환 시 ActiveCamera 가 없거나 아직 한 번도 갱신되지 않은 상태.
	bool GetCameraCachePOV(FMinimalViewInfo& OutPOV) const;

private:
	float ApplyBlendFunction(float Alpha, FViewTargetTransitionParams BlendParams) const;
	FMinimalViewInfo LerpPOV(const FMinimalViewInfo& From, const FMinimalViewInfo& To, float Alpha) const;

	// 기본 modifier (CameraShake) 가 없으면 만들어 ModifierList 에 등록. lazy init.
	void EnsureDefaultModifiers();

	// ModifierList 를 priority 순으로 순회하며 ModifyCamera 호출 — UpdateCamera 가 base+blend
	// POV 산출 후 1회 호출.
	void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& InOutPOV);

private:
	TSet<UCameraComponent*> RegisteredCameras;
	TArray<UCameraComponent*> RegisteredCameraOrder;

	UCameraComponent* ActiveCamera = nullptr;		// Rendering Camera
	UCameraComponent* PossessedCamera = nullptr;	// Input/Control Camera

	// View Target
	AActor* ViewTarget = nullptr;
	AActor* PendingViewTarget = nullptr;
	FViewTargetTransitionParams BlendParams;
	float BlendTimeRemaining = 0.0f;

	// ActiveCamera 컴포넌트 단위 blend (ViewTarget blend 와 별개 — 같은 액터의
	// 다른 카메라로 보간 전환할 때 사용). PendingActiveCamera != nullptr 이면 진행 중.
	UCameraComponent* PendingActiveCamera = nullptr;
	float ActiveCameraBlendTimeRemaining = 0.0f;
	float ActiveCameraBlendDuration = 0.0f;
	EViewTargetBlendFunction ActiveCameraBlendFunction = EViewTargetBlendFunction::VTBlend_Linear;

	// Camera modifier list — priority 오름차순 정렬. 기본 ShakeModifier 1개를 lazy 추가.
	TArray<UCameraModifier*> ModifierList;
	UCameraModifier_CameraShake* ShakeModifier = nullptr;  // 빠른 접근용 캐시

	// Fade 상태
	bool bEnableFading = false;
	bool bHoldFadeWhenFinished = false;
	bool bFadeAudio = false;
	float FadeAmount = 0.0f;
	float FadeAlphaFrom = 0.0f;
	float FadeAlphaTo = 0.0f;
	float FadeDuration = 0.0f;
	float FadeTimeRemaining = 0.0f;
	FLinearColor FadeColor = FLinearColor::Black();

	// Vignette 상태
	bool bEnableVignette = false;
	float VignetteIntensity = 0.0f;
	float VignetteRadius = 0.75f;
	float VignetteSoftness = 0.35f;
	FLinearColor VignetteColor = FLinearColor::Black();

	// Depth of Field / Bokeh state. Render pipelines copy this into FViewportRenderOptions.
	bool bEnableDepthOfField = false;
	float DoFFocusDistance = 500.0f;
	float DoFFocusRange = 200.0f;
	float DoFMaxBlurRadius = 4.0f;
	float DoFBokehRadiusThreshold = 2.5f;
	float DoFBokehLumaThreshold = 0.45f;
	float DoFBokehIntensity = 0.65f;

	// POV cache — UpdateCamera 가 채우고, 외부는 GetCameraCachePOV 로 read.
	// ActiveCamera 가 한 번도 없었으면 bCameraCacheValid=false → caller 가 fallback 처리.
	FMinimalViewInfo CameraCachePOV;
	bool bCameraCacheValid = false;
};
