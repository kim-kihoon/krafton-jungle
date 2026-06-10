#include "GameFramework/Actor/AudioVolume.h"

#include "Audio/AudioManager.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace
{
	float Clamp01(float Value)
	{
		return std::max(0.0f, std::min(1.0f, Value));
	}

	bool IsPointInsideVisualBox(const UBoxComponent* Box, const FVector& WorldPoint)
	{
		if (!Box)
		{
			return false;
		}

		const FVector Center = Box->GetWorldLocation();
		const FVector Extent = Box->GetScaledBoxExtent();
		const FQuat InvRotation = Box->GetWorldRotation().ToQuaternion().GetNormalized().Inverse();
		const FVector LocalPoint = InvRotation.RotateVector(WorldPoint - Center);
		constexpr float Tolerance = 0.05f;
		return std::abs(LocalPoint.X) <= Extent.X + Tolerance
			&& std::abs(LocalPoint.Y) <= Extent.Y + Tolerance
			&& std::abs(LocalPoint.Z) <= Extent.Z + Tolerance;
	}
}

void AAudioVolume::BeginPlay()
{
	Super::BeginPlay();
}

void AAudioVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	RefreshInitialPawnOccupancy();

	if (bPlaying)
	{
		FAudioManager::Get().SetLoopVolume(GetLoopName(), ComputeCurrentVolume());
		FAudioManager::Get().SetLoopPitch(GetLoopName(), Pitch);
	}
}

void AAudioVolume::EndPlay()
{
	StopVolumeAudio();
	if (bApplyZoneEffect)
	{
		FAudioManager::Get().ClearAudioZoneEffect(this);
	}
	Super::EndPlay();
}

void AAudioVolume::OnPossessedPawnEntered(APawn* /*Pawn*/)
{
	if (bAutoPlayWhileInside)
	{
		PlayVolumeAudio();
	}
	ApplyInsideZoneEffect();
}

void AAudioVolume::OnPossessedPawnExited(APawn* /*Pawn*/)
{
	if (GetOccupyingPawnCount() <= 0)
	{
		StopVolumeAudio();
		ApplyOutsideZoneEffect();
	}
}

void AAudioVolume::PlayVolumeAudio()
{
	if (SoundPath.empty() || !EnsureLoaded())
	{
		return;
	}

	FAudioManager::Get().PlayLoop(GetAudioKey(), GetLoopName(), ComputeCurrentVolume(), Pitch);
	bPlaying = true;
}

void AAudioVolume::StopVolumeAudio()
{
	if (bPlaying)
	{
		FAudioManager::Get().StopLoop(GetLoopName());
	}
	bPlaying = false;
}

bool AAudioVolume::EnsureLoaded()
{
	if (bLoaded)
	{
		return true;
	}

	if (SoundPath.empty())
	{
		return false;
	}

	bLoaded = FAudioManager::Get().LoadAudio(GetAudioKey(), SoundPath, true);
	if (!bLoaded)
	{
		UE_LOG("[AudioVolume] Failed to load audio. Actor=%s Path=%s",
			GetName().c_str(),
			SoundPath.c_str());
	}
	return bLoaded;
}

float AAudioVolume::ComputeCurrentVolume() const
{
	const float BaseVolume = Clamp01(Volume);
	if (FadeDistance <= 0.0f || GetOccupyingPawnCount() > 0)
	{
		return BaseVolume;
	}
	return 0.0f;
}

FAudioZoneEffectSettings AAudioVolume::MakeInsideZoneEffectSettings() const
{
	FAudioZoneEffectSettings Settings;
	Settings.bEnableLowPass = bEnableInsideLowPass;
	Settings.LowPassCutoffHz = InsideLowPassCutoffHz;
	Settings.bEnableReverb = bEnableInsideReverb;
	Settings.ReverbWetLevelDb = InsideReverbWetLevelDb;
	Settings.ReverbDecayTimeMs = InsideReverbDecayTimeMs;
	Settings.FadeTimeSeconds = ZoneEffectFadeTime;
	return Settings;
}

FAudioZoneEffectSettings AAudioVolume::MakeOutsideZoneEffectSettings() const
{
	FAudioZoneEffectSettings Settings;
	Settings.bEnableLowPass = bEnableOutsideLowPass;
	Settings.LowPassCutoffHz = OutsideLowPassCutoffHz;
	Settings.bEnableReverb = bEnableOutsideReverb;
	Settings.ReverbWetLevelDb = OutsideReverbWetLevelDb;
	Settings.ReverbDecayTimeMs = OutsideReverbDecayTimeMs;
	Settings.FadeTimeSeconds = ZoneEffectFadeTime;
	return Settings;
}

void AAudioVolume::ApplyInsideZoneEffect()
{
	if (!bApplyZoneEffect)
	{
		return;
	}

	bZoneEffectApplied = true;
	FAudioManager::Get().ApplyAudioZoneEffect(MakeInsideZoneEffectSettings(), this);
}

void AAudioVolume::ApplyOutsideZoneEffect()
{
	if (!bApplyZoneEffect)
	{
		return;
	}

	if (bApplyOutsideEffectOnExit)
	{
		bZoneEffectApplied = true;
		FAudioManager::Get().ApplyAudioZoneEffect(MakeOutsideZoneEffectSettings(), this);
	}
	else
	{
		bZoneEffectApplied = false;
		FAudioManager::Get().ClearAudioZoneEffect(this);
	}
}

void AAudioVolume::RefreshInitialPawnOccupancy()
{
	if (!bApplyZoneEffect)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController ? PlayerController->GetPossessedPawn() : nullptr;
	const bool bPawnPossessed = Pawn && Pawn->IsPossessed();
	const bool bInsideTrigger = bPawnPossessed && IsPawnInsideAudioVolume(Pawn);
	if (!Pawn || !bPawnPossessed)
	{
		return;
	}

	if (bInsideTrigger)
	{
		if (AddOccupyingPawn(Pawn))
		{
			OnPossessedPawnEntered(Pawn);
		}
	}
	else if (RemoveOccupyingPawn(Pawn))
	{
		OnPossessedPawnExited(Pawn);
	}
}

bool AAudioVolume::IsPawnInsideAudioVolume(APawn* Pawn) const
{
	UBoxComponent* Box = GetTriggerBox();
	if (!Pawn || !Box)
	{
		return false;
	}

	if (IsPointInsideVisualBox(Box, Pawn->GetActorLocation()))
	{
		return true;
	}

	for (UPrimitiveComponent* Primitive : Pawn->GetPrimitiveComponents())
	{
		if (!Primitive || Primitive == Box)
		{
			continue;
		}

		if (IsPointInsideVisualBox(Box, Primitive->GetWorldLocation()))
		{
			return true;
		}
	}

	return false;
}

FString AAudioVolume::GetAudioKey() const
{
	FString Key = "AudioVolume:";
	Key += SoundPath;
	return Key;
}

FString AAudioVolume::GetLoopName() const
{
	FString Name = "AudioVolumeLoop:";
	Name += GetName();
	Name += ":";
	Name += std::to_string(reinterpret_cast<std::uintptr_t>(this));
	return Name;
}
