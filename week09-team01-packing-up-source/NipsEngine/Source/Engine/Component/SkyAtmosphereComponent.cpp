#include "SkyAtmosphereComponent.h"

#include "Object/ObjectFactory.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/HeightFogComponent.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Scene/RenderCommand.h"

namespace
{
	float Saturate(float Value)
	{
		return MathUtil::Clamp(Value, 0.0f, 1.0f);
	}

	float SmoothStep(float Edge0, float Edge1, float X)
	{
		if (MathUtil::Abs(Edge1 - Edge0) <= MathUtil::Epsilon)
		{
			return (X >= Edge1) ? 1.0f : 0.0f;
		}

		const float T = Saturate((X - Edge0) / (Edge1 - Edge0));
		return T * T * (3.0f - 2.0f * T);
	}

	float LerpFloat(float A, float B, float T)
	{
		return A + (B - A) * T;
	}

	FColor WeightedColor(const FColor& Day, const FColor& Sunset, const FColor& Night, float DayWeight, float SunsetWeight, float NightWeight)
	{
		return Day * DayWeight + Sunset * SunsetWeight + Night * NightWeight;
	}
}

DEFINE_CLASS(USkyAtmosphereComponent, UPrimitiveComponent)
REGISTER_FACTORY(USkyAtmosphereComponent)

USkyAtmosphereComponent::USkyAtmosphereComponent()
{
	SetEnableCull(false);
	CachedSkyState = BuildDefaultSkyState();
}

void USkyAtmosphereComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	Ar << "DayZenithColor" << DayZenithColor;
	Ar << "DayHorizonColor" << DayHorizonColor;
	Ar << "SunsetZenithColor" << SunsetZenithColor;
	Ar << "SunsetHorizonColor" << SunsetHorizonColor;
	Ar << "NightZenithColor" << NightZenithColor;
	Ar << "NightHorizonColor" << NightHorizonColor;
	Ar << "SunDiskSize" << SunDiskSize;
	Ar << "SunDiskIntensity" << SunDiskIntensity;
	Ar << "SunHaloIntensity" << SunHaloIntensity;
	Ar << "HorizonFalloff" << HorizonFalloff;
	Ar << "TwilightRange" << TwilightRange;
	Ar << "NightStartHeight" << NightStartHeight;
	Ar << "StarsIntensity" << StarsIntensity;
	Ar << "DriveAmbientLight" << bDriveAmbientLight;
	Ar << "DriveHeightFog" << bDriveHeightFog;
}

void USkyAtmosphereComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);

	OutProps.push_back({"DayZenithColor", EPropertyType::Color, &DayZenithColor});
	OutProps.push_back({"DayHorizonColor", EPropertyType::Color, &DayHorizonColor});
	OutProps.push_back({"SunsetZenithColor", EPropertyType::Color, &SunsetZenithColor});
	OutProps.push_back({"SunsetHorizonColor", EPropertyType::Color, &SunsetHorizonColor});
	OutProps.push_back({"NightZenithColor", EPropertyType::Color, &NightZenithColor});
	OutProps.push_back({"NightHorizonColor", EPropertyType::Color, &NightHorizonColor});
	OutProps.push_back({"SunDiskSize", EPropertyType::Float, &SunDiskSize, 0.1f, 10.0f, 0.01f});
	OutProps.push_back({"SunDiskIntensity", EPropertyType::Float, &SunDiskIntensity, 0.0f, 64.0f, 0.1f});
	OutProps.push_back({"SunHaloIntensity", EPropertyType::Float, &SunHaloIntensity, 0.0f, 32.0f, 0.05f});
	OutProps.push_back({"HorizonFalloff", EPropertyType::Float, &HorizonFalloff, 0.1f, 8.0f, 0.05f});
	OutProps.push_back({"TwilightRange", EPropertyType::Float, &TwilightRange, 0.01f, 1.0f, 0.01f});
	OutProps.push_back({"NightStartHeight", EPropertyType::Float, &NightStartHeight, -1.0f, 0.0f, 0.01f});
	OutProps.push_back({"StarsIntensity", EPropertyType::Float, &StarsIntensity, 0.0f, 4.0f, 0.01f});
	OutProps.push_back({"bDriveAmbientLight", EPropertyType::Bool, &bDriveAmbientLight});
	OutProps.push_back({"bDriveHeightFog", EPropertyType::Bool, &bDriveHeightFog});
}

void USkyAtmosphereComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	SunDiskSize = MathUtil::Clamp(SunDiskSize, 0.1f, 10.0f);
	SunDiskIntensity = MathUtil::Clamp(SunDiskIntensity, 0.0f, 64.0f);
	SunHaloIntensity = MathUtil::Clamp(SunHaloIntensity, 0.0f, 32.0f);
	HorizonFalloff = MathUtil::Clamp(HorizonFalloff, 0.1f, 8.0f);
	TwilightRange = MathUtil::Clamp(TwilightRange, 0.01f, 1.0f);
	NightStartHeight = MathUtil::Clamp(NightStartHeight, -1.0f, -0.001f);
	StarsIntensity = MathUtil::Clamp(StarsIntensity, 0.0f, 4.0f);

	RefreshSkyStateFromWorld();
}

void USkyAtmosphereComponent::RefreshSkyStateFromWorld()
{
	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetFocusedWorld() : nullptr;
	if (!World)
	{
		if (!bHasEverCapturedDirectionalLight)
		{
			CachedSkyState = BuildDefaultSkyState();
		}
		return;
	}

	const UDirectionalLightComponent* PrimaryDirectionalLight = nullptr;
	for (const FLightSlot& Slot : World->GetWorldLightSlots())
	{
		if (!Slot.bAlive || Slot.LightData == nullptr || !Slot.LightData->IsVisible())
		{
			continue;
		}

		const UDirectionalLightComponent* DirectionalLight = Cast<UDirectionalLightComponent>(Slot.LightData);
		if (DirectionalLight == nullptr)
		{
			continue;
		}

		PrimaryDirectionalLight = DirectionalLight;
		break;
	}

	if (PrimaryDirectionalLight != nullptr)
	{
		CachedSkyState = BuildSkyStateFromDirectionalLight(PrimaryDirectionalLight);
		bHasEverCapturedDirectionalLight = true;
	}
	else if (!bHasEverCapturedDirectionalLight)
	{
		CachedSkyState = BuildDefaultSkyState();
	}

	ApplyDrivenLightingAndFog(CachedSkyState);
}

FSkyState USkyAtmosphereComponent::BuildDefaultSkyState() const
{
	FSkyState State = {};
	State.SunDirection = FVector(0.0f, 0.0f, 1.0f);
	State.SunHeight = 1.0f;
	State.DayWeight = 1.0f;
	State.SunsetWeight = 0.0f;
	State.NightWeight = 0.0f;
	State.SkyZenithColor = DayZenithColor;
	State.SkyHorizonColor = DayHorizonColor;
	State.SunColor = FColor(1.0f, 0.94f, 0.84f, 1.0f);
	State.AmbientColor = FColor::Lerp(DayHorizonColor, DayZenithColor, 0.35f);
	State.FogColor = FColor::Lerp(DayHorizonColor, DayZenithColor, 0.15f);
	State.AmbientIntensityScale = 1.0f;
	State.FogDensityScale = 0.75f;
	State.SunDiskIntensity = SunDiskIntensity;
	State.SunHaloIntensity = SunHaloIntensity * 0.5f;
	State.StarsIntensity = 0.0f;
	return State;
}

