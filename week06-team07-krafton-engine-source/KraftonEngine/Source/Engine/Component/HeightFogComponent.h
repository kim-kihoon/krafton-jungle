#pragma once

#include "SceneComponent.h"
#include "Object/ObjectFactory.h"
#include "Math/Vector.h"

// UE5 ExponentialHeightFogComponent 대응
// 씬에 배치하면 Exponential Height Fog가 적용된다.
class UHeightFogComponent : public USceneComponent
{
public:
	DECLARE_CLASS(UHeightFogComponent, USceneComponent)

	void CreateRenderState() override;
	void DestroyRenderState() override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void Serialize(FArchive& Ar) override;

	float GetFogDensity()        const { return FogDensity; }
	float GetFogHeightFalloff()  const { return FogHeightFalloff; }
	float GetStartDistance()     const { return StartDistance; }
	float GetFogCutoffDistance() const { return FogCutoffDistance; }
	float GetFogMaxOpacity()     const { return FogMaxOpacity; }
	FVector4 GetFogInscatteringColor() const { return FogInscatteringColor; }

	void SetFogDensity(float InValue)        { FogDensity = InValue; }
	void SetFogHeightFalloff(float InValue)  { FogHeightFalloff = InValue; }
	void SetStartDistance(float InValue)     { StartDistance = InValue; }
	void SetFogCutoffDistance(float InValue) { FogCutoffDistance = InValue; }
	void SetFogMaxOpacity(float InValue)     { FogMaxOpacity = InValue; }

protected:
	float FogDensity = 0.02f;
	float FogHeightFalloff = 0.2f;
	float StartDistance = 0.0f;
	float FogCutoffDistance = 0.0f;
	float FogMaxOpacity = 1.0f;

	FVector4 FogInscatteringColor = FVector4(0.45f, 0.55f, 0.7f, 1.0f);
};
