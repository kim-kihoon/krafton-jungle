#pragma once

#include "ShapeComponent.h"

class UCapsuleComponent : public UShapeComponent
{
public:
    DECLARE_CLASS(UCapsuleComponent, UShapeComponent)

    UCapsuleComponent();

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;

    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
    EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_Line; }

    float GetCapsuleHalfHeight() const { return CapsuleHalfHeight; }
    float GetCapsuleRadius() const { return CapsuleRadius; }
    void SetCapsuleSize(float InCapsuleHalfHeight, float InCapsuleRadius);

protected:
    float CapsuleHalfHeight = 1.0f;
    float CapsuleRadius = 1.0f;
};