FSkyState USkyAtmosphereComponent::BuildSkyStateFromDirectionalLight(const UDirectionalLightComponent* DirectionalLight) const
{
	FSkyState State = BuildDefaultSkyState();
	if (DirectionalLight == nullptr)
	{
		return State;
	}

	const FVector DirectionToLight = (DirectionalLight->GetForwardVector() * -1.0f).GetSafeNormal();
	const float SunHeight = MathUtil::Clamp(FVector::DotProduct(DirectionToLight, FVector(0.0f, 0.0f, 1.0f)), -1.0f, 1.0f);
	const float SafeTwilightRange = MathUtil::Clamp(TwilightRange, 0.01f, 1.0f);
	const float SafeNightStart = MathUtil::Clamp(NightStartHeight, -1.0f, -0.001f);

	float DayWeight = SmoothStep(0.0f, SafeTwilightRange, SunHeight);
	float NightWeight = 1.0f - SmoothStep(SafeNightStart, 0.0f, SunHeight);

	float SunsetWeight = 1.0f - Saturate(MathUtil::Abs(SunHeight) / SafeTwilightRange);
	SunsetWeight = SmoothStep(0.0f, 1.0f, SunsetWeight);
	SunsetWeight *= (1.0f - DayWeight) * (1.0f - NightWeight);

	const float WeightSum = DayWeight + SunsetWeight + NightWeight;
	if (WeightSum > MathUtil::Epsilon)
	{
		DayWeight /= WeightSum;
		SunsetWeight /= WeightSum;
		NightWeight /= WeightSum;
	}
	else
	{
		DayWeight = 1.0f;
		SunsetWeight = 0.0f;
		NightWeight = 0.0f;
	}

	const FColor DayAmbient = FColor::Lerp(DayHorizonColor, DayZenithColor, 0.35f);
	const FColor SunsetAmbient = FColor::Lerp(SunsetHorizonColor, SunsetZenithColor, 0.25f);
	const FColor NightAmbient = FColor::Lerp(NightHorizonColor, NightZenithColor, 0.5f);

	const FColor DayFog = FColor::Lerp(DayHorizonColor, DayZenithColor, 0.15f);
	const FColor SunsetFog = FColor::Lerp(SunsetHorizonColor, SunsetZenithColor, 0.18f);
	const FColor NightFog = FColor::Lerp(NightHorizonColor, NightZenithColor, 0.1f);

	const FColor DirectionalLightTint = DirectionalLight->GetLightColor();
	const FColor DaySunBase = FColor(1.0f, 0.95f, 0.84f, 1.0f);
	const FColor SunsetSunBase = FColor(1.0f, 0.62f, 0.32f, 1.0f);
	const FColor NightSunBase = FColor(0.58f, 0.66f, 0.82f, 1.0f);

	State.bHasDirectionalLight = true;
	State.SunDirection = DirectionToLight;
	State.SunHeight = SunHeight;
	State.DayWeight = DayWeight;
	State.SunsetWeight = SunsetWeight;
	State.NightWeight = NightWeight;
	State.SkyZenithColor = WeightedColor(DayZenithColor, SunsetZenithColor, NightZenithColor, DayWeight, SunsetWeight, NightWeight);
	State.SkyHorizonColor = WeightedColor(DayHorizonColor, SunsetHorizonColor, NightHorizonColor, DayWeight, SunsetWeight, NightWeight);
	State.SunColor = WeightedColor(DaySunBase, SunsetSunBase, NightSunBase, DayWeight, SunsetWeight, NightWeight) * DirectionalLightTint;
	State.AmbientColor = WeightedColor(DayAmbient, SunsetAmbient, NightAmbient, DayWeight, SunsetWeight, NightWeight);
	State.FogColor = WeightedColor(DayFog, SunsetFog, NightFog, DayWeight, SunsetWeight, NightWeight);
	State.AmbientIntensityScale = DayWeight * 1.0f + SunsetWeight * 0.72f + NightWeight * 0.25f;
	State.FogDensityScale = DayWeight * 0.75f + SunsetWeight * 1.0f + NightWeight * 1.15f;

	const float LightIntensityScale = MathUtil::Clamp(DirectionalLight->GetIntensity(), 0.25f, 4.0f);
	State.SunDiskIntensity = SunDiskIntensity * (DayWeight * 1.0f + SunsetWeight * 0.9f + NightWeight * 0.12f) * LightIntensityScale;
	State.SunHaloIntensity = SunHaloIntensity * (DayWeight * 0.45f + SunsetWeight * 1.2f + NightWeight * 0.08f) * LightIntensityScale;
	State.StarsIntensity = StarsIntensity * NightWeight;
	return State;
}

void USkyAtmosphereComponent::ApplyDrivenLightingAndFog(const FSkyState& State)
{
	if (!bDriveAmbientLight && !bDriveHeightFog)
	{
		return;
	}

	if (GetOwner() == nullptr || GetOwner()->GetFocusedWorld() == nullptr)
	{
		return;
	}

	if (bDriveAmbientLight)
	{
		DriveAmbientLights(State);
	}

	if (bDriveHeightFog)
	{
		DriveHeightFog(State);
	}
}

void USkyAtmosphereComponent::DriveAmbientLights(const FSkyState& State)
{
	UWorld* World = GetOwner()->GetFocusedWorld();
	if (!World)
	{
		return;
	}

	for (const FLightSlot& Slot : World->GetWorldLightSlots())
	{
		if (!Slot.bAlive || Slot.LightData == nullptr)
		{
			continue;
		}

		UAmbientLightComponent* AmbientLight = Cast<UAmbientLightComponent>(Slot.LightData);
		if (AmbientLight == nullptr)
		{
			continue;
		}

		auto BaselineIt = AmbientDriveBaselines.find(AmbientLight->GetUUID());
		if (BaselineIt == AmbientDriveBaselines.end())
		{
			FAmbientDriveBaseline Baseline = {};
			Baseline.LightColor = AmbientLight->GetLightColor();
			Baseline.Intensity = AmbientLight->GetIntensity();
			BaselineIt = AmbientDriveBaselines.emplace(AmbientLight->GetUUID(), Baseline).first;
		}

		const FAmbientDriveBaseline& Baseline = BaselineIt->second;
		AmbientLight->SetLightColor(Baseline.LightColor * State.AmbientColor);
		AmbientLight->SetIntensity(Baseline.Intensity * State.AmbientIntensityScale);
	}
}

