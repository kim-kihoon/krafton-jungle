#pragma once

#include "ShapeComponent.h"

class UCylinderComponent : public UShapeComponent
{
public:
	DECLARE_CLASS(UCylinderComponent, UShapeComponent)

	UCylinderComponent();

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void Serialize(FArchive& Ar) override;

	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_Line; }

	float GetCylinderHalfHeight() const { return CylinderHalfHeight; }
	float GetCylinderRadius() const { return CylinderRadius; }
	void SetCylinderSize(float InCylinderHalfHeight, float InCylinderRadius);

protected:
	float CylinderHalfHeight = 0.1f;
	float CylinderRadius = 0.5f;
};
