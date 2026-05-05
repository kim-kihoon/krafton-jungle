#pragma once

#include "PrimitiveComponent.h"
#include "Core/ResourceTypes.h"
#include "Object/FName.h"
#include "Math/Vector4.h"

class FViewportCamera;

class USubUVComponent : public UPrimitiveComponent
{
public:
	DECLARE_CLASS(USubUVComponent, UPrimitiveComponent)

	USubUVComponent();
	~USubUVComponent() override = default;

	virtual void PostDuplicate(UObject* Original) override;
	virtual void Serialize(FArchive& Ar) override;

	void SetBillboardEnabled(bool bEnable) { bIsBillboard = bEnable; }

	// --- Particle Resource ---
	void SetParticle(const FName& InParticleName);
	const FParticleResource* GetParticle() const { return CachedParticle; }
	const FName& GetParticleName() const { return ParticleName; }

	// --- SubUV Frame ---
	void SetFrameIndex(uint32 InIndex) { FrameIndex = InIndex; }
	uint32 GetFrameIndex() const { return FrameIndex; }

	// --- Playback ---
	void SetFrameRate(float InFPS) { PlayRate = InFPS; }
	void SetLoop(bool bInLoop) { bLoop = bInLoop; }
	bool IsLoop()     const { return bLoop; }
	bool IsFinished() const { return !bLoop && bIsExecute; }
	void Play() { FrameIndex = 0; TimeAccumulator = 0.0f; bIsExecute = false; } // 처음부터 다시 재생

	// --- Sprite Size (월드 공간 크기) ---
	void SetSpriteSize(float InWidth, float InHeight) { Width = InWidth; Height = InHeight; }
	float GetWidth()  const { return Width; }
	float GetHeight() const { return Height; }

	// --- Tint Color (오염도 시각화용) ---
	void SetTintColor(const FVector4& InColor) { TintColor = InColor; }
	const FVector4& GetTintColor() const { return TintColor; }

    static FMatrix MakeBillboardWorldMatrix(const FVector& WorldLocation, const FVector& WorldScale, const FVector& CameraForward, const FVector& CameraRight, const FVector& CameraUp);

    // --- Property / Serialization ---
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	// --- PrimitiveComponent 인터페이스 ---
	EPrimitiveType GetPrimitiveType() const override { return PrimitiveType; }
	bool SupportsOutline() const override { return true; }
	static constexpr EPrimitiveType PrimitiveType = EPrimitiveType::EPT_SubUV;

	void UpdateWorldAABB() const override;
	bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;

protected:
	void TickComponent(float DeltaTime) override;

private:
	bool TryGetActiveCamera(const FViewportCamera*& OutCamera) const;

	FName ParticleName;
	FParticleResource* CachedParticle = nullptr;

	uint32 FrameIndex = 0;
	float  Width = 1.0f;
	float  Height = 1.0f;
	float  PlayRate = 30.0f;
	float  TimeAccumulator = 0.0f;

	bool bIsBillboard = true;
	bool bLoop = true;
	bool bIsExecute = false;

	FVector4 TintColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
};
