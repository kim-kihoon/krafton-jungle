#include "Audio/AudioSystem.h"

#include "Core/Logger.h"

#include <memory>
#include <cmath>
#include <algorithm>

#if __has_include(<miniaudio.h>)
	#define NIPS_WITH_MINIAUDIO 1
	#define MINIAUDIO_IMPLEMENTATION
	#pragma warning(push)
	#pragma warning(disable: 4244)
	#include <miniaudio.h>
	#pragma warning(pop)
	#include "Core/Paths.h"
	#include <algorithm>
	#include <filesystem>
	#include <unordered_map>
#else
	#define NIPS_WITH_MINIAUDIO 0
#endif

#if NIPS_WITH_MINIAUDIO
namespace
{
	float ClampVolume(float Value, bool bAllowBoost = false)
	{
		if (Value < 0.0f) return 0.0f;
		const float MaxVolume = bAllowBoost ? 10.0f : 2.0f;
		if (Value > MaxVolume) return MaxVolume;
		return Value;
	}

	int32 ToBusIndex(EAudioBus Bus)
	{
		const int32 Index = static_cast<int32>(Bus);
		if (Index < 0 || Index >= static_cast<int32>(EAudioBus::Count))
		{
			return static_cast<int32>(EAudioBus::SFX);
		}
		return Index;
	}

	float Clamp01(float Value)
	{
		if (Value < 0.0f) return 0.0f;
		if (Value > 1.0f) return 1.0f;
		return Value;
	}

	float ClampLowPassCutoff(float Value)
	{
		if (Value < 100.0f) return 100.0f;
		if (Value > 20000.0f) return 20000.0f;
		return Value;
	}

	constexpr float ZoneReverbBypassWetThreshold = 0.001f;

	FVector ToAudioVector(const FVector& WorldVector)
	{
		return FVector(WorldVector.X, -WorldVector.Y, WorldVector.Z);
	}

	std::wstring ResolveAudioPath(const FString& SoundPath)
	{
		if (SoundPath.empty())
		{
			return {};
		}

		std::filesystem::path Path(FPaths::ToWide(SoundPath));
		if (Path.is_absolute())
		{
			return Path.lexically_normal().generic_wstring();
		}

		return FPaths::ToAbsolute(FPaths::ToWide(SoundPath));
	}
}

struct FAudioSystemImpl
{
	struct FActiveSound
	{
		std::unique_ptr<ma_sound> Sound;
		std::wstring ResolvedSoundPath;
		bool bLoop = false;
		bool bSpatial = false;
		bool bAffectedByAudioZones = true;
		bool bAllowVolumeBoost = false;
		bool bUsingZoneEffectBus = false;
		EAudioBus Bus = EAudioBus::SFX;
		float BaseVolume = 1.0f;
		float MinDistance = 1.0f;
		float MaxDistance = 8.0f;
		FVector Location = FVector::ZeroVector;
	};

	struct FZoneMix
	{
		int32 Priority = 0;
		float Weight = 0.0f;
		FVector Location = FVector::ZeroVector;
		FVector Forward = FVector(1.0f, 0.0f, 0.0f);
		FVector Right = FVector(0.0f, 1.0f, 0.0f);
		FVector Up = FVector(0.0f, 0.0f, 1.0f);
		FVector Extent = FVector(1.0f, 1.0f, 1.0f);
		float InteriorMasterVolume = 1.0f;
		float InteriorSFXVolume = 1.0f;
		float InteriorMusicVolume = 1.0f;
		float InteriorAmbientVolume = 1.0f;
		float ExteriorMasterVolume = 1.0f;
		float ExteriorSFXVolume = 1.0f;
		float ExteriorMusicVolume = 1.0f;
		float ExteriorAmbientVolume = 1.0f;
		float InteriorLowPassCutoff = 20000.0f;
		float ExteriorLowPassCutoff = 20000.0f;
		float InteriorReverbWet = 0.0f;
		float InteriorReverbDecay = 0.35f;
		float ExteriorReverbWet = 0.0f;
		float ExteriorReverbDecay = 0.35f;
	};

	ma_engine Engine{};
	bool bInitialized = false;
	bool bZoneEffectBusReady = false;
	uint32 NextHandleId = 1;
	std::unique_ptr<ma_sound_group> ZoneEffectGroup;
	std::unique_ptr<ma_lpf_node> ZoneLowPassNode;
	std::unique_ptr<ma_delay_node> ZoneReverbNode;
	float CurrentZoneLowPassCutoff = 20000.0f;
	float CurrentZoneReverbWet = 0.0f;
	float CurrentZoneReverbDecay = 0.35f;
	bool bZoneReverbBypassed = true;
	ma_uint32 EffectChannels = 2;
	ma_uint32 EffectSampleRate = 48000;
	std::unordered_map<uint32, FActiveSound> ActiveSounds;
	std::unordered_map<uint32, FZoneMix> ZoneMixes;
	float BusVolumes[static_cast<int32>(EAudioBus::Count)] = { 1.0f, 1.0f, 1.0f };
	FVector ListenerLocation = FVector::ZeroVector;
	uint32 LastListenerZoneId = 0;

	bool IsPointInsideZone(const FVector& Point, const FZoneMix& Zone) const
	{
		const FVector Delta = Point - Zone.Location;
		const float LocalX = FVector::DotProduct(Delta, Zone.Forward);
		const float LocalY = FVector::DotProduct(Delta, Zone.Right);
		const float LocalZ = FVector::DotProduct(Delta, Zone.Up);

		return std::abs(LocalX) <= Zone.Extent.X
			&& std::abs(LocalY) <= Zone.Extent.Y
			&& std::abs(LocalZ) <= Zone.Extent.Z;
	}

