#pragma once
#include "Component/Light/LightComponentBase.h"


#include "Source/Engine/Component/Light/LightComponent.generated.h"

UCLASS()
class ULightComponent : public ULightComponentBase
{
public:
	GENERATED_BODY()
	UFUNCTION(Pure, Category="Light|Shadow")
	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	UFUNCTION(Pure, Category="Light|Shadow")
	float GetShadowBias() const { return ShadowBias; }
	UFUNCTION(Pure, Category="Light|Shadow")
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	UFUNCTION(Pure, Category="Light|Shadow")
	float GetShadowNormalBias() const { return ShadowNormalBias; } 
	UFUNCTION(Pure, Category="Light|Shadow")
	float GetShadowSharpen() const { return ShadowSharpen; }

	UFUNCTION(Callable, Category="Light|Shadow")
	void SetShadowBias(float V) { ShadowBias = V; }
	UFUNCTION(Callable, Category="Light|Shadow")
	void SetShadowSlopeBias(float V) { ShadowSlopeBias = V; }
	UFUNCTION(Callable, Category="Light|Shadow")
	void SetShadowNormalBias(float V) { ShadowNormalBias = V; }
	UFUNCTION(Callable, Category="Light|Shadow")
	void SetShadowSharpen(float V) { ShadowSharpen = V; }

protected:
	UPROPERTY(Edit, Save, Category="Shadow", DisplayName="Shadow Resolution Scale", Min=0.1f, Max=4.0f, Speed=0.1f)
	float ShadowResolutionScale = 1.0f;
	UPROPERTY(Edit, Save, Category="Shadow", DisplayName="Shadow Bias", Min=-0.2f, Max=0.2f, Speed=0.0001f)
	float ShadowBias = -0.0001f;
	UPROPERTY(Edit, Save, Category="Shadow", DisplayName="Shadow Slope Bias", Min=-0.2f, Max=0.2f, Speed=0.0001f)
	float ShadowSlopeBias = 0.0001f;
	UPROPERTY(Edit, Save, Category="Shadow", DisplayName="Shadow Normal Bias", Min=-0.2f, Max=0.2f, Speed=0.0001f)
	float ShadowNormalBias = -0.0020f;
	UPROPERTY(Edit, Save, Category="Shadow", DisplayName="Shadow Sharpen", Min=0.0f, Max=1.0f, Speed=0.05f)
	float ShadowSharpen = 0.67f;
};
