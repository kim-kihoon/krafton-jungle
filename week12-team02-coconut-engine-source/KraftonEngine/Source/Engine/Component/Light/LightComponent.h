#pragma once
#include "Component/Light/LightComponentBase.h"
#include "LightComponent.generated.h"

UCLASS(HiddenInComponentList)
class ULightComponent : public ULightComponentBase
{
public:
	GENERATED_BODY(ULightComponent)

	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowNormalBias() const { return ShadowNormalBias; } 
	float GetShadowSharpen() const { return ShadowSharpen; }

	void SetShadowBias(float V) { ShadowBias = V; }
	void SetShadowSlopeBias(float V) { ShadowSlopeBias = V; }
	void SetShadowNormalBias(float V) { ShadowNormalBias = V; }
	void SetShadowSharpen(float V) { ShadowSharpen = V; }

protected:
	UPROPERTY(Edit, Category="Shadow", DisplayName="Shadow Resolution Scale", Min=0.1f, Max=4.0f, Speed=0.1f)
	float ShadowResolutionScale = 1.0f;
	UPROPERTY(Edit, Category="Shadow", DisplayName="Shadow Bias", Min=-0.2f, Max=0.2f, Speed=0.0001f)
	float ShadowBias = -0.0001f;
	UPROPERTY(Edit, Category="Shadow", DisplayName="Shadow Slope Bias", Min=-0.2f, Max=0.2f, Speed=0.0001f)
	float ShadowSlopeBias = 0.0001f;
	UPROPERTY(Edit, Category="Shadow", DisplayName="Shadow Normal Bias", Min=-0.2f, Max=0.2f, Speed=0.0001f)
	float ShadowNormalBias = -0.0020f;
	UPROPERTY(Edit, Category="Shadow", DisplayName="Shadow Sharpen", Min=0.f, Max=1.f, Speed=0.06f)
	float ShadowSharpen = 0.67f;
};