	float GetDistanceSqToZone(const FVector& Point, const FZoneMix& Zone) const
	{
		const FVector Delta = Point - Zone.Location;
		const float LocalX = FVector::DotProduct(Delta, Zone.Forward);
		const float LocalY = FVector::DotProduct(Delta, Zone.Right);
		const float LocalZ = FVector::DotProduct(Delta, Zone.Up);
		const float OutsideX = std::max(std::abs(LocalX) - Zone.Extent.X, 0.0f);
		const float OutsideY = std::max(std::abs(LocalY) - Zone.Extent.Y, 0.0f);
		const float OutsideZ = std::max(std::abs(LocalZ) - Zone.Extent.Z, 0.0f);
		return OutsideX * OutsideX + OutsideY * OutsideY + OutsideZ * OutsideZ;
	}

	const FZoneMix* FindBestListenerZone() const
	{
		const FZoneMix* BestZone = nullptr;
		for (const auto& Pair : ZoneMixes)
		{
			const FZoneMix& Zone = Pair.second;
			if (Zone.Weight <= 0.0f)
			{
				continue;  
			}

			if (!BestZone ||
				Zone.Priority > BestZone->Priority ||
				(Zone.Priority == BestZone->Priority && Zone.Weight > BestZone->Weight))
			{
				BestZone = &Zone;
			}
		}
		return BestZone;
	}

	uint32 FindBestContainingListenerZoneId() const
	{
		uint32 BestZoneId = 0;
		const FZoneMix* BestZone = nullptr;
		for (const auto& Pair : ZoneMixes)
		{
			const FZoneMix& Zone = Pair.second;
			if (!IsPointInsideZone(ListenerLocation, Zone))
			{
				continue;
			}

			if (!BestZone ||
				Zone.Priority > BestZone->Priority ||
				(Zone.Priority == BestZone->Priority && Pair.first == LastListenerZoneId))
			{
				BestZoneId = Pair.first;
				BestZone = &Zone;
			}
		}
		return BestZoneId;
	}

	uint32 FindBestExteriorListenerZoneId() const
	{
		uint32 BestZoneId = 0;
		const FZoneMix* BestZone = nullptr;
		float BestDistanceSq = 0.0f;
		for (const auto& Pair : ZoneMixes)
		{
			const FZoneMix& Zone = Pair.second;
			const float DistanceSq = GetDistanceSqToZone(ListenerLocation, Zone);
			if (!BestZone ||
				Zone.Priority > BestZone->Priority ||
				(Zone.Priority == BestZone->Priority && DistanceSq < BestDistanceSq))
			{
				BestZoneId = Pair.first;
				BestZone = &Zone;
				BestDistanceSq = DistanceSq;
			}
		}
		return BestZoneId;
	}

	bool IsListenerInsideAnyZone() const
	{
		for (const auto& Pair : ZoneMixes)
		{
			if (IsPointInsideZone(ListenerLocation, Pair.second))
			{
				return true;
			}
		}
		return false;
	}

	bool ShouldUseZoneEffectBus(const FActiveSound& ActiveSound) const
	{
		return ActiveSound.bAffectedByAudioZones
			&& bZoneEffectBusReady
			&& ZoneEffectGroup
			&& FindBestListenerZone() != nullptr;
	}

	float GetZoneBusVolume(const FZoneMix& Zone, EAudioBus Bus, bool bSourceInside) const
	{
		if (bSourceInside)
		{
			switch (Bus)
			{
			case EAudioBus::Music:
				return Zone.InteriorMusicVolume;
			case EAudioBus::Ambient:
				return Zone.InteriorAmbientVolume;
			case EAudioBus::SFX:
			default:
				return Zone.InteriorSFXVolume;
			}
		}

		switch (Bus)
		{
		case EAudioBus::Music:
			return Zone.ExteriorMusicVolume;
		case EAudioBus::Ambient:
			return Zone.ExteriorAmbientVolume;
		case EAudioBus::SFX:
		default:
			return Zone.ExteriorSFXVolume;
		}
	}

	float GetEffectiveVolume(const FActiveSound& ActiveSound) const
	{
		float Multiplier = 1.0f;
		const FZoneMix* BestZone = ActiveSound.bAffectedByAudioZones ? FindBestListenerZone() : nullptr;
		if (BestZone)
		{
			const bool bListenerInside = IsPointInsideZone(ListenerLocation, *BestZone);
			const float Master = bListenerInside ? BestZone->InteriorMasterVolume : BestZone->ExteriorMasterVolume;
			const float BusVolume = GetZoneBusVolume(*BestZone, ActiveSound.Bus, bListenerInside);
			const float Weight = std::clamp(BestZone->Weight, 0.0f, 1.0f);
			Multiplier = 1.0f + ((Master * BusVolume) - 1.0f) * Weight;
		}

		const float BusVolume = BusVolumes[ToBusIndex(ActiveSound.Bus)];
		return ClampVolume(ActiveSound.BaseVolume * BusVolume * Multiplier, ActiveSound.bAllowVolumeBoost);
	}

