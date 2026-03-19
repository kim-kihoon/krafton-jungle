#pragma once

#include "PrimitiveComponent.h"

class UMesh;

class UCubeComponent : public UPrimitiveComponent
{
    DECLARE_CLASS(UCubeComponent, UPrimitiveComponent)

public:
    UCubeComponent();
    virtual ~UCubeComponent() = default;

public:
    void TickComponent(float DeltaTime) override {};

    virtual void Render(ID3D11DeviceContext& DevcieContext) override;

};
