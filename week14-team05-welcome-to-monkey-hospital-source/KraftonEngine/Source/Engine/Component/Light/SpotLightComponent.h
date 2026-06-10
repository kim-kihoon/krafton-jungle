#pragma once
#include "Component/Light/PointLightComponent.h"


#include "Source/Engine/Component/Light/SpotLightComponent.generated.h"

UCLASS()
class USpotLightComponent : public UPointLightComponent
{
public:
	GENERATED_BODY()
	virtual ELightComponentType GetLightType() const override { return ELightComponentType::Spot; }
	virtual void ContributeSelectedVisuals(FScene& Scene) const override;
	virtual void PushToScene() override;
	virtual void DestroyFromScene() override;
	virtual bool GetLightViewProj(FLightViewProjResult& OutResult, const FMinimalViewInfo* POV = nullptr, int32 FaceIndex = 0) const override;

	UFUNCTION(Pure, Category="Lighting")
	float GetInnerConeAngle() const { return InnerConeAngle; }
	UFUNCTION(Callable, Category="Lighting")
	void SetInnerConeAngle(float V) { InnerConeAngle = V; PushToScene(); }
	UFUNCTION(Pure, Category="Lighting")
	float GetOuterConeAngle() const { return OuterConeAngle; }
	UFUNCTION(Callable, Category="Lighting")
	void SetOuterConeAngle(float V) { OuterConeAngle = V; PushToScene(); }

protected:
	UPROPERTY(Edit, Save, Category="Lighting", DisplayName="InnerConeAngle", Min=0.0f, Max=89.0f, Speed=0.1f)
	float InnerConeAngle = 20.0f;	// Inner Cone Angle in degrees
	UPROPERTY(Edit, Save, Category="Lighting", DisplayName="OuterConeAngle", Min=0.0f, Max=89.0f, Speed=0.1f)
	float OuterConeAngle = 40.0f;	// Outer Cone Angle in degrees
};
