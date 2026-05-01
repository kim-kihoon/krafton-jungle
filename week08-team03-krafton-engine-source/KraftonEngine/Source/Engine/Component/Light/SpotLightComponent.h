#pragma once
#include "Component/Light/PointLightComponent.h"

class USpotLightComponent : public UPointLightComponent
{
public:
	DECLARE_CLASS(USpotLightComponent, UPointLightComponent)
	USpotLightComponent();
	virtual void ContributeSelectedVisuals(FScene& Scene) const override;
	virtual void PushToScene() override;
	virtual void DestroyFromScene() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	virtual FShadowHandleSet* GetShadowHandleSet() override;
	FShadowMapKey GetShadowMapKey() override;
	float GetInnerConeAngle() const { return InnerConeAngle; }
	float GetOuterConeAngle() const { return OuterConeAngle; }

protected:
	float InnerConeAngle = 20.0f;	// Inner Cone Angle in degrees
	float OuterConeAngle = 40.0f;	// Outer Cone Angle in degrees
};
