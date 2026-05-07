#pragma once
#include "Component/SceneComponent.h"
#include "Component/BillboardComponent.h"
#include "Render/Common/RenderTypes.h"
#include "GameFramework/World.h"
#include "Core/PropertyTypes.h"

class ULightComponentBase : public USceneComponent
{
public:
    DECLARE_CLASS(ULightComponentBase, USceneComponent)

    ULightComponentBase() = default;
    ~ULightComponentBase() override = default;
	
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

    void PostDuplicate(UObject* Original) override;

    void Serialize(FArchive& Ar) override;

    void BeginPlay() override;
    void EndPlay() override;

    void OnRegister() override;
    void OnUnregister() override;

    virtual const char* GetBillboardTexturePath() const { return nullptr; }

public:
    const FColor& GetLightColor() const { return LightColor; }
    float GetIntensity() const { return Intensity; }
    bool IsVisible() const { return bVisible; }
	bool IsCastShadows() const { return bCastShadows; } // DoesCastShadows() in UE5, 통일성을 위해 Is 유지

    void SetLightColor(const FColor& InColor) { LightColor = InColor; }
    void SetIntensity(float InIntensity) { Intensity = InIntensity; }
    void SetVisible(bool bInVisible) { bVisible = bInVisible; }
    void SetCastShadows(bool bInCastShadows) { bCastShadows = bInCastShadows; }

	const FLightHandle& GetLightHandle() const { return LightHandle; }
    void SetLightHandle(const FLightHandle& InLightHandle) { LightHandle = InLightHandle; }

private:
    FColor LightColor = FColor(1.0f, 1.0f, 1.0f, 1.0f);
    float Intensity = 1.0f;
    bool bVisible = true;
	bool bCastShadows = true;

	FLightHandle LightHandle;
};

class ULightComponent : public ULightComponentBase
{
public:
    DECLARE_CLASS(ULightComponent, ULightComponentBase)

    ULightComponent();
    ~ULightComponent() override = default;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void Serialize(FArchive& Ar) override;
    void PostDuplicate(UObject* Original) override;

	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowSharpen() const { return ShadowSharpen; }
	bool IsShadowTexelSnapped() const { return bShadowTexelSnapped; }

public:
    ELightType GetLightType() const { return LightType; }

protected:
    void SetLightType(ELightType InLightType) { LightType = InLightType; }
	bool bShadowTexelSnapped = true;

private:
    ELightType LightType = ELightType::Max;

	float ShadowResolutionScale = 1.0f;
	float ShadowBias = 0.001f;
	float ShadowSlopeBias = 1.0f;
	float ShadowSharpen = 0.5f; 
};
