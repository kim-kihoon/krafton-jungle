#pragma once

#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include <fmod.hpp>

class UWorld;

struct FAudio3DPlaySettings
{
	bool bEnabled = false;
	FVector Position = FVector::ZeroVector;
	float MinDistance = 1.0f;
	float MaxDistance = 500.0f;
};

struct FAudioZoneEffectSettings
{
	bool bEnableLowPass = false;
	float LowPassCutoffHz = 22000.0f;
	bool bEnableReverb = false;
	float ReverbWetLevelDb = -80.0f;
	float ReverbDecayTimeMs = 1500.0f;
	float FadeTimeSeconds = 0.25f;
};

// FMOD channel gain — 1.0 = unity, values above 1 amplify (may clip).
inline constexpr float AudioMaxChannelGain = 10.0f;

class FAudioManager : public TSingleton<FAudioManager>
{
	friend class TSingleton<FAudioManager>;

public:
	bool Initialize();
	void Shutdown();
	void Tick();

	bool LoadAudio(const FString& Key, const FString& Path, bool bLoop = false, bool b3D = false);
	void ReleaseAudio(const FString& Key);
	void PlayAudio(const FString& Key, float Volume = 1.0f, float Pitch = 1.0f, const FAudio3DPlaySettings* Settings3D = nullptr);
	void PlayAudioFadeOut(const FString& Key, float Volume = 1.0f, float FadeOutSeconds = 1.0f, float Pitch = 1.0f, const FAudio3DPlaySettings* Settings3D = nullptr);
	void PlayBGM(const FString& Key, float Volume = 1.0f);
	void StopBGM();
	void PlayLoop(const FString& Key, const FString& LoopName, float Volume = 1.0f, float Pitch = 1.0f, const FAudio3DPlaySettings* Settings3D = nullptr);
	void StopLoop(const FString& LoopName);
	void StopAllLoops();
	void SetLoopVolume(const FString& LoopName, float Volume);
	void SetLoopPitch(const FString& LoopName, float Pitch);
	void SetLoop3DAttributes(const FString& LoopName, const FVector& Position, float MinDistance, float MaxDistance);
	bool IsLoopPlaying(const FString& LoopName);

	void SetListener(const FVector& Position, const FVector& Forward, const FVector& Up);
	bool UpdateListenerFromWorld(UWorld* World);

	void SetMasterVolume(float Volume);
	void ApplyAudioZoneEffect(const FAudioZoneEffectSettings& Settings, const void* Source);
	void ClearAudioZoneEffect(const void* Source);

private:
	struct FLoopChannelEntry
	{
		FMOD::Channel* Channel = nullptr;
		bool b3D = false;
	};

	void LoadDefaultAudios();
	FMOD::Sound* FindSound(const FString& Key) const;
	FMOD::Channel* FindPlayingLoopChannel(const FString& LoopName, bool* bOut3D = nullptr);
	void Apply3DSettingsToChannel(FMOD::Channel* Channel, const FAudio3DPlaySettings& Settings3D) const;
	void EnsureZoneEffectDsps();
	void UpdateZoneEffect(float DeltaTime);
	static FMOD_VECTOR ToFmodVector(const FVector& Value);

	struct FAudioSoundEntry
	{
		FMOD::Sound* Sound = nullptr;
		int32 RefCount = 0;
		bool bLoop = false;
		bool b3D = false;
		FString Path;
	};

private:
	FAudioManager() = default;
	~FAudioManager() = default;

	FMOD::System* System = nullptr;
	FMOD::ChannelGroup* MasterGroup = nullptr;
	FMOD::Channel* BGMChannel = nullptr;
	FMOD::DSP* LowPassDsp = nullptr;
	FMOD::DSP* ReverbDsp = nullptr;

	FAudioZoneEffectSettings CurrentZoneEffect;
	FAudioZoneEffectSettings TargetZoneEffect;
	const void* ActiveZoneEffectSource = nullptr;

	TMap<FString, FAudioSoundEntry> Audios;
	TMap<FString, FLoopChannelEntry> LoopChannels;
};
