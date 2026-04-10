#pragma once
#include "CoreMinimal.h"
#include "Component/PrimitiveComponent.h"

struct FDynamicMesh;
class FDynamicMaterial;
struct FMaterialTexture;

class ENGINE_API UBillboardComponent : public UPrimitiveComponent
{
public:
	DECLARE_RTTI(UBillboardComponent, UPrimitiveComponent)
	GENERATE_SHALLOW_CLONE(UBillboardComponent)
	void PostConstruct() override;

	virtual FBoxSphereBounds GetWorldBounds() const override;

	void SetSize(const FVector2& InSize) { Size = InSize; }
	const FVector2& GetSize() const { return Size; }

	void SetBillboard(bool bInBillboard) { bBillboard = bInBillboard; }
	bool IsBillboard() const { return bBillboard; }

	void SetEditorOnly(bool bInEditorOnly) { bEditorOnly = bInEditorOnly; }
	bool IsEditorOnly() const { return bEditorOnly; }

	virtual FRenderMesh* GetRenderMesh() const override;
	FDynamicMesh* GetBillboardMesh() const { return BillboardMesh.get(); }
	FDynamicMaterial* GetMaterialInstance() const { return MaterialInstance.get(); }

	virtual void FixupReferences(const FDuplicateionContext& Context) override;

	void SetSpriteTexture(std::shared_ptr<FMaterialTexture> InTexture);
	void ResetMaterial(const FString& MaterialName);

private:
	FVector2 Size = FVector2(1.f, 1.f);
	FVector4 Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	bool bBillboard = false;
	bool bEditorOnly = false;
	std::shared_ptr<FDynamicMesh> BillboardMesh;
	std::shared_ptr<FDynamicMaterial> MaterialInstance;
};