	float GetEffectiveLowPassCutoff(const FActiveSound& ActiveSound) const
	{
		const FZoneMix* BestZone = ActiveSound.bAffectedByAudioZones ? FindBestListenerZone() : nullptr;
		if (!BestZone)
		{
			return 20000.0f;
		}

		const bool bListenerInside = IsPointInsideZone(ListenerLocation, *BestZone);
		const float TargetCutoff = bListenerInside ? BestZone->InteriorLowPassCutoff : BestZone->ExteriorLowPassCutoff;
		const float Weight = std::clamp(BestZone->Weight, 0.0f, 1.0f);
		return ClampLowPassCutoff(20000.0f + (TargetCutoff - 20000.0f) * Weight);
	}

	void GetEffectiveReverb(const FActiveSound& ActiveSound, float& OutWet, float& OutDecay) const
	{
		OutWet = 0.0f;
		OutDecay = 0.35f;

		const FZoneMix* BestZone = ActiveSound.bAffectedByAudioZones ? FindBestListenerZone() : nullptr;
		if (!BestZone)
		{
			return;
		}

		const bool bListenerInside = IsPointInsideZone(ListenerLocation, *BestZone);
		const float TargetWet = bListenerInside ? BestZone->InteriorReverbWet : BestZone->ExteriorReverbWet;
		const float TargetDecay = bListenerInside ? BestZone->InteriorReverbDecay : BestZone->ExteriorReverbDecay;
		const float Weight = std::clamp(BestZone->Weight, 0.0f, 1.0f);
		OutWet = Clamp01(TargetWet * Weight);
		OutDecay = Clamp01(TargetDecay);
	}

	float GetZoneEffectLowPassCutoff() const
	{
		const FZoneMix* BestZone = FindBestListenerZone();
		if (!BestZone)
		{
			return 20000.0f;
		}

		const bool bListenerInside = IsPointInsideZone(ListenerLocation, *BestZone);
		const float TargetCutoff = bListenerInside ? BestZone->InteriorLowPassCutoff : BestZone->ExteriorLowPassCutoff;
		const float Weight = std::clamp(BestZone->Weight, 0.0f, 1.0f);
		return ClampLowPassCutoff(20000.0f + (TargetCutoff - 20000.0f) * Weight);
	}

	void GetZoneEffectReverb(float& OutWet, float& OutDecay) const
	{
		OutWet = 0.0f;
		OutDecay = 0.35f;

		const FZoneMix* BestZone = FindBestListenerZone();
		if (!BestZone)
		{
			return;
		}

		const bool bListenerInside = IsPointInsideZone(ListenerLocation, *BestZone);
		const float TargetWet = bListenerInside ? BestZone->InteriorReverbWet : BestZone->ExteriorReverbWet;
		const float TargetDecay = bListenerInside ? BestZone->InteriorReverbDecay : BestZone->ExteriorReverbDecay;
		const float Weight = std::clamp(BestZone->Weight, 0.0f, 1.0f);
		OutWet = Clamp01(TargetWet * Weight);
		OutDecay = Clamp01(TargetDecay);
	}

	bool SetZoneReverbBypassed(bool bBypass)
	{
		if (!ZoneLowPassNode || !ZoneReverbNode)
		{
			return false;
		}

		ma_node* TargetNode = bBypass ? ma_engine_get_endpoint(&Engine) : reinterpret_cast<ma_node*>(ZoneReverbNode.get());
		const ma_result Result = ma_node_attach_output_bus(ZoneLowPassNode.get(), 0, TargetNode, 0);
		if (Result != MA_SUCCESS)
		{
			UE_LOG("AudioSystem: failed to route zone reverb bypass. error=%d", static_cast<int>(Result));
			return false;
		}

		bZoneReverbBypassed = bBypass;
		return true;
	}

	void ApplyVolume(FActiveSound& ActiveSound)
	{
		if (ActiveSound.Sound)
		{
			ma_sound_set_volume(ActiveSound.Sound.get(), GetEffectiveVolume(ActiveSound));
		}
	}

	void ApplyEffects(FActiveSound& ActiveSound)
	{
		(void)ActiveSound;
	}

	void ApplyZoneEffectSettings()
	{
		if (!bZoneEffectBusReady || !ZoneLowPassNode || !ZoneReverbNode)
		{
			return;
		}

		const float MaxCutoff = std::max(100.0f, static_cast<float>(EffectSampleRate) * 0.45f);
		const float Cutoff = std::min(GetZoneEffectLowPassCutoff(), MaxCutoff);
		if (std::fabs(CurrentZoneLowPassCutoff - Cutoff) > 10.0f)
		{
			const ma_lpf_config Config = ma_lpf_config_init(ma_format_f32, EffectChannels, EffectSampleRate, Cutoff, 2);
			if (ma_lpf_node_reinit(&Config, ZoneLowPassNode.get()) == MA_SUCCESS)
			{
				CurrentZoneLowPassCutoff = Cutoff;
			}
		}

		float ReverbWet = 0.0f;
		float ReverbDecay = 0.35f;
		GetZoneEffectReverb(ReverbWet, ReverbDecay);
		const bool bShouldBypassReverb = ReverbWet <= ZoneReverbBypassWetThreshold;
		if (bZoneReverbBypassed != bShouldBypassReverb)
		{
			SetZoneReverbBypassed(bShouldBypassReverb);
		}

		if (std::fabs(CurrentZoneReverbWet - ReverbWet) > 0.001f)
		{
			ma_delay_node_set_wet(ZoneReverbNode.get(), ReverbWet);
			ma_delay_node_set_dry(ZoneReverbNode.get(), 1.0f + (ReverbWet * 0.25f));
			CurrentZoneReverbWet = ReverbWet;
		}
		if (std::fabs(CurrentZoneReverbDecay - ReverbDecay) > 0.001f)
		{
			ma_delay_node_set_decay(ZoneReverbNode.get(), ReverbDecay);
			CurrentZoneReverbDecay = ReverbDecay;
		}
	}

