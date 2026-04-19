#pragma once

#include "PrimitiveComponent.h"

class UMesh;

class UTriangleComponent : public UPrimitiveComponent
{
    DECLARE_CLASS(UTriangleComponent, UPrimitiveComponent)

public:
    UTriangleComponent();
    virtual ~UTriangleComponent() = default;

public:
    void TickComponent(float DeltaTime) override {};

    virtual void Render(ID3D11DeviceContext& DevcieContext) override;

};