#include "AmbientLightComponent.h"
#include "Object/ObjectFactory.h"

DEFINE_CLASS(UAmbientLightComponent, ULightComponent)
REGISTER_FACTORY(UAmbientLightComponent)

UAmbientLightComponent::UAmbientLightComponent()
{
    SetLightType(ELightType::LightType_AmbientLight);
    SetLightColor({ 0.2f, 0.2f, 0.2f, 1.0f });
}

void UAmbientLightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    ULightComponent::GetEditableProperties(OutProps);
}

void UAmbientLightComponent::Serialize(FArchive& Ar)
{
    ULightComponent::Serialize(Ar);
}

void UAmbientLightComponent::PostDuplicate(UObject* Original)
{
    ULightComponent::PostDuplicate(Original);
}
