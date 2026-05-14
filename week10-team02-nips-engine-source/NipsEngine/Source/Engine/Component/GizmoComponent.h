#pragma once

#include "PrimitiveComponent.h"
#include "Core/CoreMinimal.h"
#include "Render/Resource/Material.h"
#include "GizmoTransformTarget.h"
#include <memory>

class AActor;
class USceneComponent;
class USkeletalMeshComponent;
#include "Render/Resource/VertexTypes.h"

class UGizmoComponent : public UPrimitiveComponent
{
private:
	enum EGizmoMode
	{
		Translate,
		Rotate,
		Scale,
		End
	};

	std::unique_ptr<IGizmoTransformTarget> TransformTarget;

	AActor* TrackingActor = nullptr;
	USceneComponent* TrackingComponent = nullptr;
	const TArray<AActor*>* TrackingActors = nullptr;
	int32 TrackingBoneIndex = -1;
	EGizmoMode CurMode = EGizmoMode::Translate;
	FVector LastIntersectionLocation;
	FVector DragPlaneOrigin;
	FVector DragStartIntersectionLocation;
	FVector DragAxisVector;
	FVector DragProjectDir;
	FTransform DragStartWorldTransform = FTransform::Identity;
	const float AxisLength = 1.0f;
	float Radius = 0.1f;
	const float ScaleSensitivity = 1.0f;
	int32 SelectedAxis = -1;
	float SnapAccumulatedDrag = 0.0f;
	bool bIsFirstFrameOfDrag = true;
	bool bIsHolding = false;
	bool bIsWorldSpace = true;
	bool bPressedOnHandle = false;
	bool bTranslateSnapEnabled = false;
	bool bRotateSnapEnabled = false;
	bool bScaleSnapEnabled = false;
	float TranslateSnapValue = 1.0f;
	float RotateSnapValue = 0.261799f;
	float ScaleSnapValue = 0.1f;

	bool IntersectRayAxis(const FRay& Ray, FVector AxisEnd, float& OutRayT);
	const FMeshData* GetActiveMeshData() const;
	float ApplySnapToDrag(float DragAmount);

	//Control Target Method
	void HandleDrag(float DragAmount);
	void TranslateTarget(float DragAmount);
	void RotateTarget(float DragAmount);
	void ScaleTarget(float DragAmount);

	USceneComponent* GetEffectiveTargetComponent() const;

	FVector GetTargetLocation() const;
	FVector GetTargetRotation() const;

	bool BeginLinearDrag(const FRay& Ray);
	bool GetLinearDragIntersection(const FRay& Ray, FVector& OutIntersection) const;
	float ApplySnapToTotalDrag(float DragAmount) const;
	void ApplyLinearDragFromStart(float DragAmount);
	void UpdateLinearDrag(const FRay& Ray);
	void UpdateAngularDrag(const FRay& Ray);

public:
	DECLARE_CLASS(UGizmoComponent, UPrimitiveComponent)
	UGizmoComponent();

	// 기즈모 컴포넌트는 복제를 지원하지 않습니다.
	virtual UGizmoComponent* Duplicate() override { return nullptr; }

	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;

	FVector GetVectorForAxis(int32 Axis);
	void RenderGizmo() {}

	void SetTransformTarget(std::unique_ptr<IGizmoTransformTarget> InTarget);
	void ClearTransformTarget();

	void SetTarget(AActor* NewTarget);
	void SetTargetComponent(USceneComponent* NewTargetComponent);
	void SetSelectedActors(const TArray<AActor*>* InSelectedActors);
	void SetTargetBone(USkeletalMeshComponent* MeshComponent, int32 BoneIndex);

	inline bool HasTarget() const { return TransformTarget && TransformTarget->IsValid(); }

	inline AActor* GetTarget() const { return TrackingActor; }
	inline USceneComponent* GetTargetComponent() const { return TrackingComponent; }

	inline void SetHolding(bool bHold) { bIsHolding = bHold; }
	inline bool IsHolding() const { return bIsHolding; }
	inline bool IsHovered() const { return SelectedAxis != -1; }
	inline int32 GetSelectedAxis() const { return SelectedAxis; }

	inline void SetPressedOnHandle(bool bPressed) { bPressedOnHandle = bPressed; }
	inline bool IsPressedOnHandle() const { return bPressedOnHandle; }

	void UpdateHoveredAxis(int Index);
	void UpdateDrag(const FRay& Ray);
	void DragEnd();

	void SetTargetLocation(FVector NewLocation);
	void SetTargetRotation(FVector NewRotation);
	void SetTargetScale(FVector NewScale);

	void SetNextMode();
	void UpdateGizmoMode(EGizmoMode NewMode);
	inline void SetTranslateMode() { UpdateGizmoMode(EGizmoMode::Translate); }
	inline void SetRotateMode() { UpdateGizmoMode(EGizmoMode::Rotate); }
	inline void SetScaleMode() { UpdateGizmoMode(EGizmoMode::Scale); }
	inline bool IsTranslateMode() const { return CurMode == EGizmoMode::Translate; }
	inline bool IsRotateMode() const { return CurMode == EGizmoMode::Rotate; }
	inline bool IsScaleMode() const { return CurMode == EGizmoMode::Scale; }
	inline bool IsTranslateSnapEnabled() const { return bTranslateSnapEnabled; }
	inline bool IsRotateSnapEnabled() const { return bRotateSnapEnabled; }
	inline bool IsScaleSnapEnabled() const { return bScaleSnapEnabled; }
	inline float GetTranslateSnapValue() const { return TranslateSnapValue; }
	inline float GetRotateSnapValue() const { return RotateSnapValue; }
	inline float GetScaleSnapValue() const { return ScaleSnapValue; }
	inline void SetTranslateSnap(bool bEnabled, float Value) { bTranslateSnapEnabled = bEnabled; TranslateSnapValue = Value; SnapAccumulatedDrag = 0.0f; }
	inline void SetRotateSnap(bool bEnabled, float Value) { bRotateSnapEnabled = bEnabled; RotateSnapValue = Value; SnapAccumulatedDrag = 0.0f; }
	inline void SetScaleSnap(bool bEnabled, float Value) { bScaleSnapEnabled = bEnabled; ScaleSnapValue = Value; SnapAccumulatedDrag = 0.0f; }
	void UpdateGizmoTransform();
	void ApplyScreenSpaceScaling(const FVector& CameraLocation);

	// 직교 뷰 전용: OrthoHeight 기반으로 기즈모 스케일 설정
	void ApplyScreenSpaceScalingOrtho(float OrthoHeight);
	void SetWorldSpace(bool bWorldSpace);
	bool IsWorldSpace() const { return bIsWorldSpace; }
	void Deactivate() override;

	EPrimitiveType GetPrimitiveType() const override;

	UMaterialInterface* GetMaterial() { return Material; }

private:
	const FMeshData* GizmoMeshData = nullptr;
	UMaterialInterface* Material = nullptr;
	
	FVector LocalExtents = FVector(1.5f, 1.5f, 1.5f);
};
