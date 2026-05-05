#pragma once

#include "ShapeComponent.h"

class UBoxComponent : public UShapeComponent
{
public:
    DECLARE_CLASS(UBoxComponent, UShapeComponent)

    UBoxComponent();

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;

    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;

    const FVector& GetBoxExtent() const { return BoxExtent; }
    void SetBoxExtent(const FVector& InBoxExtent);

protected:
    FVector BoxExtent = FVector(0.5f, 0.5f, 0.5f);
};