	void ApplySoundSettings(FActiveSound& ActiveSound)
	{
		ApplyVolume(ActiveSound);
		ApplyEffects(ActiveSound);
	}

	void ApplyVolumes()
	{
		ApplyZoneEffectSettings();
		UpdateSoundRoutes();
		for (auto& Pair : ActiveSounds)
		{
			ApplySoundSettings(Pair.second);
		}
	}

	bool InitSoundInstance(FActiveSound& ActiveSound, bool bUseZoneEffectBus, const FString& LogPath)
	{
		ma_sound_group* TargetGroup = bUseZoneEffectBus ? ZoneEffectGroup.get() : nullptr;
		auto NewSound = std::make_unique<ma_sound>();
		ma_result Result = ma_sound_init_from_file_w(&Engine, ActiveSound.ResolvedSoundPath.c_str(), 0, TargetGroup, nullptr, NewSound.get());
		if (Result != MA_SUCCESS && TargetGroup)
		{
			UE_LOG("AudioSystem: failed to load sound through zone effect bus '%s'. Retrying without effects. error=%d", LogPath.c_str(), static_cast<int>(Result));
			TargetGroup = nullptr;
			NewSound = std::make_unique<ma_sound>();
			Result = ma_sound_init_from_file_w(&Engine, ActiveSound.ResolvedSoundPath.c_str(), 0, nullptr, nullptr, NewSound.get());
		}
		if (Result != MA_SUCCESS)
		{
			UE_LOG("AudioSystem: failed to load sound '%s'. error=%d", LogPath.c_str(), static_cast<int>(Result));
			return false;
		}

		ma_sound_set_looping(NewSound.get(), ActiveSound.bLoop ? MA_TRUE : MA_FALSE);
		ma_sound_set_spatialization_enabled(NewSound.get(), ActiveSound.bSpatial ? MA_TRUE : MA_FALSE);

		if (ActiveSound.bSpatial)
		{
			const float SafeMinDistance = std::max(0.01f, ActiveSound.MinDistance);
			const float SafeMaxDistance = std::max(SafeMinDistance, ActiveSound.MaxDistance);
			const FVector AudioLocation = ToAudioVector(ActiveSound.Location);
			ma_sound_set_position(NewSound.get(), AudioLocation.X, AudioLocation.Y, AudioLocation.Z);
			ma_sound_set_attenuation_model(NewSound.get(), ma_attenuation_model_linear);
			ma_sound_set_min_distance(NewSound.get(), SafeMinDistance);
			ma_sound_set_max_distance(NewSound.get(), SafeMaxDistance);
		}

		ActiveSound.Sound = std::move(NewSound);
		ActiveSound.bUsingZoneEffectBus = TargetGroup != nullptr;
		return true;
	}

	void UpdateSoundRoutes()
	{
		for (auto& Pair : ActiveSounds)
		{
			FActiveSound& ActiveSound = Pair.second;
			if (!ActiveSound.Sound || ActiveSound.ResolvedSoundPath.empty())
			{
				continue;
			}

			const bool bShouldUseZoneEffectBus = ShouldUseZoneEffectBus(ActiveSound);
			if (ActiveSound.bUsingZoneEffectBus == bShouldUseZoneEffectBus)
			{
				continue;
			}

			float CursorSeconds = 0.0f;
			ma_sound_get_cursor_in_seconds(ActiveSound.Sound.get(), &CursorSeconds);
			const bool bWasPlaying = ma_sound_is_playing(ActiveSound.Sound.get()) == MA_TRUE;
			const FString LogPath = FPaths::ToUtf8(ActiveSound.ResolvedSoundPath);

			ma_sound_stop(ActiveSound.Sound.get());
			ma_sound_uninit(ActiveSound.Sound.get());
			ActiveSound.Sound.reset();

			if (!InitSoundInstance(ActiveSound, bShouldUseZoneEffectBus, LogPath))
			{
				continue;
			}

			if (CursorSeconds > 0.0f)
			{
				ma_sound_seek_to_second(ActiveSound.Sound.get(), CursorSeconds);
			}
			ApplySoundSettings(ActiveSound);
			if (bWasPlaying)
			{
				ma_sound_start(ActiveSound.Sound.get());
			}
		}
	}

	void UninitActiveSound(FActiveSound& ActiveSound)
	{
		if (ActiveSound.Sound)
		{
			ma_sound_stop(ActiveSound.Sound.get());
			ma_sound_uninit(ActiveSound.Sound.get());
			ActiveSound.Sound.reset();
		}
	}

	void ShutdownZoneEffectBus()
	{
		bZoneEffectBusReady = false;
		if (ZoneEffectGroup)
		{
			ma_sound_group_stop(ZoneEffectGroup.get());
			ma_sound_group_uninit(ZoneEffectGroup.get());
			ZoneEffectGroup.reset();
		}
		if (ZoneLowPassNode)
		{
			ma_node_detach_output_bus(ZoneLowPassNode.get(), 0);
			ma_lpf_node_uninit(ZoneLowPassNode.get(), nullptr);
			ZoneLowPassNode.reset();
		}
		if (ZoneReverbNode)
		{
			ma_node_detach_output_bus(ZoneReverbNode.get(), 0);
			ma_delay_node_uninit(ZoneReverbNode.get(), nullptr);
			ZoneReverbNode.reset();
		}
	}

