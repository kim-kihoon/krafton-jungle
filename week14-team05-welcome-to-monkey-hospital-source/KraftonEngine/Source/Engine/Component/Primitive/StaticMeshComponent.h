#pragma once

#include "Component/MeshComponent.h"
#include "Core/Types/PropertyTypes.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Object/Ptr/ObjectPtr.h"
#include "Object/Ptr/SoftObjectPtr.h"

class UMaterial;
class FPrimitiveSceneProxy;

namespace json { class JSON; }

// UStaticMeshComponent — 월드 배치 컴포넌트

#include "Source/Engine/Component/Primitive/StaticMeshComponent.generated.h"

UCLASS()
class UStaticMeshComponent : public UMeshComponent
{
public:
	GENERATED_BODY()
	UStaticMeshComponent() = default;
	~UStaticMeshComponent() override = default;

	FMeshBuffer* GetMeshBuffer() const override;
	FMeshDataView GetMeshDataView() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;
	bool LineTraceStaticMeshFast(const FRay& Ray, const FMatrix& WorldMatrix, const FMatrix& WorldInverse, FHitResult& OutHitResult);
	void UpdateWorldAABB() const override;

	// 구체 프록시 생성 (FStaticMeshSceneProxy)
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	UFUNCTION(Callable, Category="Mesh")
	void SetStaticMesh(UStaticMesh* InMesh);
	UFUNCTION(Callable, Exec, Category="Mesh")
	bool SetStaticMeshByPath(const FString& InPath);
	UFUNCTION(Callable, Exec, Category="Mesh")
	void ClearStaticMesh();
	UFUNCTION(Pure, Category="Mesh")
	UStaticMesh* GetStaticMesh() const;
	UFUNCTION(Pure, Category="Mesh")
	FString GetStaticMeshPathValue() const { return StaticMeshPath.ToString(); }

	UFUNCTION(Callable, Category="Materials")
	void SetMaterial(int32 ElementIndex, UMaterial* InMaterial);
	UFUNCTION(Callable, Exec, Category="Materials")
	bool SetMaterialByPath(int32 ElementIndex, const FString& MaterialPath);
	UFUNCTION(Pure, Category="Materials")
	UMaterial* GetMaterial(int32 ElementIndex) const;
	UFUNCTION(Pure, Category="Materials")
	FString GetMaterialPath(int32 ElementIndex) const;
	UFUNCTION(Pure, Category="Materials")
	int32 GetMaterialSlotCount() const { return static_cast<int32>(MaterialSlots.size()); }
	const TArray<UMaterial*>& GetOverrideMaterials() const { return OverrideMaterials; }

	void AddReferencedObjects(FReferenceCollector& Collector) override;

	void PostDuplicate() override;
	void OnPostLoad(FArchive& Ar) override;

	// Property Editor 지원
	void PostEditProperty(const char* PropertyName) override;

	const FString& GetStaticMeshPath() const { return StaticMeshPath.ToString(); }

private:
	void CacheLocalBounds();
	void ReloadMeshAndSerializedMaterials();

	TObjectPtr<UStaticMesh> StaticMesh;
	UPROPERTY(Edit, Save, Category="Mesh", DisplayName="Static Mesh", AssetType="StaticMesh")
	FSoftObjectPtr StaticMeshPath;
	TArray<UMaterial*> OverrideMaterials;
	UPROPERTY(Edit, Save, EditFixedSize, Category="Materials", DisplayName="Materials", AssetType="Material")
	TArray<FSoftObjectPtr> MaterialSlots;

	FVector CachedLocalCenter = { 0, 0, 0 };
	FVector CachedLocalExtent = { 0.5f, 0.5f, 0.5f };
	bool bHasValidBounds = false;
};
