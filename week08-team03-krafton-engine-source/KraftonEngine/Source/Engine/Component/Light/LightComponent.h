#pragma once
#include "Component/Light/LightComponentBase.h"
#include "Render/Resource/TexturePool/TextureAtlasPool.h"
#include "Render/Resource/TexturePool/TextureCubeShadowPool.h"

using FShadowHandle = FTexturePoolBase::TexturePoolHandle;
using FShadowHandleSet = FTexturePoolBase::TexturePoolHandleSet;
using FShadowCubeHandle = FTextureCubeShadowPool::FCubeShadowHandle;

struct FShadowMapKey
{
	bool IsAtlas() { return !Atlas.empty(); }
	bool IsCubeMap() { return CubeMap.IsValid(); }
	TArray<FAtlasUV> Atlas;
	FShadowCubeHandle CubeMap;

};

class ULightComponent : public ULightComponentBase
{
public:
	DECLARE_CLASS(ULightComponent, ULightComponentBase)

	~ULightComponent() { if(ShadowHandleSet) ShadowHandleSet->Release(); }

	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	void SetShadowResolutionScale(float InShadowResolutionScale);
	uint32 GetShadowResolution() const;
	float GetShadowBias() const { return ShadowBias; }
	void SetShadowBias(float InShadowBias) { ShadowBias = InShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	void SetShadowSlopeBias(float InShadowSlopeBias) { ShadowSlopeBias = InShadowSlopeBias; }
	float GetShadowSharpen() const { return ShadowSharpen; }
	void SetShadowSharpen(float InShadowSharpen);
	void InvalidateShadowHandleSet();
	void SetCastShadow(bool bInCastShadow) { bCastShadow = bInCastShadow; }

	virtual void Serialize(FArchive& Ar) override;

	virtual FShadowHandleSet* GetShadowHandleSet() { return ShadowHandleSet; }
	virtual FShadowMapKey GetShadowMapKey() { return FShadowMapKey(); }
	FShadowHandleSet* PeekShadowHandleSet() const { return ShadowHandleSet; }
	void SetShadowHandleSetForRenderer(FShadowHandleSet* InHandleSet);
	void ReleaseShadowHandleSetForRenderer();
	void MarkShadowAtlasRequested(uint64 FrameIndex);
	void MarkShadowAtlasSelected(uint64 FrameIndex);
	uint64 GetLastShadowAtlasRequestedFrame() const { return LastShadowAtlasRequestedFrame; }
	uint64 GetLastShadowAtlasSelectedFrame() const { return LastShadowAtlasSelectedFrame; }
	bool ShouldReleaseShadowAtlasHandle(uint64 FrameIndex, uint64 GraceFrameCount) const;
	void MarkShadowAtlasAllocationFailed(uint64 FrameIndex, uint32 Resolution);
	bool ShouldSkipShadowAtlasAllocation(uint64 FrameIndex, uint32 RequestedResolution, uint64 CooldownFrameCount) const;
	bool UpdateShadowAtlasDownscaleCandidate(uint32 DesiredResolution, uint64 FrameIndex, uint64 StableFrameCount);
	void ClearShadowAtlasDownscaleCandidate();

protected:
	float ShadowResolutionScale = 2.0f;
	float ShadowBias = 0.5f;
	float ShadowSlopeBias = 0.5f;
	float ShadowSharpen = 0.0f;

	FShadowHandleSet* ShadowHandleSet = nullptr;
	uint64 LastShadowAtlasRequestedFrame = 0;
	uint64 LastShadowAtlasSelectedFrame = 0;
	uint64 LastShadowAtlasAllocationFailedFrame = 0;
	uint32 LastFailedShadowResolution = 0;
	uint32 PendingShadowAtlasDownscaleResolution = 0;
	uint64 PendingShadowAtlasDownscaleStartFrame = 0;
};
