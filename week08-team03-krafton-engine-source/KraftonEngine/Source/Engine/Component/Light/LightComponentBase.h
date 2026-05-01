#pragma once
#include "Component/SceneComponent.h"

class ULightComponentBase : public USceneComponent
{
public:
	DECLARE_CLASS(ULightComponentBase, USceneComponent)

	ULightComponentBase() { SetComponentTickEnabled(false); }

	virtual void PushToScene() {};
	virtual void DestroyFromScene() {};
	virtual void OnTransformDirty() override;
	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	virtual void PostEditProperty(const char* PropertyName) override;
	virtual void CreateRenderState() override { PushToScene(); }
	virtual void DestroyRenderState() override { DestroyFromScene(); }

	virtual void Serialize(FArchive& Ar) override;

	float GetIntensity() const { return Intensity; }
	FVector4 GetLightColor() const { return LightColor; }
	bool IsVisible() const { return bVisible; }
	bool IsCastShadow() const { return bCastShadow; }

protected:
	float Intensity = 1.f;;
	FVector4 LightColor = { 1.0f,1.0f,1.0f,1.0f };
	bool bVisible = true;
	bool bCastShadow = true;
};
