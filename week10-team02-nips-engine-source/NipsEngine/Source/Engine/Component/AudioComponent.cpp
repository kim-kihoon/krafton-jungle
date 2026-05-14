#include "Component/AudioComponent.h"

#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cstring>

DEFINE_CLASS(UAudioComponent, USceneComponent)
REGISTER_FACTORY(UAudioComponent)

UAudioComponent::UAudioComponent()
{
	bCanEverTick = true;
}

void UAudioComponent::BeginPlay()
{
	USceneComponent::BeginPlay();
	bStartedByStartBehavior = false;

	if (GetStartBehavior() == EAudioStartBehavior::OnBeginPlay)
	{
		Play();
		bStartedByStartBehavior = PlaybackHandle.IsValid();
	}
}

void UAudioComponent::EndPlay()
{
	Stop();
	StopPreview();
	USceneComponent::EndPlay();
}

void UAudioComponent::OnUnregister()
{
	Stop();
	StopPreview();
	bRegistered = false;
}

void UAudioComponent::PostDuplicate(UObject* Original)
{
	USceneComponent::PostDuplicate(Original);
	PlaybackHandle = {};
	PreviewPlaybackHandle = {};
	bPausedByOutsideRange = false;
	bStoppedByOutsideRange = false;
	bStartedByStartBehavior = false;
}

void UAudioComponent::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar << "SoundPath" << SoundPath;
	Ar << "PlaylistEnabled" << bPlaylistEnabled;
	Ar << "PlaylistPathA" << PlaylistPathA;
	Ar << "PlaylistPathB" << PlaylistPathB;
	Ar << "StartBehavior" << StartBehavior;
	Ar << "Loop" << bLoop;
	Ar << "Spatial" << bSpatial;
	Ar << "AffectedByAudioZones" << bAffectedByAudioZones;
	Ar << "AudioBus" << AudioBus;
	Ar << "OutsideRangeBehavior" << OutsideRangeBehavior;
	Ar << "Volume" << Volume;
	Ar << "MinDistance" << MinDistance;
	Ar << "MaxDistance" << MaxDistance;
	Ar << "AudioRangeVisibility" << AudioRangeVisibility;

	if (Ar.IsLoading())
	{
		PostEditProperty(nullptr);
	}
}

void UAudioComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	static const char* StartBehaviorNames[] = { "On BeginPlay", "On First In Range", "Manual Only" };
	static const char* OutsideBehaviorNames[] = { "Continue Playing", "Pause And Resume", "Stop And Restart" };
	static const char* AudioBusNames[] = { "SFX", "Music", "Ambient" };
	static const char* AudioRangeVisibilityNames[] = { "Use Global", "Force Show", "Force Hide" };

	OutProps.push_back({ "Sound Path", EPropertyType::String, &SoundPath });
	OutProps.push_back({ "Playlist Enabled", EPropertyType::Bool, &bPlaylistEnabled });
	OutProps.push_back({ "Playlist Path A", EPropertyType::String, &PlaylistPathA });
	OutProps.push_back({ "Playlist Path B", EPropertyType::String, &PlaylistPathB });
	OutProps.push_back({ "Audio Bus", EPropertyType::Enum, &AudioBus, 0.0f, 0.0f, 0.0f, AudioBusNames, 3 });
	OutProps.push_back({ "Volume", EPropertyType::Float, &Volume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Loop", EPropertyType::Bool, &bLoop });
	OutProps.push_back({ "Spatial", EPropertyType::Bool, &bSpatial });
	OutProps.push_back({ "Affected By Audio Zones", EPropertyType::Bool, &bAffectedByAudioZones });
	OutProps.push_back({ "Min Distance", EPropertyType::Float, &MinDistance, 0.01f, 10000.0f, 1.0f });
	OutProps.push_back({ "Max Distance", EPropertyType::Float, &MaxDistance, 0.01f, 10000.0f, 1.0f });
	OutProps.push_back({ "Show Audio Range", EPropertyType::Enum, &AudioRangeVisibility, 0.0f, 0.0f, 0.0f, AudioRangeVisibilityNames, 3 });
	OutProps.push_back({ "Start Behavior", EPropertyType::Enum, &StartBehavior, 0.0f, 0.0f, 0.0f, StartBehaviorNames, 3 });
	OutProps.push_back({ "Outside Range Behavior", EPropertyType::Enum, &OutsideRangeBehavior, 0.0f, 0.0f, 0.0f, OutsideBehaviorNames, 3 });
	OutProps.push_back({ "Location", EPropertyType::Vec3, &RelativeLocation, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Rotation", EPropertyType::Vec3, &RelativeRotation, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Scale", EPropertyType::Vec3, &RelativeScale3D, 0.0f, 0.0f, 0.1f });
	OutProps.push_back({ "Enable Tick", EPropertyType::Bool, &bCanEverTick });
	OutProps.push_back({ "Editor Only", EPropertyType::Bool, &bIsEditorOnly });
}

void UAudioComponent::PostEditProperty(const char* PropertyName)
{
	USceneComponent::PostEditProperty(PropertyName);

	if (PropertyName &&
		(strcmp(PropertyName, "Sound Path") == 0 ||
		 strcmp(PropertyName, "Playlist Enabled") == 0 ||
		 strcmp(PropertyName, "Playlist Path A") == 0 ||
		 strcmp(PropertyName, "Playlist Path B") == 0))
	{
		Stop();
		StopPreview();
	}

	Volume = std::clamp(Volume, 0.0f, 2.0f);
	MinDistance = std::max(0.01f, MinDistance);
	MaxDistance = std::max(MinDistance, MaxDistance);
	StartBehavior = std::clamp(StartBehavior, 0, static_cast<int32>(EAudioStartBehavior::Count) - 1);
	AudioBus = std::clamp(AudioBus, 0, static_cast<int32>(EAudioBus::Count) - 1);
	OutsideRangeBehavior = std::clamp(OutsideRangeBehavior, 0, static_cast<int32>(EAudioOutsideBehavior::Count) - 1);
	AudioRangeVisibility = std::clamp(AudioRangeVisibility, 0, static_cast<int32>(EDebugDrawVisibility::Count) - 1);

	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().SetVolume(PlaybackHandle, Volume);
		FAudioSystem::Get().SetLooping(PlaybackHandle, IsPlaylistActive() ? false : bLoop);
		FAudioSystem::Get().SetAffectedByAudioZones(PlaybackHandle, bAffectedByAudioZones);
	}
	if (PreviewPlaybackHandle.IsValid())
	{
		FAudioSystem::Get().SetVolume(PreviewPlaybackHandle, Volume);
		FAudioSystem::Get().SetLooping(PreviewPlaybackHandle, IsPlaylistActive() ? false : bLoop);
	}
}

FAudioHandle UAudioComponent::Play()
{
	PlaylistTrackIndex = 0;
	return PlayCurrentTrack();
}

FAudioHandle UAudioComponent::PlayCurrentTrack()
{
	const FString PlaybackPath = GetCurrentPlaybackPath();
	if (PlaybackPath.empty())
	{
		return {};
	}

	Stop();
	PlaybackHandle = FAudioSystem::Get().Play(PlaybackPath, MakePlayParams());
	bPausedByOutsideRange = false;
	bStoppedByOutsideRange = false;
	bStartedByStartBehavior = PlaybackHandle.IsValid();
	return PlaybackHandle;
}

void UAudioComponent::Pause()
{
	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Pause(PlaybackHandle);
	}
}

void UAudioComponent::Resume()
{
	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Resume(PlaybackHandle);
	}
	else
	{
		Play();
	}
}

void UAudioComponent::Restart()
{
	bPausedByOutsideRange = false;
	bStoppedByOutsideRange = false;
	if (IsPlaylistActive())
	{
		Play();
		return;
	}

	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Restart(PlaybackHandle);
	}
	else
	{
		Play();
	}
}

void UAudioComponent::Stop()
{
	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Stop(PlaybackHandle);
		PlaybackHandle = {};
	}
	bPausedByOutsideRange = false;
	bStoppedByOutsideRange = false;
}

bool UAudioComponent::IsPlaying() const
{
	return PlaybackHandle.IsValid() && FAudioSystem::Get().IsPlaying(PlaybackHandle);
}

void UAudioComponent::SetPlaybackTime(float TimeSeconds)
{
	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().SetPlaybackTime(PlaybackHandle, TimeSeconds);
	}
}

