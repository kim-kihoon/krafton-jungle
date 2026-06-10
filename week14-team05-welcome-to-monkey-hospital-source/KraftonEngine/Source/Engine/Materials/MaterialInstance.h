#pragma once

#include "Materials/Material.h"
#include "Source/Engine/Materials/MaterialInstance.generated.h"

// UMaterialInstance — Parent 자산을 기반으로 일부 파라미터/상태를 덮어쓰는 머티리얼.
//
// 초기화 모델: InitializeFromParent에서 Parent의 Template/RenderState/CBMap/Textures를
// 한 번에 복제한다. 이후 SetXxxParameter는 자체 CBMap을 갱신하므로 Parent와 독립.
//
// 의미적으로는 Cascade의 MaterialInstanceConstant — 같은 base shader/layout을 공유하면서
// emitter별로 SubImagesH/V, TintColor 같은 파라미터만 다르게 가져가는 용도.
UCLASS()
class UMaterialInstance : public UMaterial
{
public:
	GENERATED_BODY()
	~UMaterialInstance() override = default;

	// Parent로부터 Template/렌더상태/CB/Texture를 통째로 복제. InPathFileName은 자산 식별자.
	void InitializeFromParent(UMaterial* InParent, const FString& InPathFileName);

	// 렌더 상태 명시적 오버라이드 — Parent 도출값 위에 저수준 override 슬롯으로 덮어씌움.
	void OverrideRenderPass(ERenderPass InPass)                { SetPassOverride(InPass); }
	void OverrideBlendState(EBlendState InBlend)               { SetBlendOverride(InBlend); }
	void OverrideDepthStencilState(EDepthStencilState InDepth) { SetDepthOverride(InDepth); }
	void OverrideRasterizerState(ERasterizerState InRaster)    { SetRasterOverride(InRaster); }
	// 고수준 의도 오버라이드 (권장)
	void OverrideDomainBlend(EMaterialDomain InDomain, EBlendMode InBlend) { SetDomainBlend(InDomain, InBlend); }

	UMaterial* GetParent() const { return Parent; }

	void AddReferencedObjects(FReferenceCollector& Collector) override;

	bool IsMaterialInstance() const override { return true; }

protected:
	// 직접 UObject 포인터 — 자산 reflection 대상 아님 (load 단계에서 MaterialManager가 imperative하게 주입).
	UMaterial* Parent = nullptr;
};
