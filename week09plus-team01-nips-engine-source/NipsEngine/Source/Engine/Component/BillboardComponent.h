#pragma once
#include "PrimitiveComponent.h"
#include "Core/ResourceTypes.h"

class FViewportCamera;

class UBillboardComponent : public UPrimitiveComponent
{
protected:
	bool bIsBillboard = true;
	bool TryGetActiveCamera(const FViewportCamera*& OutCamera) const;

	virtual void PostDuplicate(UObject* Original) override;

public:
	DECLARE_CLASS(UBillboardComponent, UPrimitiveComponent)

	virtual void Serialize(FArchive& Ar) override;

	void TickComponent(float DeltaTime) override;

	void SetBillboardEnabled(bool bEnable) { bIsBillboard = bEnable; }
	static constexpr EPrimitiveType PrimitiveType = EPrimitiveType::EPT_Billboard;
	bool SupportsOutline() const override { return true; }

	static FMatrix MakeBillboardWorldMatrix(
		const FVector& WorldLocation,
		const FVector& WorldScale,
		const FVector& CameraForward,
		const FVector& CameraRight,
		const FVector& CameraUp);

	EPrimitiveType GetPrimitiveType() const override { return PrimitiveType; }

	void SetTexturePath(FString InTexturePath);
	FString GetTexturePath() const;
	UTexture* GetTexture();
	void SetSpriteSize(float InWidth, float InHeight) { Width = InWidth; Height = InHeight; }

	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	float GetWidth()  const { return Width; }
	float GetHeight() const { return Height; }

private:
	FString TexturePath;
	UTexture* CachedTexture = nullptr;

protected:
	float Width = 1.0f;
	float Height = 1.0f;
};
