#include "PostProcessComponent.h"
#include "Object/ObjectFactory.h"

DEFINE_CLASS(UPostProcessComponent, UActorComponent)
REGISTER_FACTORY(UPostProcessComponent)

void UPostProcessComponent::SetVignette(float Intensity, float Radius, float Softness)
{
    SetVignetteIntensity(Intensity);
    SetVignetteRadius(Radius);
    SetVignetteSoftness(Softness);
}

void UPostProcessComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UActorComponent::GetEditableProperties(OutProps);

    OutProps.push_back({ "Vignette", EPropertyType::Bool, &bVignette });
    OutProps.push_back({ "Vignette Intensity", EPropertyType::Float, &VignetteIntensity, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Vignette Radius", EPropertyType::Float, &VignetteRadius, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Vignette Softness", EPropertyType::Float, &VignetteSoftness, 0.0f, 1.0f, 0.01f });

    OutProps.push_back({ "Gamma Correction", EPropertyType::Bool, &bGammaCorrection });
    OutProps.push_back({ "Gamma Value", EPropertyType::Float, &Gamma, 0.0f, 10.0f, 0.01f });
}

void UPostProcessComponent::Serialize(FArchive& Ar)
{
    UActorComponent::Serialize(Ar);
	
    Ar << "Vignette" << bVignette;
    Ar << "VignetteIntensity" << VignetteIntensity;
    Ar << "VignetteRadius" << VignetteRadius;
    Ar << "VignetteSoftness" << VignetteSoftness;
	
    Ar << "Gamma" << bGammaCorrection;
    Ar << "Gamma Value" << Gamma;
}

void UPostProcessComponent::PostDuplicate(UObject* Original)
{
    UActorComponent::PostDuplicate(Original);
}
