#pragma once
#include "LightComponent.h"

class UAmbientLightComponent : public ULightComponent
{
public:
    DECLARE_CLASS(UAmbientLightComponent, ULightComponent)

    static constexpr const char* BillboardTexturePath = "Asset/Texture/Icons/SkyLight.PNG";

    UAmbientLightComponent();
    ~UAmbientLightComponent() override = default;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void Serialize(FArchive& Ar) override;
    void PostDuplicate(UObject* Original) override;

    const char* GetBillboardTexturePath() const override { return BillboardTexturePath; }
};
