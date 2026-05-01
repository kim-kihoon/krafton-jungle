#pragma once
#include "Component/Light/LightComponent.h"

class UPointLightComponent : public ULightComponent
{
public:
	DECLARE_CLASS(UPointLightComponent, ULightComponent)
	UPointLightComponent();
	virtual ~UPointLightComponent() override;
	virtual void ContributeSelectedVisuals(FScene& Scene) const override;
	virtual void PushToScene() override;
	virtual void DestroyFromScene() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	FShadowMapKey GetShadowMapKey() override;
	FShadowCubeHandle PeekCubeShadowHandle() const { return CubeShadowHandle; }
	FShadowCubeHandle AcquireCubeShadowHandleForRenderer();
	void ReleaseCubeShadowHandleForRenderer();

protected:
	float AttenuationRadius = 1.f;
	float LightFalloffExponent = 1.f;

private:
	void ReleaseCubeShadowHandle();

	FShadowCubeHandle CubeShadowHandle;
};
