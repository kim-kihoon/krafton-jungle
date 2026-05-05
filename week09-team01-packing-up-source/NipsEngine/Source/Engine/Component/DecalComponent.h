#pragma once

#include "Component/PrimitiveComponent.h"

class UMaterialInterface;

class UDecalComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(UDecalComponent, UPrimitiveComponent)

	UDecalComponent();

	void PostDuplicate(UObject* Original) override;

	virtual void Serialize(FArchive& Ar) override;

	void BeginPlay() override;

	virtual void SetMaterial(int32 SlotIndex, UMaterialInterface* InMaterial) override { if (SlotIndex == 0) Materials[0] = InMaterial; }
	virtual UMaterialInterface* GetMaterial(int32 SlotIndex) const override { return (SlotIndex == 0) ? Materials[0] : nullptr; }
	virtual int32 GetNumMaterials() const override { return 1; }

	// Legacy single-slot access
	void SetMaterial(UMaterialInterface* InMaterial) { SetMaterial(0, InMaterial); }
	UMaterialInterface* GetMaterial() const { return GetMaterial(0); }

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_Decal; }

	FMatrix GetDecalMatrix() const;
	FColor GetDecalColor() const { return DecalColor; }

	void SetSize(const FVector& InSize) { DecalSize = InSize; }
	const FVector& GetDecalSize() const { return DecalSize; }

	void SetFadeIn(float InStartDelay, float InDuration);
	void SetFadeOut(float InStartDelay, float InDuration, bool bInDestroyOwnerAfterFade = false);

	bool SupportsOutline() const override { return true; }

	// GameJam
	void InitializeMask(uint32 InWidth, uint32 InHeight);
	void ResizeMaskToDecalSize();
    void PaintMask(FVector2 UV, float Radius, uint8 Value);
	void UpdateMaskTexture();
	ID3D11ShaderResourceView* GetMaskSRV() const { return MaskSRV.Get(); }
	bool IsCleanCompleteEffectActive() const { return bCleanCompleteFlashActive || (bCleanCompleteFlashPlayed && FadeStartDelay + FadeDuration > 0.0f); }
    float GetCleanPercentage() const;
    bool WorldPosToDecalUV(const FVector& WorldPos, FVector2& OutUV) const;
    bool IsPixelCleanAt(FVector2 UV) const;

protected:
	void TickComponent(float DeltaTime) override;

private:
	void TickFadeIn();
	void TickFadeOut();
	void StartCleanCompleteFlash(float CleanAlpha);
	void TickCleanCompleteFlash(float DeltaTime);

private:
	TArray<UMaterialInterface*> Materials;
	FVector DecalSize = FVector(5.0f, 5.0f, 5.0f);
	FColor DecalColor = FColor::White();

	float FadeStartDelay = 0.0f;
	float FadeDuration = 0.0f;
	float FadeInDuration = 0.0f;
	float FadeInStartDelay = 0.0f;
	bool bDestroyOwnerAfterFade = false;

	float LifeTime = 0.0f;
	float CleanCompleteFlashTime = 0.0f;
	bool bCleanCompleteFlashActive = false;
	bool bCleanCompleteFlashPlayed = false;


	// GameJam
    TArray<uint8_t> MaskPixels;

	uint32 MaskWidth = 256;
    uint32 MaskHeight = 256;

	TComPtr<ID3D11Texture2D> MaskTexture;
	TComPtr<ID3D11ShaderResourceView> MaskSRV;

	bool bMaskDirty = false;
	float CachedCleanPercentage = 0.0f; // 디버그용, 추후 수정 필요할 시 변경
};
