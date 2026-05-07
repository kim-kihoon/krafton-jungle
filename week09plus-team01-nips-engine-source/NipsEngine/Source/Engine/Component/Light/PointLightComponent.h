#pragma once
#include "LightComponent.h"

class UPointLightComponent : public ULightComponent
{
public:
    DECLARE_CLASS(UPointLightComponent, ULightComponent)

    static constexpr const char* BillboardTexturePath = "Asset/Texture/Icons/S_LightPoint.PNG";

    UPointLightComponent();
    ~UPointLightComponent() override = default;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void Serialize(FArchive& Ar) override;
    void PostDuplicate(UObject* Original) override;

    const char* GetBillboardTexturePath() const override { return BillboardTexturePath; }

public:
    float GetAttenuationRadius()    const { return AttenuationRadius; }
    float GetLightFalloffExponent() const { return LightFalloffExponent; }

    void SetAttenuationRadius(float InRadius)       { AttenuationRadius    = InRadius; }
    void SetLightFalloffExponent(float InExponent)  { LightFalloffExponent = InExponent; }

private:
    float AttenuationRadius    = 10.0f;
    float LightFalloffExponent = 1.0f;
};