float UAudioComponent::GetPlaybackTime() const
{
	return PlaybackHandle.IsValid() ? FAudioSystem::Get().GetPlaybackTime(PlaybackHandle) : 0.0f;
}

float UAudioComponent::GetDuration() const
{
	if (PlaybackHandle.IsValid())
	{
		const float ActiveDuration = FAudioSystem::Get().GetDuration(PlaybackHandle);
		if (ActiveDuration > 0.0f)
		{
			return ActiveDuration;
		}
	}

	return FAudioSystem::Get().GetSoundDuration(GetCurrentPlaybackPath());
}

FAudioHandle UAudioComponent::PlayPreview()
{
	const FString PreviewPath = GetCurrentPlaybackPath();
	if (PreviewPath.empty())
	{
		return {};
	}

	StopPreview();
	PreviewPlaybackHandle = FAudioSystem::Get().Play2D(PreviewPath, Volume, IsPlaylistActive() ? false : bLoop);
	return PreviewPlaybackHandle;
}

void UAudioComponent::PausePreview()
{
	if (PreviewPlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Pause(PreviewPlaybackHandle);
	}
}

void UAudioComponent::ResumePreview()
{
	if (PreviewPlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Resume(PreviewPlaybackHandle);
	}
	else
	{
		PlayPreview();
	}
}

void UAudioComponent::RestartPreview()
{
	if (PreviewPlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Restart(PreviewPlaybackHandle);
	}
	else
	{
		PlayPreview();
	}
}

void UAudioComponent::StopPreview()
{
	if (PreviewPlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Stop(PreviewPlaybackHandle);
		PreviewPlaybackHandle = {};
	}
}

bool UAudioComponent::IsPreviewPlaying() const
{
	return PreviewPlaybackHandle.IsValid() && FAudioSystem::Get().IsPlaying(PreviewPlaybackHandle);
}

void UAudioComponent::SetPreviewPlaybackTime(float TimeSeconds)
{
	if (PreviewPlaybackHandle.IsValid())
	{
		FAudioSystem::Get().SetPlaybackTime(PreviewPlaybackHandle, TimeSeconds);
	}
}

float UAudioComponent::GetPreviewPlaybackTime() const
{
	return PreviewPlaybackHandle.IsValid() ? FAudioSystem::Get().GetPlaybackTime(PreviewPlaybackHandle) : 0.0f;
}

float UAudioComponent::GetPreviewDuration() const
{
	if (PreviewPlaybackHandle.IsValid())
	{
		const float ActiveDuration = FAudioSystem::Get().GetDuration(PreviewPlaybackHandle);
		if (ActiveDuration > 0.0f)
		{
			return ActiveDuration;
		}
	}

	return FAudioSystem::Get().GetSoundDuration(GetCurrentPlaybackPath());
}

void UAudioComponent::TickComponent(float DeltaTime)
{
	(void)DeltaTime;

	if (!PlaybackHandle.IsValid())
	{
		if (bStoppedByOutsideRange && !IsListenerOutsideMaxDistance())
		{
			bStoppedByOutsideRange = false;
			Play();
		}
		else if (ShouldAutoStart())
		{
			Play();
		}
		return;
	}

	const EAudioOutsideBehavior OutsideBehavior = GetOutsideRangeBehavior();
	const bool bOutsideRange = IsListenerOutsideMaxDistance();
	if (bOutsideRange)
	{
		if (OutsideBehavior == EAudioOutsideBehavior::PauseAndResume)
		{
			if (FAudioSystem::Get().IsPlaying(PlaybackHandle))
			{
				FAudioSystem::Get().Pause(PlaybackHandle);
				bPausedByOutsideRange = true;
			}
			return;
		}

		if (OutsideBehavior == EAudioOutsideBehavior::StopAndRestart)
		{
			FAudioSystem::Get().Stop(PlaybackHandle);
			PlaybackHandle = {};
			bPausedByOutsideRange = false;
			bStoppedByOutsideRange = true;
			return;
		}
	}
	else if (bPausedByOutsideRange)
	{
		FAudioSystem::Get().Resume(PlaybackHandle);
		bPausedByOutsideRange = false;
	}

	if (!FAudioSystem::Get().IsPlaying(PlaybackHandle))
	{
		const bool bHandleActive = FAudioSystem::Get().IsHandleActive(PlaybackHandle);
		const bool bTrackEnded = !bHandleActive || FAudioSystem::Get().IsAtEnd(PlaybackHandle);
		if (IsPlaylistActive() && bTrackEnded)
		{
			const int32 TrackCount = GetPlaylistTrackCount();
			if (TrackCount > 0)
			{
				PlaylistTrackIndex = (PlaylistTrackIndex + 1) % TrackCount;
				PlayCurrentTrack();
				return;
			}
		}

		PlaybackHandle = {};
		bPausedByOutsideRange = false;
		return;
	}

	if (bSpatial)
	{
		FAudioSystem::Get().SetSoundPosition(PlaybackHandle, GetAudioLocation());
	}
}

