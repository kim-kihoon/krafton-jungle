#pragma once
#include "PointLightComponent.h"

class USpotLightComponent : public UPointLightComponent
{
public:
    DECLARE_CLASS(USpotLightComponent, UPointLightComponent)

    static constexpr const char* BillboardTexturePath = "Asset/Texture/Icons/S_LightSpot.PNG";

    USpotLightComponent();
    ~USpotLightComponent() override = default;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;
    void PostDuplicate(UObject* Original) override;

    const char* GetBillboardTexturePath() const override { return BillboardTexturePath; }

public:
    float GetInnerConeAngle() const { return InnerConeAngle; }
    float GetOuterConeAngle() const { return OuterConeAngle; }

    void SetInnerConeAngle(float InAngle) { InnerConeAngle = InAngle; }
    void SetOuterConeAngle(float InAngle) { OuterConeAngle = InAngle; }

private:
    float InnerConeAngle = 15.0f;
    float OuterConeAngle = 30.0f;
};
