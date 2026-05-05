#pragma once

#include "PrimitiveComponent.h"
#include "BillboardComponent.h"

class UAmbientLightComponent;
class UDirectionalLightComponent;
class UHeightFogComponent;
class FRenderBus;
struct FSkyConstants;

struct FSkyState
{
	bool bHasDirectionalLight = false;
	FVector SunDirection = FVector(0.0f, 0.0f, 1.0f);
	float SunHeight = 1.0f;
	float DayWeight = 1.0f;
	float SunsetWeight = 0.0f;
	float NightWeight = 0.0f;
	FColor SkyZenithColor = FColor(0.11f, 0.27f, 0.58f, 1.0f);
	FColor SkyHorizonColor = FColor(0.68f, 0.82f, 0.95f, 1.0f);
	FColor SunColor = FColor(1.0f, 0.92f, 0.82f, 1.0f);
	FColor AmbientColor = FColor(0.45f, 0.52f, 0.6f, 1.0f);
	FColor FogColor = FColor(0.68f, 0.82f, 0.95f, 1.0f);
	float AmbientIntensityScale = 1.0f;
	float FogDensityScale = 0.75f;
	float SunDiskIntensity = 14.0f;
	float SunHaloIntensity = 2.0f;
	float StarsIntensity = 0.0f;
};

class USkyAtmosphereComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(USkyAtmosphereComponent, UPrimitiveComponent)

	USkyAtmosphereComponent();
	~USkyAtmosphereComponent() override = default;

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SKY; }
	bool SupportsOutline() const override { return false; }

	void RefreshSkyStateFromWorld();
	const FSkyState& GetSkyState() const { return CachedSkyState; }
	void FillSkyConstants(const FRenderBus& RenderBus, FSkyConstants& OutConstants) const;

	void SetDriveAmbientLight(bool bInDriveAmbientLight) { bDriveAmbientLight = bInDriveAmbientLight; }
	bool GetDriveAmbientLight() const { return bDriveAmbientLight; }

	void SetDriveHeightFog(bool bInDriveHeightFog) { bDriveHeightFog = bInDriveHeightFog; }
	bool GetDriveHeightFog() const { return bDriveHeightFog; }

private:
	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;

	FSkyState BuildDefaultSkyState() const;
	FSkyState BuildSkyStateFromDirectionalLight(const UDirectionalLightComponent* DirectionalLight) const;
	void ApplyDrivenLightingAndFog(const FSkyState& State);
	void DriveAmbientLights(const FSkyState& State);
	void DriveHeightFog(const FSkyState& State);

private:
	struct FAmbientDriveBaseline
	{
		FColor LightColor = FColor::White();
		float Intensity = 1.0f;
	};

	struct FFogDriveBaseline
	{
		FColor FogColor = FColor::White();
		float FogDensity = 0.0f;
		float FogMaxOpacity = 1.0f;
	};

	FSkyState CachedSkyState = {};
	bool bHasEverCapturedDirectionalLight = false;

	FColor DayZenithColor = FColor(0.11f, 0.27f, 0.58f, 1.0f);
	FColor DayHorizonColor = FColor(0.68f, 0.82f, 0.95f, 1.0f);
	FColor SunsetZenithColor = FColor(0.28f, 0.16f, 0.34f, 1.0f);
	FColor SunsetHorizonColor = FColor(0.98f, 0.56f, 0.28f, 1.0f);
	FColor NightZenithColor = FColor(0.01f, 0.02f, 0.06f, 1.0f);
	FColor NightHorizonColor = FColor(0.05f, 0.08f, 0.18f, 1.0f);

	float SunDiskSize = 1.5f;
	float SunDiskIntensity = 14.0f;
	float SunHaloIntensity = 2.0f;
	float HorizonFalloff = 1.5f;
	float TwilightRange = 0.22f;
	float NightStartHeight = -0.15f;
	float StarsIntensity = 0.65f;
	bool bDriveAmbientLight = true;
	bool bDriveHeightFog = true;

	TMap<uint32, FAmbientDriveBaseline> AmbientDriveBaselines;
	TMap<uint32, FFogDriveBaseline> FogDriveBaselines;
};
