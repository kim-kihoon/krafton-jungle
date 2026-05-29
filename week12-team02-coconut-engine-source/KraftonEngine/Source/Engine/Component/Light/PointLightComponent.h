#pragma once
#include "Component/Light/LightComponent.h"
#include "PointLightComponent.generated.h"

UCLASS()
class UPointLightComponent : public ULightComponent
{
public:
	GENERATED_BODY(UPointLightComponent)
	virtual ELightComponentType GetLightType() const override { return ELightComponentType::Point; }
	virtual void ContributeSelectedVisuals(FScene& Scene) const override;
	virtual void PushToScene() override;
	virtual void DestroyFromScene() override;
	virtual bool GetLightViewProj(FLightViewProjResult& OutResult, const FMinimalViewInfo* POV, int32 FaceIndex) const override;

	float GetAttenuationRadius() const { return AttenuationRadius; }
	void  SetAttenuationRadius(float V) { AttenuationRadius = V; PushToScene(); }

protected:
	UPROPERTY(Edit, Category="Lighting", Min=0.05f, Max=1000.f, Speed=0.01f)
	float AttenuationRadius = 1.f;
	UPROPERTY(Edit, Category="Lighting", Min=0.05f, Max=10.f, Speed=0.01f)
	float LightFalloffExponent = 1.f;
};
