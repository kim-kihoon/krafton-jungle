#include "AnimNotify_PlaySound.h"

#include "Component/Audio/AudioComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"

namespace
{
	FString SanitizeSoundPath(const FString& InPath)
	{
		FString Path = InPath;
		auto IsSpace = [](char Character)
		{
			return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n';
		};

		while (!Path.empty() && IsSpace(Path.back()))
		{
			Path.pop_back();
		}

		size_t Start = 0;
		while (Start < Path.size() && IsSpace(Path[Start]))
		{
			++Start;
		}
		if (Start > 0)
		{
			Path.erase(0, Start);
		}
		return Path;
	}

	bool PathsMatch(const FString& Left, const FString& Right)
	{
		return SanitizeSoundPath(Left) == SanitizeSoundPath(Right);
	}

	bool ContainsSoundPath(const TArray<FAnimNotifySoundEntry>& Sounds, const FString& Path)
	{
		for (const FAnimNotifySoundEntry& Sound : Sounds)
		{
			if (PathsMatch(Sound.SoundPath, Path))
			{
				return true;
			}
		}
		return false;
	}

	bool ContainsSoundPath(const TArray<FString>& Paths, const FString& Path)
	{
		for (const FString& Entry : Paths)
		{
			if (PathsMatch(Entry, Path))
			{
				return true;
			}
		}
		return false;
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

	float ResolvePlaybackVolume(float Volume, float TrackVolumeScale)
	{
		const float ScaledVolume = Volume * TrackVolumeScale;
		if (ScaledVolume > 0.001f)
		{
			return ScaledVolume;
		}
		return TrackVolumeScale;
	}

	bool PlayNotifySoundPath(UAudioComponent* AudioComponent, const FString& InSoundPath, float Volume, float Pitch)
	{
		const FString SoundPath = SanitizeSoundPath(InSoundPath);
		if (!IsValid(AudioComponent) || SoundPath.empty())
		{
			return false;
		}

		AudioComponent->PlayOneShot(SoundPath, Volume, Pitch);
		return true;
	}
}

void UAnimNotify_PlaySound::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* /*Anim*/)
{
	UAudioComponent* AudioComponent = ResolveNotifyAudioComponent(MeshComp);
	if (!AudioComponent)
	{
		return;
	}

	const float TrackVolumeScale = GetDispatchVolumeScale();

	for (const FAnimNotifySoundEntry& Sound : Sounds)
	{
		if (Sound.SoundPath.empty())
		{
			continue;
		}
		PlayNotifySoundPath(
			AudioComponent,
			Sound.SoundPath,
			ResolvePlaybackVolume(Sound.Volume, TrackVolumeScale),
			Sound.Pitch);
	}

	for (const FString& Path : SoundPaths)
	{
		if (Path.empty() || ContainsSoundPath(Sounds, Path))
		{
			continue;
		}
		PlayNotifySoundPath(
			AudioComponent,
			Path,
			ResolvePlaybackVolume(Volume, TrackVolumeScale),
			1.0f);
	}

	if (!SoundPath.empty() &&
		!ContainsSoundPath(Sounds, SoundPath) &&
		!ContainsSoundPath(SoundPaths, SoundPath))
	{
		PlayNotifySoundPath(
			AudioComponent,
			SoundPath,
			ResolvePlaybackVolume(Volume, TrackVolumeScale),
			1.0f);
	}
}