FAudioPlayParams UAudioComponent::MakePlayParams() const
{
	FAudioPlayParams Params;
	Params.bLoop = IsPlaylistActive() ? false : bLoop;
	Params.bSpatial = bSpatial;
	Params.bAffectedByAudioZones = bAffectedByAudioZones;
	Params.Bus = static_cast<EAudioBus>(AudioBus);
	Params.Volume = Volume;
	Params.MinDistance = MinDistance;
	Params.MaxDistance = MaxDistance;
	Params.Location = GetAudioLocation();
	return Params;
}

FVector UAudioComponent::GetAudioLocation() const
{
	return GetWorldLocation();
}

FVector UAudioComponent::GetListenerLocation() const
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = OwnerActor ? OwnerActor->GetFocusedWorld() : nullptr;
	const FViewportCamera* Camera = World ? World->GetActiveCamera() : nullptr;
	return Camera ? Camera->GetLocation() : GetAudioLocation();
}

bool UAudioComponent::IsListenerOutsideMaxDistance() const
{
	if (!bSpatial || MaxDistance <= 0.0f)
	{
		return false;
	}

	return FVector::Distance(GetListenerLocation(), GetAudioLocation()) > MaxDistance;
}

EAudioOutsideBehavior UAudioComponent::GetOutsideRangeBehavior() const
{
	const int32 ClampedValue = std::clamp(
		OutsideRangeBehavior,
		0,
		static_cast<int32>(EAudioOutsideBehavior::Count) - 1);
	return static_cast<EAudioOutsideBehavior>(ClampedValue);
}

EAudioStartBehavior UAudioComponent::GetStartBehavior() const
{
	const int32 ClampedValue = std::clamp(
		StartBehavior,
		0,
		static_cast<int32>(EAudioStartBehavior::Count) - 1);
	return static_cast<EAudioStartBehavior>(ClampedValue);
}

bool UAudioComponent::ShouldAutoStart() const
{
	if (bStartedByStartBehavior || !HasPlayableSound())
	{
		return false;
	}

	const EAudioStartBehavior Behavior = GetStartBehavior();
	if (Behavior != EAudioStartBehavior::OnFirstEnter)
	{
		return false;
	}

	return !IsListenerOutsideMaxDistance();
}

bool UAudioComponent::IsPlaylistActive() const
{
	return bPlaylistEnabled && GetPlaylistTrackCount() > 0;
}

bool UAudioComponent::HasPlayableSound() const
{
	return !GetCurrentPlaybackPath().empty();
}

int32 UAudioComponent::GetPlaylistTrackCount() const
{
	int32 Count = 0;
	if (!PlaylistPathA.empty())
	{
		++Count;
	}
	if (!PlaylistPathB.empty())
	{
		++Count;
	}
	return Count;
}

FString UAudioComponent::GetPlaylistTrackPath(int32 TrackIndex) const
{
	const bool bHasA = !PlaylistPathA.empty();
	const bool bHasB = !PlaylistPathB.empty();
	if (bHasA && bHasB)
	{
		return (TrackIndex % 2) == 0 ? PlaylistPathA : PlaylistPathB;
	}
	return bHasA ? PlaylistPathA : PlaylistPathB;
}

FString UAudioComponent::GetCurrentPlaybackPath() const
{
	if (IsPlaylistActive())
	{
		return GetPlaylistTrackPath(PlaylistTrackIndex);
	}
	return SoundPath;
}
