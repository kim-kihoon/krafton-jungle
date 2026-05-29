#pragma once
#include "Object/ObjectFactory.h"
#include "Component/SceneComponent.h"
#include "Math/MathUtils.h"
#include "Math/Vector.h"

#include "CameraComponent.generated.h"

struct FMinimalViewInfo;

USTRUCT()
struct FCameraState
{
	GENERATED_BODY(FCameraState)
	UPROPERTY(Edit, Category="Camera", DisplayName="FOV", Min = 0.1, Max = 3.14, Speed = 0.01)
	float FOV = 3.14159265358979f / 3.0f;

	float AspectRatio = 16.0f / 9.0f;

	UPROPERTY(Edit, Category = "Camera", DisplayName = "Near Z", Min = 0.01, Max = 100.0, Speed = 0.01)
	float NearZ = 0.1f;
	
	UPROPERTY(Edit, Category = "Camera", DisplayName = "Far Z", Min = 1.0, Max = 100000.0, Speed = 10.0)
	float FarZ = 1000.0f;

	UPROPERTY(Edit, Category = "Camera", DisplayName = "Ortho Width", Min = 0.1, Max = 1000.0, Speed = 0.5)
	float OrthoWidth = 10.0f;

	UPROPERTY(Edit, Category = "Camera", DisplayName = "Is Orthogonal")
	bool bIsOrthogonal = false;
};

UCLASS()
class UCameraComponent : public USceneComponent
{
public:
	GENERATED_BODY(UCameraComponent)
	UCameraComponent() = default;

	void BeginPlay() override;
	void EndPlay() override;

	void LookAt(const FVector& Target);
	void SetCameraState(const FCameraState& NewState);
	const FCameraState& GetCameraState() const { return CameraState; }

	// 카메라 POV 통화 산출 — UE: UCameraComponent::GetCameraView.
	// CameraManager / RenderPipeline 이 이걸 받아 매트릭스/프러스텀을 빌드한다.
	// DeltaTime 은 향후 카메라 lag / interpolation 에 쓰이도록 시그니처 보존.
	void GetCameraView(float DeltaTime, FMinimalViewInfo& OutPOV) const;

	void SetFOV(float InFOV) { CameraState.FOV = InFOV; }
	void SetOrthoWidth(float InWidth) { CameraState.OrthoWidth = InWidth; }
	void SetOrthographic(bool bOrtho) { CameraState.bIsOrthogonal = bOrtho; }

	void OnResize(int32 Width, int32 Height);

	float GetFOV() const { return CameraState.FOV; }
	float GetNearPlane() const { return CameraState.NearZ; }
	float GetFarPlane() const { return CameraState.FarZ; }
	float GetOrthoWidth() const { return CameraState.OrthoWidth; }
	bool IsOrthogonal() const { return CameraState.bIsOrthogonal; }

private:
	UPROPERTY(Edit, Category="Camera", DisplayName="Camera State")
	FCameraState CameraState;
};
