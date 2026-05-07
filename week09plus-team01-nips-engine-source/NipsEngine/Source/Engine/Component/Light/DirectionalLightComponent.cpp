#include "DirectionalLightComponent.h"
#include "Object/ObjectFactory.h"

DEFINE_CLASS(UDirectionalLightComponent, ULightComponent)
REGISTER_FACTORY(UDirectionalLightComponent)

UDirectionalLightComponent::UDirectionalLightComponent()
{
    SetLightType(ELightType::LightType_Directional);
}

void UDirectionalLightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    ULightComponent::GetEditableProperties(OutProps);
	static const char* ShadowModeNames[] = { "CSM", "PSM" };
	OutProps.push_back({ "Shadow Mode", EPropertyType::Enum, &ShadowMode, 0.0f, 0.0f, 0.0f, ShadowModeNames, 2 });

	// Cascade Count는 4로 고정하고 외부에 노출하거나 저장하지 않는다.
	OutProps.push_back({ "Shadow Distance", EPropertyType::Float, &ShadowDistance, 500.0f, 30000.0f, 100.0f });
	OutProps.push_back({ "Cascade Split Weight", EPropertyType::Float, &CascadeSplitWeight, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Shadow Texel Snapped", EPropertyType::Bool, &bShadowTexelSnapped });
	OutProps.push_back({ "PSM Virtual Slide Back", EPropertyType::Float, &PSMVirtualSlideBack, 0.0f, 10000.0f, 10.0f });
}

void UDirectionalLightComponent::Serialize(FArchive& Ar)
{
    ULightComponent::Serialize(Ar);
	int32 ShadowModeValue = static_cast<int32>(ShadowMode);
	Ar << "ShadowMode" << ShadowModeValue;
	SetShadowMode(static_cast<EShadowMode>(ShadowModeValue));

	// Cascade Count는 4로 고정하고 외부에 노출하거나 저장하지 않는다.
	Ar << "ShadowDistance" << ShadowDistance;
	Ar << "CascadeSplitWeight" << CascadeSplitWeight;
	Ar << "bShadowTexelSnapped" << bShadowTexelSnapped;
	Ar << "PSMVirtualSlideBack" << PSMVirtualSlideBack;
	SetPSMVirtualSlideBack(PSMVirtualSlideBack);
}

void UDirectionalLightComponent::PostDuplicate(UObject* Original)
{
    ULightComponent::PostDuplicate(Original);

    const UDirectionalLightComponent* Orig = Cast<UDirectionalLightComponent>(Original);
    if (!Orig) { return; }

    ShadowMode = Orig->ShadowMode;
    ShadowDistance = Orig->ShadowDistance;
    CascadeSplitWeight = Orig->CascadeSplitWeight;
    bShadowTexelSnapped = Orig->bShadowTexelSnapped;
    SetPSMVirtualSlideBack(Orig->PSMVirtualSlideBack);
}

