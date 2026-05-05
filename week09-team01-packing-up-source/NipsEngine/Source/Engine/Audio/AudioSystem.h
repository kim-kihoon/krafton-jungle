#pragma once

#include "Core/Containers/String.h"
#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Core/Singleton.h"

#include <memory>

struct FAudioHandle
{
	uint32 Id = 0;

	bool IsValid() const { return Id != 0; }
};

enum class EAudioBus : int32
{
	SFX = 0,
	Music,
	Ambient,
	Count
};

struct FAudioPlayParams
{
	bool bLoop = false;
	bool bSpatial = false;  
	bool bAffectedByAudioZones = true;
	bool bAllowVolumeBoost = false;
	EAudioBus Bus = EAudioBus::SFX;
	float Volume = 1.0f;
	float MinDistance = 1.0f;
	float MaxDistance = 8.0f;
	FVector Location = FVector::ZeroVector;
};

enum class EAudioOutsideBehavior : int32
{
	ContinuePlaying = 0,
	PauseAndResume,
	StopAndRestart,
	Count
};

enum class EAudioStartBehavior : int32
{
	OnBeginPlay = 0,
	OnFirstEnter,
	ManualOnly,
	Count
};

struct FAudioSystemImpl;

class FAudioSystem : public TSingleton<FAudioSystem>
{
	friend class TSingleton<FAudioSystem>;

public:
	bool Init();
	void Shutdown();
	void Tick(float DeltaTime);

	bool IsInitialized() const;

	FAudioHandle Play2D(const FString& SoundPath, float Volume = 1.0f, bool bLoop = false);
	FAudioHandle PlayAtLocation(const FString& SoundPath, const FVector& Location, float Volume = 1.0f,
		bool bLoop = false, float MinDistance = 1.0f, float MaxDistance = 8.0f);
	FAudioHandle Play(const FString& SoundPath, const FAudioPlayParams& Params);

	void Stop(FAudioHandle Handle);
	void Pause(FAudioHandle Handle);
	void Resume(FAudioHandle Handle);
	void Restart(FAudioHandle Handle);
	void StopAll();
	bool IsHandleActive(FAudioHandle Handle) const;
	bool IsPlaying(FAudioHandle Handle) const;
	bool IsAtEnd(FAudioHandle Handle) const;
	void SetVolume(FAudioHandle Handle, float Volume);
	void SetBusVolume(EAudioBus Bus, float Volume);
	float GetBusVolume(EAudioBus Bus) const;
	void SetLooping(FAudioHandle Handle, bool bLoop);
	void SetAffectedByAudioZones(FAudioHandle Handle, bool bAffected);

	void SetPlaybackTime(FAudioHandle Handle, float TimeSeconds);
	float GetPlaybackTime(FAudioHandle Handle) const;
	float GetDuration(FAudioHandle Handle) const;
	float GetSoundDuration(const FString& SoundPath) const;

	void SetSoundPosition(FAudioHandle Handle, const FVector& Location);
	void SetListenerTransform(const FVector& Location, const FVector& Forward, const FVector& Up);
	void SubmitZoneMix(uint32 ZoneId, int32 Priority,
		const FVector& Location, const FVector& Forward, const FVector& Right, const FVector& Up, const FVector& Extent,
		float InteriorMasterVolume, float InteriorSFXVolume, float InteriorMusicVolume, float InteriorAmbientVolume,
		float ExteriorMasterVolume, float ExteriorSFXVolume, float ExteriorMusicVolume, float ExteriorAmbientVolume,
		float InteriorLowPassCutoff, float ExteriorLowPassCutoff,
		float InteriorReverbWet, float InteriorReverbDecay,
		float ExteriorReverbWet, float ExteriorReverbDecay);
	void RemoveZoneMix(uint32 ZoneId);

private:      
	FAudioSystem();
	~FAudioSystem();

	std::unique_ptr<FAudioSystemImpl> Impl;
};