	bool InitZoneEffectBus()
	{
		ShutdownZoneEffectBus();

		EffectChannels = std::max<ma_uint32>(1, ma_engine_get_channels(&Engine));
		EffectSampleRate = std::max<ma_uint32>(1, ma_engine_get_sample_rate(&Engine));
		ZoneLowPassNode = std::make_unique<ma_lpf_node>();
		ZoneReverbNode = std::make_unique<ma_delay_node>();
		ZoneEffectGroup = std::make_unique<ma_sound_group>();

		ma_node_graph* NodeGraph = ma_engine_get_node_graph(&Engine);
		ma_lpf_node_config LowPassConfig = ma_lpf_node_config_init(EffectChannels, EffectSampleRate, 20000.0, 2);
		ma_result Result = ma_lpf_node_init(NodeGraph, &LowPassConfig, nullptr, ZoneLowPassNode.get());
		if (Result != MA_SUCCESS)
		{
			UE_LOG("AudioSystem: failed to initialize zone low pass node. Audio zone effects disabled. error=%d", static_cast<int>(Result));
			ShutdownZoneEffectBus();
			return false;
		}

		const ma_uint32 DelayFrames = std::max<ma_uint32>(1, static_cast<ma_uint32>(static_cast<double>(EffectSampleRate) * 0.085));
		ma_delay_node_config ReverbConfig = ma_delay_node_config_init(EffectChannels, EffectSampleRate, DelayFrames, 0.35f);
		Result = ma_delay_node_init(NodeGraph, &ReverbConfig, nullptr, ZoneReverbNode.get());
		if (Result != MA_SUCCESS)
		{
			UE_LOG("AudioSystem: failed to initialize zone reverb node. Audio zone effects disabled. error=%d", static_cast<int>(Result));
			ShutdownZoneEffectBus();
			return false;
		}

		ma_delay_node_set_wet(ZoneReverbNode.get(), 0.0f);
		ma_delay_node_set_dry(ZoneReverbNode.get(), 1.0f);
		Result = ma_node_attach_output_bus(ZoneLowPassNode.get(), 0, ma_engine_get_endpoint(&Engine), 0);
		if (Result != MA_SUCCESS)
		{
			UE_LOG("AudioSystem: failed to route zone low pass node. Audio zone effects disabled. error=%d", static_cast<int>(Result));
			ShutdownZoneEffectBus();
			return false;
		}

		Result = ma_node_attach_output_bus(ZoneReverbNode.get(), 0, ma_engine_get_endpoint(&Engine), 0);
		if (Result != MA_SUCCESS)
		{
			UE_LOG("AudioSystem: failed to route zone reverb node. Audio zone effects disabled. error=%d", static_cast<int>(Result));
			ShutdownZoneEffectBus();
			return false;
		}

		ma_sound_group_config GroupConfig = ma_sound_group_config_init_2(&Engine);
		GroupConfig.pInitialAttachment = ZoneLowPassNode.get();
		GroupConfig.initialAttachmentInputBusIndex = 0;
		GroupConfig.channelsOut = EffectChannels;
		Result = ma_sound_group_init_ex(&Engine, &GroupConfig, ZoneEffectGroup.get());
		if (Result != MA_SUCCESS)
		{
			UE_LOG("AudioSystem: failed to initialize zone effect group. Audio zone effects disabled. error=%d", static_cast<int>(Result));
			ShutdownZoneEffectBus();
			return false;
		}

		Result = ma_sound_group_start(ZoneEffectGroup.get());
		if (Result != MA_SUCCESS)
		{
			UE_LOG("AudioSystem: failed to start zone effect group. Audio zone effects disabled. error=%d", static_cast<int>(Result));
			ShutdownZoneEffectBus();
			return false;
		}

		bZoneEffectBusReady = true;
		CurrentZoneLowPassCutoff = 20000.0f;
		CurrentZoneReverbWet = 0.0f;
		CurrentZoneReverbDecay = 0.35f;
		bZoneReverbBypassed = true;
		return true;
	}

	void UpdateZoneWeights(float DeltaTime)
	{
		(void)DeltaTime;

		const uint32 ContainingZoneId = FindBestContainingListenerZoneId();
		if (ContainingZoneId != 0)
		{
			LastListenerZoneId = ContainingZoneId;
		}
		else
		{
			if (LastListenerZoneId != 0 && ZoneMixes.find(LastListenerZoneId) == ZoneMixes.end())
			{
				LastListenerZoneId = 0;
			}
			if (LastListenerZoneId == 0)
			{
				LastListenerZoneId = FindBestExteriorListenerZoneId();
			}
		}

		for (auto& Pair : ZoneMixes)
		{
			FZoneMix& Zone = Pair.second;
			Zone.Weight = Pair.first == LastListenerZoneId ? 1.0f : 0.0f;
		}
	}
};
#else
struct FAudioSystemImpl
{
	bool bLoggedDisabled = false;
	float BusVolumes[static_cast<int32>(EAudioBus::Count)] = { 1.0f, 1.0f, 1.0f };
};
#endif

FAudioSystem::FAudioSystem()
	: Impl(std::make_unique<FAudioSystemImpl>())
{
}

FAudioSystem::~FAudioSystem()
{
	Shutdown();
}

