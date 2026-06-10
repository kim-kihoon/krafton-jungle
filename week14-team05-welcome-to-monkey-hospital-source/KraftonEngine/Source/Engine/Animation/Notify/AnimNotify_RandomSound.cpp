#include "AnimNotify_RandomSound.h"

#include "Component/Audio/AudioComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"

#include <random>

namespace
{
	const FAnimNotifySoundEntry* PickRandomSoundEntry(const TArray<FAnimNotifySoundEntry>& Sounds)
	{
		TArray<const FAnimNotifySoundEntry*> Candidates;
		for (const FAnimNotifySoundEntry& Sound : Sounds)
		{
			if (!Sound.SoundPath.empty())
			{
				Candidates.push_back(&Sound);
			}
		}

		if (Candidates.empty())
		{
			return nullptr;
		}

		static std::mt19937 RNG{ std::random_device{}() };
		std::uniform_int_distribution<size_t> Dist(0, Candidates.size() - 1);
		return Candidates[Dist(RNG)];
	}

	FAnimNotifySoundEntry PickLegacyRandomSoundEntry(const TArray<FString>& SoundPaths, float Volume)
	{
		TArray<const FString*> Candidates;
		for (const FString& Path : SoundPaths)
		{
			if (!Path.empty())
			{
				Candidates.push_back(&Path);
			}
		}

		if (Candidates.empty())
		{
			return FAnimNotifySoundEntry();
		}

		static std::mt19937 RNG{ std::random_device{}() };
		std::uniform_int_distribution<size_t> Dist(0, Candidates.size() - 1);
		FAnimNotifySoundEntry Result;
		Result.SoundPath = *Candidates[Dist(RNG)];
		Result.Volume = Volume;
		Result.Pitch = 1.0f;
		return Result;
	}

	UAudioComponent* ResolveNotifyAudioComponent(USkeletalMeshComponent* MeshComp)
	{
		if (!IsValid(MeshComp))
		{
			return nullptr;
		}

		AActor* Owner = MeshComp->GetOwner();
		if (!IsValid(Owner))
		{
			return nullptr;
		}

		if (UAudioComponent* ExistingAudioComponent = Owner->GetComponentByClass<UAudioComponent>())
		{
			return ExistingAudioComponent;
		}

		UAudioComponent* NewAudioComponent = Owner->AddComponent<UAudioComponent>();
		if (IsValid(NewAudioComponent))
		{
			NewAudioComponent->SetAutoActivate(false);
			NewAudioComponent->SetHiddenInComponentTree(true);
			NewAudioComponent->SetComponentTickEnabled(false);
			NewAudioComponent->AttachToComponent(MeshComp);
		}
		return NewAudioComponent;
	}
}

void UAnimNotify_RandomSound::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* /*Anim*/)
{
	UAudioComponent* AudioComponent = ResolveNotifyAudioComponent(MeshComp);
	if (!AudioComponent)
	{
		return;
	}

	const float TrackVolumeScale = GetDispatchVolumeScale();
	if (!Sounds.empty())
	{
		if (const FAnimNotifySoundEntry* Sound = PickRandomSoundEntry(Sounds))
		{
			FAnimNotifySoundEntry ScaledSound = *Sound;
			ScaledSound.Volume *= TrackVolumeScale;
			AudioComponent->PlayOneShot(ScaledSound.SoundPath, ScaledSound.Volume, ScaledSound.Pitch);
		}
		return;
	}

	FAnimNotifySoundEntry LegacySound = PickLegacyRandomSoundEntry(SoundPaths, Volume);
	LegacySound.Volume *= TrackVolumeScale;
	if (!LegacySound.SoundPath.empty())
	{
		AudioComponent->PlayOneShot(LegacySound.SoundPath, LegacySound.Volume, LegacySound.Pitch);
	}
}
