#pragma once
#include "LightComponent.h"
#include <algorithm>

enum class EShadowMode
{
	CSM = 0,
	PSM,
	Max
};

class UDirectionalLightComponent : public ULightComponent
{
public:
    DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)

    static constexpr const char* BillboardTexturePath = "Asset/Texture/Icons/S_LightDirectional.PNG";

    UDirectionalLightComponent();
    ~UDirectionalLightComponent() override = default;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void Serialize(FArchive& Ar) override;
    void PostDuplicate(UObject* Original) override;

    const char* GetBillboardTexturePath() const override { return BillboardTexturePath; }

	// ──────────── Directional Shadow Map ────────────
	EShadowMode GetShadowMode() const { return ShadowMode; }
	void SetShadowMode(EShadowMode InShadowMode)
	{
		const int32 ShadowModeValue = static_cast<int32>(InShadowMode);
		if (ShadowModeValue >= 0 && ShadowModeValue < static_cast<int32>(EShadowMode::Max))
		{
			ShadowMode = InShadowMode;
		}
	}

	float GetShadowDistance() const { return ShadowDistance; }
	float GetCascadeSplitWeight() const { return CascadeSplitWeight;  }
	float GetPSMVirtualSlideBack() const { return PSMVirtualSlideBack; }
	void SetPSMVirtualSlideBack(float InVirtualSlideBack)
	{
		PSMVirtualSlideBack = std::max(0.0f, InVirtualSlideBack);
	}

private:
	EShadowMode ShadowMode = EShadowMode::CSM;
	float ShadowDistance = 500.0f; // 빛마다 Cascade Split을 다르게 조정할 수 있으므로 static constexpr로 선언하지 않는다.
	float CascadeSplitWeight = 0.5f; // 0.0f면 선형 분할하며, 1.0f면 로그 분할(가까울수록 좁게, 멀수록 크게)한다.
	float PSMVirtualSlideBack = 100.0f;
};
