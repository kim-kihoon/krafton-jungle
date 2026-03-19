#pragma once
#include "PrimitiveComponent.h"

class UMesh;

class URingComponent : public UPrimitiveComponent
{
    DECLARE_CLASS(URingComponent, UPrimitiveComponent)

public:
    URingComponent();
    virtual ~URingComponent() = default;

public:
    void TickComponent(float DeltaTime) override {};
    virtual void Render(ID3D11DeviceContext& DeviceContext) override;
};