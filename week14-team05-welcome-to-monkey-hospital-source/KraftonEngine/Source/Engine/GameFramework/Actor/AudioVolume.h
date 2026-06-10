#pragma once

#include "Audio/AudioManager.h"
#include "GameFramework/Actor/TriggerVolumeBase.h"

#include "Source/Engine/GameFramework/Actor/AudioVolume.generated.h"

UCLASS()
class AAudioVolume : public ATriggerVolumeBase
{
public:
	GENERATED_BODY()
	AAudioVolume() = default;
	~AAudioVolume() override = default;

	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void EndPlay() override;

	void OnPossessedPawnEntered(APawn* Pawn) override;
	void OnPossessedPawnExited(APawn* Pawn) override;

	UFUNCTION(Callable, Category="AudioVolume")
	void PlayVolumeAudio();
	UFUNCTION(Callable, Category="AudioVolume")
	void StopVolumeAudio();
	UFUNCTION(Pure, Category="AudioVolume")
	bool IsVolumeAudioPlaying() const { return bPlaying; }

private:
	bool EnsureLoaded();
	float ComputeCurrentVolume() const;
	FAudioZoneEffectSettings MakeInsideZoneEffectSettings() const;
	FAudioZoneEffectSettings MakeOutsideZoneEffectSettings() const;
	void ApplyInsideZoneEffect();
	void ApplyOutsideZoneEffect();
	void RefreshInitialPawnOccupancy();
	bool IsPawnInsideAudioVolume(APawn* Pawn) const;
	FString GetAudioKey() const;
	FString GetLoopName() const;

private:
	UPROPERTY(Edit, Save, Category="AudioVolume", DisplayName="Sound Path", AssetType="Audio")
	FString SoundPath;

	UPROPERTY(Edit, Save, Category="AudioVolume", DisplayName="Auto Play While Inside")
	bool bAutoPlayWhileInside = true;

	UPROPERTY(Edit, Save, Category="AudioVolume", DisplayName="Volume", Min=0.0f, Max=1.0f, Speed=0.01f)
	float Volume = 1.0f;

	UPROPERTY(Edit, Save, Category="AudioVolume", DisplayName="Pitch", Min=0.1f, Max=4.0f, Speed=0.01f)
	float Pitch = 1.0f;

	UPROPERTY(Edit, Save, Category="AudioVolume", DisplayName="Fade Distance", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float FadeDistance = 1.0f;

	UPROPERTY(Edit, Save, Category="AudioZone", DisplayName="Apply Zone Effect")
	bool bApplyZoneEffect = true;

	UPROPERTY(Edit, Save, Category="AudioZone", DisplayName="Effect Fade Time", Min=0.0f, Max=10.0f, Speed=0.05f)
	float ZoneEffectFadeTime = 0.35f;

	UPROPERTY(Edit, Save, Category="AudioZone|Inside", DisplayName="Enable Inside Low Pass")
	bool bEnableInsideLowPass = false;

	UPROPERTY(Edit, Save, Category="AudioZone|Inside", DisplayName="Inside Low Pass Cutoff Hz", Min=10.0f, Max=22000.0f, Speed=10.0f)
	float InsideLowPassCutoffHz = 22000.0f;

	UPROPERTY(Edit, Save, Category="AudioZone|Inside", DisplayName="Enable Inside Reverb")
	bool bEnableInsideReverb = true;

	UPROPERTY(Edit, Save, Category="AudioZone|Inside", DisplayName="Inside Reverb Wet dB", Min=-80.0f, Max=10.0f, Speed=0.5f)
	float InsideReverbWetLevelDb = -12.0f;

	UPROPERTY(Edit, Save, Category="AudioZone|Inside", DisplayName="Inside Reverb Decay ms", Min=100.0f, Max=20000.0f, Speed=50.0f)
	float InsideReverbDecayTimeMs = 1800.0f;

	UPROPERTY(Edit, Save, Category="AudioZone|Outside", DisplayName="Apply Outside Effect On Exit")
	bool bApplyOutsideEffectOnExit = false;

	UPROPERTY(Edit, Save, Category="AudioZone|Outside", DisplayName="Enable Outside Low Pass")
	bool bEnableOutsideLowPass = false;

	UPROPERTY(Edit, Save, Category="AudioZone|Outside", DisplayName="Outside Low Pass Cutoff Hz", Min=10.0f, Max=22000.0f, Speed=10.0f)
	float OutsideLowPassCutoffHz = 22000.0f;

	UPROPERTY(Edit, Save, Category="AudioZone|Outside", DisplayName="Enable Outside Reverb")
	bool bEnableOutsideReverb = false;

	UPROPERTY(Edit, Save, Category="AudioZone|Outside", DisplayName="Outside Reverb Wet dB", Min=-80.0f, Max=10.0f, Speed=0.5f)
	float OutsideReverbWetLevelDb = -80.0f;

	UPROPERTY(Edit, Save, Category="AudioZone|Outside", DisplayName="Outside Reverb Decay ms", Min=100.0f, Max=20000.0f, Speed=50.0f)
	float OutsideReverbDecayTimeMs = 1500.0f;

	bool bLoaded = false;
	bool bPlaying = false;
	bool bZoneEffectApplied = false;
};
