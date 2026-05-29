#pragma once
#include "Component/Light/LightComponent.h"

#include "AmbientLightComponent.generated.h"

UCLASS()
class UAmbientLightComponent : public ULightComponent
{
public:
	GENERATED_BODY(UAmbientLightComponent)
	UAmbientLightComponent();

	virtual ELightComponentType GetLightType() const override { return ELightComponentType::Ambient; }
	virtual void PushToScene() override;
	virtual void DestroyFromScene() override;
};