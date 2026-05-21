#pragma once
#include "LightComponent.h"

#include "UDirectionalLightComponent.generated.h"
UCLASS()
class UDirectionalLightComponent : public ULightComponent
{
public:
    GENERATED_BODY(UDirectionalLightComponent, ULightComponent)
	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

protected:
	FMatrix ComputePerspectiveShadowMatrix(const FMatrix& CamView, const FMatrix& CamProj,
		const TArray<FBoundingBox>* VisibleObjectsBounds) const override;

public:
	float CSMMaxDistance = { 300.f };
	float CSMPractialLambda = { 0.25f };

};