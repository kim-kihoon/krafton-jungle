#pragma once

#include "PrimitiveComponent.h"

class UMesh;

class USphereComponent : public UPrimitiveComponent
{
    DECLARE_CLASS(USphereComponent, UPrimitiveComponent)

public:
    USphereComponent();
    virtual ~USphereComponent() = default;

public:
    void TickComponent(float DeltaTime) override {};

    virtual void Render(ID3D11DeviceContext& DevcieContext) override;
};
