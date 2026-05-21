#pragma once
#include "PointLightComponent.h"

#include "USpotlightComponent.generated.h"
UCLASS()
class USpotlightComponent : public UPointLightComponent
{
public:
    GENERATED_BODY(USpotlightComponent, UPointLightComponent)
    void PostDuplicate(UObject* Origiunal) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	void Serialize(FArchive& Ar) override;

protected:
	FMatrix ComputeCascadeShadowMatrix(const FMatrix& CamView, const FMatrix& CamProj,
		float SplitNearT, float SplitFarT, float ShadowMapResolution) const override;
	FMatrix ComputePerspectiveShadowMatrix(const FMatrix& CamView, const FMatrix& CamProj,
		const TArray<FBoundingBox>* VisibleObjectsBounds) const override;

public:
    float InnerConeAngle = 10.f;
    float OuterConeAngle = 15.f;
};
