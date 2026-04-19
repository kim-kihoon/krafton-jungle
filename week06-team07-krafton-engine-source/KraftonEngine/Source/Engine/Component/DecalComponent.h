#pragma once

#include "Component/PrimitiveComponent.h"
#include "Render/Proxy/DecalSceneProxy.h"
#include "Core/ResourceTypes.h"
#include "Object/FName.h"

class FPrimitiveSceneProxy;
class UArrowComponent;

class UDecalComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UDecalComponent, UPrimitiveComponent)

	UDecalComponent() = default;
	~UDecalComponent() override = default;

	void InitializeComponent() override;

	FMeshBuffer* GetMeshBuffer() const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;

	void SetTexture(const FName& InTextureName);
	void SetFade(bool bEnable, float Inner = 0.2f, float Outer = 0.6f);
	void SetFadeConstants(FDecalConstants& OutDecalConstants) const;
	ID3D11ShaderResourceView* GetSRV() const;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	uint64 CalculateOBBScreenPixels(const FMatrix& ViewProj, float ViewportWidth,float ViewportHeight);

private:
	FName TextureName = "None";
	FTextureResource* CachedTexture = nullptr;

	bool bHasFade = false;
	float FadeInner = 0.2f;
	float FadeOuter = 0.6f;

	UArrowComponent* ArrowComponent = nullptr;
};