void USkyAtmosphereComponent::DriveHeightFog(const FSkyState& State)
{
	UWorld* World = GetOwner()->GetFocusedWorld();
	if (!World || World->GetPersistentLevel() == nullptr)
	{
		return;
	}

	for (AActor* Actor : World->GetPersistentLevel()->GetActors())
	{
		if (Actor == nullptr)
		{
			continue;
		}

		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			UHeightFogComponent* HeightFog = Cast<UHeightFogComponent>(Primitive);
			if (HeightFog == nullptr)
			{
				continue;
			}

			auto BaselineIt = FogDriveBaselines.find(HeightFog->GetUUID());
			if (BaselineIt == FogDriveBaselines.end())
			{
				FFogDriveBaseline Baseline = {};
				Baseline.FogColor = FColor(HeightFog->GetFogInscatteringColor().X, HeightFog->GetFogInscatteringColor().Y, HeightFog->GetFogInscatteringColor().Z, HeightFog->GetFogInscatteringColor().W);
				Baseline.FogDensity = HeightFog->GetFogDensity();
				Baseline.FogMaxOpacity = HeightFog->GetFogMaxOpacity();
				BaselineIt = FogDriveBaselines.emplace(HeightFog->GetUUID(), Baseline).first;
			}

			const FFogDriveBaseline& Baseline = BaselineIt->second;
			const FColor DrivenFogColor = FColor::Lerp(Baseline.FogColor, State.FogColor, 0.8f);
			const float BaseDensity = (Baseline.FogDensity > 0.001f) ? Baseline.FogDensity : 0.15f;
			const float MaxOpacityScale = 0.9f + State.SunsetWeight * 0.1f + State.NightWeight * 0.05f;

			HeightFog->SetFogInscatteringColor(DrivenFogColor.ToVector4());
			HeightFog->SetFogDensity(BaseDensity * State.FogDensityScale);
			HeightFog->SetFogMaxOpacity(MathUtil::Clamp(Baseline.FogMaxOpacity * MaxOpacityScale, 0.0f, 1.0f));
		}
	}
}

void USkyAtmosphereComponent::FillSkyConstants(const FRenderBus& RenderBus, FSkyConstants& OutConstants) const
{
	OutConstants.InvView = RenderBus.GetView().GetInverse();
	OutConstants.InvProjection = RenderBus.GetProj().GetInverse();
	OutConstants.SkyZenithColor = CachedSkyState.SkyZenithColor.ToVector4();
	OutConstants.SkyHorizonColor = CachedSkyState.SkyHorizonColor.ToVector4();
	OutConstants.SunColor = CachedSkyState.SunColor.ToVector4();
	OutConstants.SunDirectionAndDiskSize = FVector4(
		CachedSkyState.SunDirection.X,
		CachedSkyState.SunDirection.Y,
		CachedSkyState.SunDirection.Z,
		SunDiskSize);
	OutConstants.SkyParams0 = FVector4(
		CachedSkyState.SunDiskIntensity,
		CachedSkyState.SunHaloIntensity,
		HorizonFalloff,
		CachedSkyState.StarsIntensity);
	OutConstants.SkyParams1 = FVector4(
		CachedSkyState.SunsetWeight,
		CachedSkyState.NightWeight,
		CachedSkyState.DayWeight,
		0.0f);
	OutConstants.CameraForward = FVector4(RenderBus.GetCameraForward().X, RenderBus.GetCameraForward().Y, RenderBus.GetCameraForward().Z, 0.0f);
	OutConstants.CameraRight = FVector4(RenderBus.GetCameraRight().X, RenderBus.GetCameraRight().Y, RenderBus.GetCameraRight().Z, 0.0f);
	OutConstants.CameraUp = FVector4(RenderBus.GetCameraUp().X, RenderBus.GetCameraUp().Y, RenderBus.GetCameraUp().Z, 0.0f);
}

void USkyAtmosphereComponent::UpdateWorldAABB() const
{
	WorldAABB.Reset();

	const FVector Center = GetWorldLocation();
	const FVector Scale = GetWorldScale();
	const float MaxScale = MathUtil::Clamp(std::max(std::max(Scale.X, Scale.Y), Scale.Z), 0.01f, 1000.0f);
	const float Extent = 200000.0f * MaxScale;

	WorldAABB.Expand(Center - FVector(Extent, Extent, Extent));
	WorldAABB.Expand(Center + FVector(Extent, Extent, Extent));
}

bool USkyAtmosphereComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
	(void)Ray;
	(void)OutHitResult;
	return false;
}