bool FAudioSystem::Init()
{
#if NIPS_WITH_MINIAUDIO
	if (Impl->bInitialized)
	{
		return true;
	}

	ma_engine_config Config = ma_engine_config_init();
	const ma_result Result = ma_engine_init(&Config, &Impl->Engine);
	if (Result != MA_SUCCESS)
	{
		UE_LOG("AudioSystem: failed to initialize miniaudio engine. error=%d", static_cast<int>(Result));
		return false;
	}

	Impl->bInitialized = true;
	Impl->InitZoneEffectBus();
	UE_LOG("AudioSystem: initialized.");
	return true;
#else
	if (!Impl->bLoggedDisabled)
	{
		Impl->bLoggedDisabled = true;
		UE_LOG("AudioSystem: miniaudio.h was not found. Run vcpkg install to enable audio.");
	}
	return false;
#endif
}

void FAudioSystem::Shutdown()
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl || !Impl->bInitialized)
	{
		return;
	}

	StopAll();
	Impl->ShutdownZoneEffectBus();
	Impl->ZoneMixes.clear();
	Impl->LastListenerZoneId = 0;
	ma_engine_uninit(&Impl->Engine);
	Impl->bInitialized = false;
	UE_LOG("AudioSystem: shutdown.");
#endif
}

void FAudioSystem::Tick(float DeltaTime)
{
	(void)DeltaTime;

#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized)
	{
		return;
	}

	Impl->UpdateZoneWeights(DeltaTime);

	for (auto It = Impl->ActiveSounds.begin(); It != Impl->ActiveSounds.end();)
	{
		ma_sound* Sound = It->second.Sound.get();
		if (!Sound || (!It->second.bLoop && ma_sound_at_end(Sound)))
		{
			Impl->UninitActiveSound(It->second);
			It = Impl->ActiveSounds.erase(It);
			continue;
		}

		++It;
	}
	Impl->ApplyVolumes();
#endif
}

bool FAudioSystem::IsInitialized() const
{
#if NIPS_WITH_MINIAUDIO
	return Impl && Impl->bInitialized;
#else
	return false;
#endif
}

FAudioHandle FAudioSystem::Play2D(const FString& SoundPath, float Volume, bool bLoop)
{
	FAudioPlayParams Params;
	Params.bSpatial = false;
	Params.bLoop = bLoop;
	Params.bAffectedByAudioZones = false;
	Params.Volume = Volume;
	return Play(SoundPath, Params);
}

FAudioHandle FAudioSystem::PlayAtLocation(const FString& SoundPath, const FVector& Location, float Volume,
	bool bLoop, float MinDistance, float MaxDistance)
{
	FAudioPlayParams Params;
	Params.bSpatial = true;
	Params.bLoop = bLoop;
	Params.Volume = Volume;
	Params.Location = Location;
	Params.MinDistance = MinDistance;
	Params.MaxDistance = MaxDistance;
	return Play(SoundPath, Params);
}

FAudioHandle FAudioSystem::Play(const FString& SoundPath, const FAudioPlayParams& Params)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized && !Init())
	{
		return {};
	}

	const std::wstring AbsolutePath = ResolveAudioPath(SoundPath);
	if (AbsolutePath.empty())
	{
		UE_LOG("AudioSystem: empty sound path.");
		return {};
	}

	if (!std::filesystem::exists(std::filesystem::path(AbsolutePath)))
	{
		UE_LOG("AudioSystem: sound file not found: %s", SoundPath.c_str());
		return {};
	}

	const float BaseVolume = ClampVolume(Params.Volume, Params.bAllowVolumeBoost);
	FAudioSystemImpl::FActiveSound ActiveSound;
	ActiveSound.ResolvedSoundPath = AbsolutePath;
	ActiveSound.bLoop = Params.bLoop;
	ActiveSound.bSpatial = Params.bSpatial;
	ActiveSound.bAffectedByAudioZones = Params.bAffectedByAudioZones;
	ActiveSound.bAllowVolumeBoost = Params.bAllowVolumeBoost;
	ActiveSound.Bus = Params.Bus;
	ActiveSound.BaseVolume = BaseVolume;
	ActiveSound.MinDistance = Params.MinDistance;
	ActiveSound.MaxDistance = Params.MaxDistance;
	ActiveSound.Location = Params.Location;

	if (!Impl->InitSoundInstance(ActiveSound, Impl->ShouldUseZoneEffectBus(ActiveSound), SoundPath))
	{
		return {};
	}

	Impl->ApplySoundSettings(ActiveSound);

	ma_result Result = ma_sound_start(ActiveSound.Sound.get());
	if (Result != MA_SUCCESS)
	{
		UE_LOG("AudioSystem: failed to start sound '%s'. error=%d", SoundPath.c_str(), static_cast<int>(Result));
		Impl->UninitActiveSound(ActiveSound);
		return {};
	}

	FAudioHandle Handle;
	Handle.Id = Impl->NextHandleId++;
	if (Impl->NextHandleId == 0)
	{
		Impl->NextHandleId = 1;
	}

	Impl->ActiveSounds[Handle.Id] = std::move(ActiveSound);
	return Handle;
#else
	(void)SoundPath;
	(void)Params;
	Init();
	return {};
#endif
}

void FAudioSystem::Stop(FAudioHandle Handle)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end())
	{
		return;
	}

	Impl->UninitActiveSound(It->second);
	Impl->ActiveSounds.erase(It);
#else
	(void)Handle;
#endif
}

void FAudioSystem::Pause(FAudioHandle Handle)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	ma_sound_stop(It->second.Sound.get());
#else
	(void)Handle;
#endif
}

