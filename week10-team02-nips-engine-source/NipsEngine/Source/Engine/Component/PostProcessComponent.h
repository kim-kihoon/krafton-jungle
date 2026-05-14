#pragma once
#include "ActorComponent.h"
#include "Engine/Math/Utils.h"

class UPostProcessComponent : public UActorComponent
{
public:
	DECLARE_CLASS(UPostProcessComponent, UActorComponent)

	UPostProcessComponent() = default;
	~UPostProcessComponent() override = default;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void Serialize(FArchive& Ar) override;
	void PostDuplicate(UObject* Original) override;

public:
	bool IsEnableVignette() const { return bVignette; }
	float GetVignetteIntensity() const { return VignetteIntensity; }
	float GetVignetteRadius() const { return VignetteRadius; }
	float GetVignetteSoftness() const { return VignetteSoftness; }

	bool IsEnableGammaCorrection() const { return bGammaCorrection; }
	float GetGamma() const { return Gamma; }

private:
	bool bVignette = false;
	float VignetteIntensity = 0.5f;
	float VignetteRadius = 0.75f;
	float VignetteSoftness = 0.25f;

	bool bGammaCorrection = false;
	float Gamma = 2.2f;

	// ─────────────────── Setter for Lua ──────────────────────────────────────
public:
	void SetVignetteEnabled(bool bEnabled) { bVignette = bEnabled; }
	void SetVignette(float Intensity, float Radius, float Softness);
	void SetVignetteIntensity(float Intensity) { VignetteIntensity = MathUtil::Clamp(Intensity, 0.0f, 1.0f); }
	void SetVignetteRadius(float Radius) { VignetteRadius = MathUtil::Clamp(Radius, 0.0f, 1.0f); }
	void SetVignetteSoftness(float Softness) { VignetteSoftness = MathUtil::Clamp(Softness, 0.0f, 1.0f); }
	
	void SetGammaCorrectionEnabled(bool bEnabled) { bGammaCorrection = bEnabled; }
	void SetGamma(float InGamma) { Gamma = MathUtil::Clamp(InGamma, 0.01f, 10.0f); }
};
