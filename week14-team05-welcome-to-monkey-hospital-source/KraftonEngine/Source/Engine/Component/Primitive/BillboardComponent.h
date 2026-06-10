#pragma once
#include "Component/PrimitiveComponent.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Core/Types/ResourceTypes.h"
#include "Object/FName.h"
#include "Object/Ptr/SoftObjectPtr.h"

#include "Source/Engine/Component/Primitive/BillboardComponent.generated.h"

class FPrimitiveSceneProxy;

UCLASS()
class UBillboardComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void PostDuplicate() override;

	void PostEditProperty(const char* PropertyName) override;

	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;

	UFUNCTION(Callable, Category="Billboard")
	void SetBillboardEnabled(bool bEnable) { bIsBillboard = bEnable; }
	UFUNCTION(Pure, Category="Billboard")
	bool IsBillboardEnabled() const { return bIsBillboard; }

	// --- Material ---
	UFUNCTION(Callable, Category="Materials")
	void SetMaterial(class UMaterial* InMaterial);
	UFUNCTION(Pure, Category="Materials")
	class UMaterial* GetMaterial() const { return Material; }

	// 주어진 카메라 방향으로 빌보드 월드 행렬을 계산 (per-view 렌더링용)
	UFUNCTION(Pure, Category="Billboard")
	FMatrix ComputeBillboardMatrix(const FVector& CameraForward) const;

	FMeshBuffer* GetMeshBuffer() const override { return &FMeshBufferManager::Get().GetMeshBuffer(EMeshShape::Quad); }
	FMeshDataView GetMeshDataView() const override { return FMeshDataView::FromMeshData(FMeshBufferManager::Get().GetMeshData(EMeshShape::Quad)); }

protected:
	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="Billboard")
	bool bIsBillboard = true;

	UPROPERTY(Edit, Save, Category="Rendering", DisplayName="Material", AssetType="Material")
	FSoftObjectPtr MaterialSlot;
	UMaterial* Material = nullptr;
};