void FAudioSystem::Resume(FAudioHandle Handle)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	ma_sound_start(It->second.Sound.get());
#else
	(void)Handle;
#endif
}

void FAudioSystem::Restart(FAudioHandle Handle)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	ma_sound_seek_to_pcm_frame(It->second.Sound.get(), 0);
	ma_sound_start(It->second.Sound.get());
#else
	(void)Handle;
#endif
}

void FAudioSystem::StopAll()
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl)
	{
		return;
	}

	for (auto& Pair : Impl->ActiveSounds)
	{
		Impl->UninitActiveSound(Pair.second);
	}

	Impl->ActiveSounds.clear();
#endif
}

bool FAudioSystem::IsPlaying(FAudioHandle Handle) const
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return false;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return false;
	}

	return ma_sound_is_playing(It->second.Sound.get()) == MA_TRUE;
#else
	(void)Handle;
	return false;
#endif
}

bool FAudioSystem::IsHandleActive(FAudioHandle Handle) const
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return false;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	return It != Impl->ActiveSounds.end() && It->second.Sound != nullptr;
#else
	(void)Handle;
	return false;
#endif
}

bool FAudioSystem::IsAtEnd(FAudioHandle Handle) const
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return false;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return false;
	}

	return ma_sound_at_end(It->second.Sound.get()) == MA_TRUE;
#else
	(void)Handle;
	return false;
#endif
}

void FAudioSystem::SetVolume(FAudioHandle Handle, float Volume)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	It->second.BaseVolume = ClampVolume(Volume);
	Impl->ApplySoundSettings(It->second);
#else
	(void)Handle;
	(void)Volume;
#endif
}

void FAudioSystem::SetBusVolume(EAudioBus Bus, float Volume)
{
	const int32 BusIndex = static_cast<int32>(Bus);
	if (BusIndex < 0 || BusIndex >= static_cast<int32>(EAudioBus::Count))
	{
		return;
	}

#if NIPS_WITH_MINIAUDIO
	Impl->BusVolumes[BusIndex] = ClampVolume(Volume);
	if (Impl->bInitialized)
	{
		Impl->ApplyVolumes();
	}
#else
	Impl->BusVolumes[BusIndex] = std::clamp(Volume, 0.0f, 2.0f);
#endif
}

float FAudioSystem::GetBusVolume(EAudioBus Bus) const
{
	const int32 BusIndex = static_cast<int32>(Bus);
	if (BusIndex < 0 || BusIndex >= static_cast<int32>(EAudioBus::Count))
	{
		return 1.0f;
	}

	return Impl->BusVolumes[BusIndex];
}

void FAudioSystem::SetLooping(FAudioHandle Handle, bool bLoop)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	It->second.bLoop = bLoop;
	ma_sound_set_looping(It->second.Sound.get(), bLoop ? MA_TRUE : MA_FALSE);

	if (bLoop && ma_sound_at_end(It->second.Sound.get()))
	{
		ma_sound_seek_to_pcm_frame(It->second.Sound.get(), 0);
		ma_sound_start(It->second.Sound.get());
	}
#else
	(void)Handle;
	(void)bLoop;
#endif
}

void FAudioSystem::SetAffectedByAudioZones(FAudioHandle Handle, bool bAffected)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	It->second.bAffectedByAudioZones = bAffected;
	Impl->ApplySoundSettings(It->second);
#else
	(void)Handle;
	(void)bAffected;
#endif
}

void FAudioSystem::SetPlaybackTime(FAudioHandle Handle, float TimeSeconds)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	const float Duration = GetDuration(Handle);
	if (Duration > 0.0f)
	{
		TimeSeconds = std::clamp(TimeSeconds, 0.0f, Duration);
	}
	else
	{
		TimeSeconds = std::max(0.0f, TimeSeconds);
	}

	ma_sound_seek_to_second(It->second.Sound.get(), TimeSeconds);
#else
	(void)Handle;
	(void)TimeSeconds;
#endif
}

float FAudioSystem::GetPlaybackTime(FAudioHandle Handle) const
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return 0.0f;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return 0.0f;
	}

	float Cursor = 0.0f;
	if (ma_sound_get_cursor_in_seconds(It->second.Sound.get(), &Cursor) != MA_SUCCESS)
	{
		return 0.0f;
	}
	return Cursor;
#else
	(void)Handle;
	return 0.0f;
#endif
}

float FAudioSystem::GetDuration(FAudioHandle Handle) const
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return 0.0f;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return 0.0f;
	}

	float Duration = 0.0f;
	if (ma_sound_get_length_in_seconds(It->second.Sound.get(), &Duration) != MA_SUCCESS)
	{
		return 0.0f;
	}
	return Duration;
#else
	(void)Handle;
	return 0.0f;
#endif
}

float FAudioSystem::GetSoundDuration(const FString& SoundPath) const
{
#if NIPS_WITH_MINIAUDIO
	const std::wstring AbsolutePath = ResolveAudioPath(SoundPath);
	if (AbsolutePath.empty() || !std::filesystem::exists(std::filesystem::path(AbsolutePath)))
	{
		return 0.0f;
	}

	ma_decoder Decoder{};
	if (ma_decoder_init_file_w(AbsolutePath.c_str(), nullptr, &Decoder) != MA_SUCCESS)
	{
		return 0.0f;
	}

	ma_uint64 LengthInFrames = 0;
	const ma_result Result = ma_decoder_get_length_in_pcm_frames(&Decoder, &LengthInFrames);
	const float Duration = (Result == MA_SUCCESS && Decoder.outputSampleRate > 0)
		? static_cast<float>(static_cast<double>(LengthInFrames) / static_cast<double>(Decoder.outputSampleRate))
		: 0.0f;
	ma_decoder_uninit(&Decoder);
	return Duration;
#else
	(void)SoundPath;
	return 0.0f;
#endif
}

