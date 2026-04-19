#pragma once
#include "Component/PrimitiveComponent.h"

class UFireBallComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UFireBallComponent, UPrimitiveComponent)
	UFireBallComponent() {};

	//Inherited
	virtual void Serialize(FArchive& Ar) override;
	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;



	//Getter/Setter
	float GetIntensity() const { return Intensity; }
	float GetRadiusOfSource() const { return RadiusOfSource; }
	float GetRadius() const { return Radius; }
	float GetRadiusFallOff() const { return RadiusFallOff; }
	FVector4 GetColor() const { return Color; }

	void SetRadiusOfSource(float Rad) { if (Rad > 0.01) RadiusOfSource = Rad; }

private:
	float Intensity = 10.f;
	float RadiusOfSource = 10.f;
	float Radius = 10.f;
	float RadiusFallOff = 10.f;
	FVector4 Color;
};