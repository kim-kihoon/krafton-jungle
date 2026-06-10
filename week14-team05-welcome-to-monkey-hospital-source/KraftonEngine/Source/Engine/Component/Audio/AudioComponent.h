#pragma once

#include "Component/SceneComponent.h"
#include "Math/Vector.h"

#include "Source/Engine/Component/Audio/AudioComponent.generated.h"

UCLASS()
class UAudioComponent : public USceneComponent
{
public:
	GENERATED_BODY()
	UAudioComponent() = default;
	~UAudioComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	UFUNCTION(Callable, Category="Audio")
	void Play();
	UFUNCTION(Callable, Category="Audio")
	void Stop();
	UFUNCTION(Pure, Category="Audio")
	bool IsPlaying() const { return bPlaying; }

	UFUNCTION(Callable, Category="Audio")
	void SetSoundPath(const FString& InSoundPath) { SoundPath = InSoundPath; bLoaded = false; }
	UFUNCTION(Pure, Category="Audio")
	FString GetSoundPath() const { return SoundPath; }

	UFUNCTION(Callable, Category="Audio")
	void SetVolume(float InVolume);
	UFUNCTION(Pure, Category="Audio")
	float GetVolume() const { return Volume; }
	UFUNCTION(Callable, Category="Audio")
	bool MuteForStartup();
	UFUNCTION(Callable, Category="Audio")
	bool RestoreStartupMute();

	void PlayOneShot(const FString& InSoundPath, float InVolume, float InPitch, bool bInSpatialize = true);

private:
	bool EnsureLoaded();
	float ComputeAttenuatedVolume() const;
	FVector ResolveListenerLocation() const;
	FString GetAudioKey() const;
	FString GetLoopName() const;
	FString GetOneShotAudioKey(const FString& InSoundPath, bool bInSpatialize) const;

private:
	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Sound Path", AssetType="Audio")
	FString SoundPath;

	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Auto Play")
	bool bAutoPlay = true;

	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Mute Until Start")
	bool bMuteUntilStart = false;

	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Loop")
	bool bLoop = true;

	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Spatialize")
	bool bSpatialize = true;

	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Volume", Min=0.0f, Max=10.0f, Speed=0.01f)
	float Volume = 1.0f;

	UPROPERTY(Edit, Save, Category="Audio", DisplayName="Pitch", Min=0.1f, Max=4.0f, Speed=0.01f)
	float Pitch = 1.0f;

	UPROPERTY(Edit, Save, Category="Audio|Attenuation", DisplayName="Min Distance", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float MinDistance = 1.0f;

	UPROPERTY(Edit, Save, Category="Audio|Attenuation", DisplayName="Max Distance", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float MaxDistance = 12.0f;

	UPROPERTY(Edit, Save, Category="Audio|Attenuation", DisplayName="Falloff Exponent", Min=0.1f, Max=8.0f, Speed=0.05f)
	float FalloffExponent = 1.0f;

	bool bLoaded = false;
	bool bPlaying = false;
	bool bStartupMuteActive = false;
	float StartupMuteRestoreVolume = 1.0f;
	TSet<FString> OneShotAudioKeys;
};
