#include "Component/AudioZoneComponent.h"

#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

DEFINE_CLASS(UAudioZoneComponent, USceneComponent)
REGISTER_FACTORY(UAudioZoneComponent)

UAudioZoneComponent::UAudioZoneComponent()
{
	bCanEverTick = true;
}

void UAudioZoneComponent::BeginPlay()
{
	USceneComponent::BeginPlay();
	SubmitMix();
}

void UAudioZoneComponent::EndPlay()
{
	RemoveMix();
	USceneComponent::EndPlay();
}

void UAudioZoneComponent::OnUnregister()
{
	RemoveMix();
	bRegistered = false;
}

void UAudioZoneComponent::PostDuplicate(UObject* Original)
{
	USceneComponent::PostDuplicate(Original);
	RemoveMix();
}

void UAudioZoneComponent::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar << "BoxExtent" << BoxExtent;
	Ar << "Priority" << Priority;
	if (Ar.IsLoading())
	{
		float DeprecatedFadeInTime = 0.0f;
		float DeprecatedFadeOutTime = 0.0f;
		Ar << "FadeInTime" << DeprecatedFadeInTime;
		Ar << "FadeOutTime" << DeprecatedFadeOutTime;
	}
	Ar << "MasterVolume" << MasterVolume;
	Ar << "SFXVolume" << SFXVolume;
	Ar << "MusicVolume" << MusicVolume;
	Ar << "AmbientVolume" << AmbientVolume;
	Ar << "ExteriorMasterVolume" << ExteriorMasterVolume;   
	Ar << "ExteriorSFXVolume" << ExteriorSFXVolume;
	Ar << "ExteriorMusicVolume" << ExteriorMusicVolume;
	Ar << "ExteriorAmbientVolume" << ExteriorAmbientVolume;
	Ar << "InteriorLowPassCutoff" << InteriorLowPassCutoff;
	Ar << "ExteriorLowPassCutoff" << ExteriorLowPassCutoff;
	Ar << "InteriorReverbWet" << InteriorReverbWet;
	Ar << "InteriorReverbDecay" << InteriorReverbDecay;
	Ar << "ExteriorReverbWet" << ExteriorReverbWet;
	Ar << "ExteriorReverbDecay" << ExteriorReverbDecay;
	Ar << "AudioRangeVisibility" << AudioRangeVisibility;

	if (Ar.IsLoading())
	{
		ClampEditableValues();
	}
}

void UAudioZoneComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	static const char* AudioRangeVisibilityNames[] = { "Use Global", "Force Show", "Force Hide" };

	OutProps.push_back({ "Location", EPropertyType::Vec3, &RelativeLocation, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Rotation", EPropertyType::Vec3, &RelativeRotation, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Scale", EPropertyType::Vec3, &RelativeScale3D, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Box Extent", EPropertyType::Vec3, &BoxExtent, 0.01f, 10000.0f, 0.1f });
	OutProps.push_back({ "Priority", EPropertyType::Int, &Priority, -1000.0f, 1000.0f, 1.0f });
	OutProps.push_back({ "Show Audio Range", EPropertyType::Enum, &AudioRangeVisibility, 0.0f, 0.0f, 0.0f, AudioRangeVisibilityNames, 3 });
	OutProps.push_back({ "Enable Tick", EPropertyType::Bool, &bCanEverTick });
	OutProps.push_back({ "Editor Only", EPropertyType::Bool, &bIsEditorOnly });
	OutProps.push_back({ "Interior Master Volume", EPropertyType::Float, &MasterVolume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Interior SFX Volume", EPropertyType::Float, &SFXVolume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Interior Music Volume", EPropertyType::Float, &MusicVolume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Interior Ambient Volume", EPropertyType::Float, &AmbientVolume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Exterior Master Volume", EPropertyType::Float, &ExteriorMasterVolume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Exterior SFX Volume", EPropertyType::Float, &ExteriorSFXVolume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Exterior Music Volume", EPropertyType::Float, &ExteriorMusicVolume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Exterior Ambient Volume", EPropertyType::Float, &ExteriorAmbientVolume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Interior LowPass Cutoff", EPropertyType::Float, &InteriorLowPassCutoff, 100.0f, 20000.0f, 100.0f });
	OutProps.push_back({ "Exterior LowPass Cutoff", EPropertyType::Float, &ExteriorLowPassCutoff, 100.0f, 20000.0f, 100.0f });
	OutProps.push_back({ "Interior Reverb Wet", EPropertyType::Float, &InteriorReverbWet, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Interior Reverb Decay", EPropertyType::Float, &InteriorReverbDecay, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Exterior Reverb Wet", EPropertyType::Float, &ExteriorReverbWet, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Exterior Reverb Decay", EPropertyType::Float, &ExteriorReverbDecay, 0.0f, 1.0f, 0.01f });
}

void UAudioZoneComponent::PostEditProperty(const char* PropertyName)
{
	USceneComponent::PostEditProperty(PropertyName);
	ClampEditableValues();
}

FVector UAudioZoneComponent::GetScaledBoxExtent() const
{
	const FVector WorldScale = GetWorldScale();
	return FVector(
		BoxExtent.X * std::abs(WorldScale.X),
		BoxExtent.Y * std::abs(WorldScale.Y),
		BoxExtent.Z * std::abs(WorldScale.Z));
}

void UAudioZoneComponent::TickComponent(float DeltaTime)
{
	(void)DeltaTime;
	SubmitMix();
}

void UAudioZoneComponent::SubmitMix()
{
	FAudioSystem::Get().SubmitZoneMix(
		GetUUID(),
		Priority,
		GetWorldLocation(),
		GetForwardVector(),
		GetRightVector(),
		GetUpVector(),
		GetScaledBoxExtent(),
		MasterVolume,
		SFXVolume,
		MusicVolume,
		AmbientVolume,
		ExteriorMasterVolume,
		ExteriorSFXVolume,
		ExteriorMusicVolume,
		ExteriorAmbientVolume,
		InteriorLowPassCutoff,
		ExteriorLowPassCutoff,
		InteriorReverbWet,
		InteriorReverbDecay,
		ExteriorReverbWet,
		ExteriorReverbDecay);
}

void UAudioZoneComponent::RemoveMix()
{
	FAudioSystem::Get().RemoveZoneMix(GetUUID());
}

void UAudioZoneComponent::ClampEditableValues()
{
	BoxExtent.X = std::max(0.01f, std::abs(BoxExtent.X));
	BoxExtent.Y = std::max(0.01f, std::abs(BoxExtent.Y));
	BoxExtent.Z = std::max(0.01f, std::abs(BoxExtent.Z));
	MasterVolume = std::clamp(MasterVolume, 0.0f, 2.0f);
	SFXVolume = std::clamp(SFXVolume, 0.0f, 2.0f);
	MusicVolume = std::clamp(MusicVolume, 0.0f, 2.0f);
	AmbientVolume = std::clamp(AmbientVolume, 0.0f, 2.0f);
	ExteriorMasterVolume = std::clamp(ExteriorMasterVolume, 0.0f, 2.0f);
	ExteriorSFXVolume = std::clamp(ExteriorSFXVolume, 0.0f, 2.0f);
	ExteriorMusicVolume = std::clamp(ExteriorMusicVolume, 0.0f, 2.0f);
	ExteriorAmbientVolume = std::clamp(ExteriorAmbientVolume, 0.0f, 2.0f);
	InteriorLowPassCutoff = std::clamp(InteriorLowPassCutoff, 100.0f, 20000.0f);
	ExteriorLowPassCutoff = std::clamp(ExteriorLowPassCutoff, 100.0f, 20000.0f);
	InteriorReverbWet = std::clamp(InteriorReverbWet, 0.0f, 1.0f);
	InteriorReverbDecay = std::clamp(InteriorReverbDecay, 0.0f, 1.0f);
	ExteriorReverbWet = std::clamp(ExteriorReverbWet, 0.0f, 1.0f);
	ExteriorReverbDecay = std::clamp(ExteriorReverbDecay, 0.0f, 1.0f);
	AudioRangeVisibility = std::clamp(AudioRangeVisibility, 0, static_cast<int32>(EDebugDrawVisibility::Count) - 1);
}
