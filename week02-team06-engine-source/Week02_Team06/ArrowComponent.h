#pragma once
#include "PrimitiveComponent.h"

class UArrowComponent : public UPrimitiveComponent
{
    DECLARE_CLASS(UArrowComponent, UPrimitiveComponent)

public:
    UArrowComponent() = default;
    virtual ~UArrowComponent() = default;

public:
    void TickComponent(float DeltaTime) override {};
    virtual void Render(ID3D11DeviceContext& DevcieContext) override;
};