void FAudioSystem::SetSoundPosition(FAudioHandle Handle, const FVector& Location)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	const FVector AudioLocation = ToAudioVector(Location);
	It->second.Location = Location;
	ma_sound_set_position(It->second.Sound.get(), AudioLocation.X, AudioLocation.Y, AudioLocation.Z);
	Impl->ApplySoundSettings(It->second);
#else
	(void)Handle;
	(void)Location;
#endif
}

void FAudioSystem::SetListenerTransform(const FVector& Location, const FVector& Forward, const FVector& Up)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized)
	{
		return;
	}

	const FVector AudioLocation = ToAudioVector(Location);
	const FVector SafeForward = ToAudioVector(Forward).GetSafeNormal();
	const FVector SafeUp = ToAudioVector(Up).GetSafeNormal();
	Impl->ListenerLocation = Location;
	ma_engine_listener_set_position(&Impl->Engine, 0, AudioLocation.X, AudioLocation.Y, AudioLocation.Z);
	ma_engine_listener_set_direction(&Impl->Engine, 0, SafeForward.X, SafeForward.Y, SafeForward.Z);
	ma_engine_listener_set_world_up(&Impl->Engine, 0, SafeUp.X, SafeUp.Y, SafeUp.Z);
#else
	(void)Location;
	(void)Forward;
	(void)Up;
#endif
}

void FAudioSystem::SubmitZoneMix(uint32 ZoneId, int32 Priority,
	const FVector& Location, const FVector& Forward, const FVector& Right, const FVector& Up, const FVector& Extent,
	float InteriorMasterVolume, float InteriorSFXVolume, float InteriorMusicVolume, float InteriorAmbientVolume,
	float ExteriorMasterVolume, float ExteriorSFXVolume, float ExteriorMusicVolume, float ExteriorAmbientVolume,
	float InteriorLowPassCutoff, float ExteriorLowPassCutoff,
	float InteriorReverbWet, float InteriorReverbDecay,
	float ExteriorReverbWet, float ExteriorReverbDecay)
{
#if NIPS_WITH_MINIAUDIO
	if (ZoneId == 0)
	{
		return;
	}

	FAudioSystemImpl::FZoneMix& Zone = Impl->ZoneMixes[ZoneId];
	Zone.Priority = Priority;
	Zone.Location = Location;
	Zone.Forward = Forward.GetSafeNormal();
	Zone.Right = Right.GetSafeNormal();
	Zone.Up = Up.GetSafeNormal();
	Zone.Extent = FVector(std::max(0.01f, Extent.X), std::max(0.01f, Extent.Y), std::max(0.01f, Extent.Z));
	Zone.InteriorMasterVolume = ClampVolume(InteriorMasterVolume);
	Zone.InteriorSFXVolume = ClampVolume(InteriorSFXVolume);
	Zone.InteriorMusicVolume = ClampVolume(InteriorMusicVolume);
	Zone.InteriorAmbientVolume = ClampVolume(InteriorAmbientVolume);
	Zone.ExteriorMasterVolume = ClampVolume(ExteriorMasterVolume);
	Zone.ExteriorSFXVolume = ClampVolume(ExteriorSFXVolume);
	Zone.ExteriorMusicVolume = ClampVolume(ExteriorMusicVolume);
	Zone.ExteriorAmbientVolume = ClampVolume(ExteriorAmbientVolume);
	Zone.InteriorLowPassCutoff = ClampLowPassCutoff(InteriorLowPassCutoff);
	Zone.ExteriorLowPassCutoff = ClampLowPassCutoff(ExteriorLowPassCutoff);
	Zone.InteriorReverbWet = Clamp01(InteriorReverbWet);
	Zone.InteriorReverbDecay = Clamp01(InteriorReverbDecay);
	Zone.ExteriorReverbWet = Clamp01(ExteriorReverbWet);
	Zone.ExteriorReverbDecay = Clamp01(ExteriorReverbDecay);
#else
	(void)ZoneId;
	(void)Priority;
	(void)Location;
	(void)Forward;
	(void)Right;
	(void)Up;
	(void)Extent;
	(void)InteriorMasterVolume;
	(void)InteriorSFXVolume;
	(void)InteriorMusicVolume;
	(void)InteriorAmbientVolume;
	(void)ExteriorMasterVolume;
	(void)ExteriorSFXVolume;
	(void)ExteriorMusicVolume;
	(void)ExteriorAmbientVolume;
	(void)InteriorLowPassCutoff;
	(void)ExteriorLowPassCutoff;
	(void)InteriorReverbWet;
	(void)InteriorReverbDecay;
	(void)ExteriorReverbWet;
	(void)ExteriorReverbDecay;
#endif
}

void FAudioSystem::RemoveZoneMix(uint32 ZoneId)
{
#if NIPS_WITH_MINIAUDIO
	if (ZoneId == 0)
	{
		return;
	}

	Impl->ZoneMixes.erase(ZoneId);
	if (Impl->LastListenerZoneId == ZoneId)
	{
		Impl->LastListenerZoneId = 0;
	}
	Impl->ApplyVolumes();
#else
	(void)ZoneId;
#endif
}
