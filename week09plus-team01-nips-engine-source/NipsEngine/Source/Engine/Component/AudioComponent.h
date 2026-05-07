#pragma once

#include "Component/SceneComponent.h"
#include "Audio/AudioSystem.h"
#include "Render/Common/ViewTypes.h"

class UAudioComponent : public USceneComponent
{
public:
	DECLARE_CLASS(UAudioComponent, USceneComponent)

	UAudioComponent();
	~UAudioComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void OnUnregister() override;
	void PostDuplicate(UObject* Original) override;
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	FAudioHandle Play();
	void Pause();
	void Resume();
	void Restart();
	void Stop();
	bool IsPlaying() const;
	bool HasPlayback() const { return FAudioSystem::Get().IsHandleActive(PlaybackHandle); }
	void SetPlaybackTime(float TimeSeconds);
	float GetPlaybackTime() const;
	float GetDuration() const;
	FAudioHandle PlayPreview();
	void PausePreview();
	void ResumePreview();
	void RestartPreview();
	void StopPreview();
	bool IsPreviewPlaying() const;
	bool HasPreviewPlayback() const { return FAudioSystem::Get().IsHandleActive(PreviewPlaybackHandle); }
	void SetPreviewPlaybackTime(float TimeSeconds);
	float GetPreviewPlaybackTime() const;
	float GetPreviewDuration() const;

	const FString& GetSoundPath() const { return SoundPath; }
	void SetSoundPath(const FString& InSoundPath) { SoundPath = InSoundPath; }
	bool IsSpatial() const { return bSpatial; }
	float GetMinDistance() const { return MinDistance; }
	float GetMaxDistance() const { return MaxDistance; }
	bool ShouldDrawAudioRange(bool bGlobalEnabled) const { return ResolveDebugDrawVisibility(AudioRangeVisibility, bGlobalEnabled); }

	void SetLooping(bool bInLoop) { bLoop = bInLoop; }
	void SetSpatial(bool bInSpatial) { bSpatial = bInSpatial; }
	void SetVolume(float InVolume) { Volume = InVolume; }

protected:
	void TickComponent(float DeltaTime) override;

private:
	FAudioPlayParams MakePlayParams() const;
	FVector GetAudioLocation() const;
	FVector GetListenerLocation() const;
	bool IsListenerOutsideMaxDistance() const;
	EAudioOutsideBehavior GetOutsideRangeBehavior() const;
	EAudioStartBehavior GetStartBehavior() const;
	bool ShouldAutoStart() const;
	bool IsPlaylistActive() const;
	bool HasPlayableSound() const;
	int32 GetPlaylistTrackCount() const;
	FString GetPlaylistTrackPath(int32 TrackIndex) const;
	FString GetCurrentPlaybackPath() const;
	FAudioHandle PlayCurrentTrack();

	FString SoundPath;
	bool bPlaylistEnabled = false;
	FString PlaylistPathA;
	FString PlaylistPathB;
	int32 StartBehavior = static_cast<int32>(EAudioStartBehavior::OnBeginPlay);
	bool bLoop = false;
	bool bSpatial = true;
	bool bAffectedByAudioZones = true;
	int32 AudioBus = static_cast<int32>(EAudioBus::SFX);
	int32 OutsideRangeBehavior = static_cast<int32>(EAudioOutsideBehavior::ContinuePlaying);
	float Volume = 1.0f;
	float MinDistance = 1.0f;
	float MaxDistance = 8.0f;
	int32 AudioRangeVisibility = static_cast<int32>(EDebugDrawVisibility::UseGlobal);
	bool bPausedByOutsideRange = false;
	bool bStoppedByOutsideRange = false;
	bool bStartedByStartBehavior = false;
	int32 PlaylistTrackIndex = 0;
	FAudioHandle PlaybackHandle;
	FAudioHandle PreviewPlaybackHandle;
};
