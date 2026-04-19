#pragma once
#include "PrimitiveComponent.h"

class UMesh;

class UHammerComponent : public UPrimitiveComponent
{
    DECLARE_CLASS(UHammerComponent, UPrimitiveComponent)

public:
    UHammerComponent();
    virtual ~UHammerComponent() = default;

public:
    void TickComponent(float DeltaTime) override {};
    virtual void Render(ID3D11DeviceContext& DeviceContext) override;
};