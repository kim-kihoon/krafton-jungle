#pragma once
#include "Component/Light/LightComponentBase.h"
#include "Component/Light/LightComponent.h"

#include "DirectionalLightComponent.generated.h"

UCLASS()
class UDirectionalLightComponent : public ULightComponent
{
public:
	GENERATED_BODY(UDirectionalLightComponent)
	virtual ELightComponentType GetLightType() const override { return ELightComponentType::Directional; }
	void ContributeSelectedVisuals(FScene& Scene) const;
	virtual void PushToScene() override;
	virtual void DestroyFromScene() override;
	virtual bool GetLightViewProj(FLightViewProjResult& OutResult, const FMinimalViewInfo* POV = nullptr, int32 FaceIndex = 0) const override;
};
