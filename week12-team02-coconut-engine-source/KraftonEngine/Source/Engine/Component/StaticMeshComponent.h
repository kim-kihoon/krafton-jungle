#pragma once

#include "Component/MeshComponent.h"
#include "Core/Property/PropertyTypes.h"
#include "Core/UObject/TSoftObjectPtr.h"
#include "Mesh/MeshManager.h"
#include "Mesh/StaticMesh.h"
#include "StaticMeshComponent.generated.h"

class UMaterial;
class FPrimitiveSceneProxy;

namespace json { class JSON; }

// UStaticMeshComponent — 월드 배치 컴포넌트
UCLASS()
class UStaticMeshComponent : public UMeshComponent
{
public:
	GENERATED_BODY(UStaticMeshComponent)
	UStaticMeshComponent() = default;
	~UStaticMeshComponent() override = default;

	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;
	bool LineTraceStaticMeshFast(const FRay& Ray, const FMatrix& WorldMatrix, const FMatrix& WorldInverse, FHitResult& OutHitResult);
	void UpdateWorldAABB() const override;

	// 구체 프록시 생성 (FStaticMeshSceneProxy)
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void SetStaticMesh(UStaticMesh* InMesh);
	UStaticMesh* GetStaticMesh() const;

	void SetMaterial(int32 ElementIndex, UMaterial* InMaterial);
	UMaterial* GetMaterial(int32 ElementIndex) const;
	const TArray<UMaterial*>& GetOverrideMaterials() const { return OverrideMaterials; }
	void PostDuplicate() override;

	// Property Editor 지원
	void PostEditProperty(const char* PropertyName) override;

	const FString& GetStaticMeshPath() const { return StaticMesh.GetPath().ToString(); }

private:
	void CacheLocalBounds();

	UPROPERTY(Edit, Category = "Mesh", DisplayName = "Static Mesh", Type = SoftObject, Class = UStaticMesh)
	TSoftObjectPtr<UStaticMesh> StaticMesh;
	TArray<UMaterial*> OverrideMaterials;

	UPROPERTY(Edit, FixedSize, Category = "Materials", DisplayName = "Materials")
	TArray<FMaterialSlot> MaterialSlots; // 경로 + UVScroll 묶음

	FVector CachedLocalCenter = { 0, 0, 0 };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool bHasValidBounds = false;
};
