#include "PointLightComponent.h"
#include "Object/ObjectFactory.h"

DEFINE_CLASS(UPointLightComponent, ULightComponent)
REGISTER_FACTORY(UPointLightComponent)

UPointLightComponent::UPointLightComponent()
{
	SetLightType(ELightType::LightType_Point);
	SetCastShadows(false); // Point Light의 그림자 연산은 심각하게 비싸기 때문에 기본값을 false로 설정한다.
}

void UPointLightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	ULightComponent::GetEditableProperties(OutProps);
	
	OutProps.push_back({ "Attenuation Radius",     EPropertyType::Float, &AttenuationRadius, 0.0f, 10000.0f, 1.0f });
	OutProps.push_back({ "Light Falloff Exponent", EPropertyType::Float, &LightFalloffExponent, 0.01f, 16.0f,    0.01f });
	OutProps.push_back({ "Shadow Texel Snapped", EPropertyType::Bool, &bShadowTexelSnapped });
}

void UPointLightComponent::Serialize(FArchive& Ar)
{
	ULightComponent::Serialize(Ar);
	
	Ar << "AttenuationRadius"    << AttenuationRadius;
	Ar << "LightFalloffExponent" << LightFalloffExponent;
	Ar << "bShadowTexelSnapped" << bShadowTexelSnapped;
}

void UPointLightComponent::PostDuplicate(UObject* Original)
{
	ULightComponent::PostDuplicate(Original);

	const UPointLightComponent* Orig = Cast<UPointLightComponent>(Original);
	if (!Orig) { return; }

	AttenuationRadius    = Orig->AttenuationRadius;
	LightFalloffExponent = Orig->LightFalloffExponent;
}

