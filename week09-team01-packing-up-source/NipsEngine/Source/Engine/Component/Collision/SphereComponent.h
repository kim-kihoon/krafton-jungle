#pragma once

#include "ShapeComponent.h"

class USphereComponent : public UShapeComponent
{
public:
    DECLARE_CLASS(USphereComponent, UShapeComponent)

    USphereComponent();

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;

    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;

    float GetSphereRadius() const { return SphereRadius; }
    void SetSphereRadius(float InSphereRadius);

protected:
    float SphereRadius = 1.0f;
};
