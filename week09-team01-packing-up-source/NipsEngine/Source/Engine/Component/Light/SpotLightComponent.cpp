#include "SpotLightComponent.h"
#include "Object/ObjectFactory.h"

DEFINE_CLASS(USpotLightComponent, UPointLightComponent)
REGISTER_FACTORY(USpotLightComponent)

USpotLightComponent::USpotLightComponent()
{
    SetLightType(ELightType::LightType_Spot);
}

void USpotLightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UPointLightComponent::GetEditableProperties(OutProps);
	
    OutProps.push_back({ "Inner Cone Angle", EPropertyType::Float, &InnerConeAngle, 0.0f, 80.0f, 0.1f });
    OutProps.push_back({ "Outer Cone Angle", EPropertyType::Float, &OuterConeAngle, 0.0f, 80.0f, 0.1f });
}

void USpotLightComponent::PostEditProperty(const char* PropertyName)
{
    UPointLightComponent::PostEditProperty(PropertyName);

    if (strcmp(PropertyName, "Inner Cone Angle") == 0)
    {
        if (InnerConeAngle > OuterConeAngle)
        {
            OuterConeAngle = InnerConeAngle;
        }
    }
    else if (strcmp(PropertyName, "Outer Cone Angle") == 0)
    {
        if (OuterConeAngle < InnerConeAngle)
        {
            InnerConeAngle = OuterConeAngle;
        }
    }
}

void USpotLightComponent::Serialize(FArchive& Ar)
{
    UPointLightComponent::Serialize(Ar);
	
    Ar << "InnerConeAngle" << InnerConeAngle;
    Ar << "OuterConeAngle" << OuterConeAngle;
	Ar << "bShadowTexelSnapped" << bShadowTexelSnapped;
}

void USpotLightComponent::PostDuplicate(UObject* Original)
{
    UPointLightComponent::PostDuplicate(Original);

    const USpotLightComponent* Orig = Cast<USpotLightComponent>(Original);
    if (!Orig) { return; }

    InnerConeAngle = Orig->InnerConeAngle;
    OuterConeAngle = Orig->OuterConeAngle;
}

