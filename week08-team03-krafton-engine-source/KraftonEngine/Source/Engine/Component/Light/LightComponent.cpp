#include "Component/Light/LightComponent.h"
#include "Object/ObjectFactory.h"
#include "Engine/Serialization/Archive.h"
#include "Math/MathUtils.h"

IMPLEMENT_ABSTRACT_CLASS(ULightComponent, ULightComponentBase)

void ULightComponent::SetShadowResolutionScale(float InShadowResolutionScale)
{
	if (ShadowResolutionScale == InShadowResolutionScale)
	{
		return;
	}

	ShadowResolutionScale = InShadowResolutionScale;
	InvalidateShadowHandleSet();
}

uint32 ULightComponent::GetShadowResolution() const
{
	const float SafeScale = ShadowResolutionScale > 0.0f ? ShadowResolutionScale : 0.0f;
	if (SafeScale <= 0.25f)
	{
		return 256;
	}
	if (SafeScale <= 0.5f)
	{
		return 512;
	}
	if (SafeScale <= 1.0f)
	{
		return 1024;
	}
	return 2048;
}

void ULightComponent::SetShadowSharpen(float InShadowSharpen)
{
	ShadowSharpen = FMath::Clamp(InShadowSharpen, 0.0f, 0.5f);
}

void ULightComponent::InvalidateShadowHandleSet()
{
	if (ShadowHandleSet)
	{
		ShadowHandleSet->bIsValid = false;
	}
}

void ULightComponent::SetShadowHandleSetForRenderer(FShadowHandleSet* InHandleSet)
{
	if (ShadowHandleSet && ShadowHandleSet != InHandleSet)
	{
		ShadowHandleSet->Release();
	}

	ShadowHandleSet = InHandleSet;
}

void ULightComponent::ReleaseShadowHandleSetForRenderer()
{
	if (ShadowHandleSet)
	{
		ShadowHandleSet->Release();
		ShadowHandleSet = nullptr;
	}
}

void ULightComponent::MarkShadowAtlasRequested(uint64 FrameIndex)
{
	LastShadowAtlasRequestedFrame = FrameIndex;
}

void ULightComponent::MarkShadowAtlasSelected(uint64 FrameIndex)
{
	LastShadowAtlasSelectedFrame = FrameIndex;
}

bool ULightComponent::ShouldReleaseShadowAtlasHandle(uint64 FrameIndex, uint64 GraceFrameCount) const
{
	if (!ShadowHandleSet || LastShadowAtlasRequestedFrame == 0 || FrameIndex <= LastShadowAtlasRequestedFrame)
	{
		return false;
	}

	return FrameIndex - LastShadowAtlasRequestedFrame > GraceFrameCount;
}

void ULightComponent::MarkShadowAtlasAllocationFailed(uint64 FrameIndex, uint32 Resolution)
{
	LastShadowAtlasAllocationFailedFrame = FrameIndex;
	LastFailedShadowResolution = Resolution;
}

bool ULightComponent::ShouldSkipShadowAtlasAllocation(uint64 FrameIndex, uint32 RequestedResolution, uint64 CooldownFrameCount) const
{
	if (LastShadowAtlasAllocationFailedFrame == 0 || FrameIndex <= LastShadowAtlasAllocationFailedFrame)
	{
		return false;
	}

	return FrameIndex - LastShadowAtlasAllocationFailedFrame <= CooldownFrameCount
		&& RequestedResolution >= LastFailedShadowResolution;
}

bool ULightComponent::UpdateShadowAtlasDownscaleCandidate(uint32 DesiredResolution, uint64 FrameIndex, uint64 StableFrameCount)
{
	if (DesiredResolution == 0)
	{
		PendingShadowAtlasDownscaleResolution = 0;
		PendingShadowAtlasDownscaleStartFrame = 0;
		return false;
	}

	if (PendingShadowAtlasDownscaleResolution != DesiredResolution)
	{
		PendingShadowAtlasDownscaleResolution = DesiredResolution;
		PendingShadowAtlasDownscaleStartFrame = FrameIndex;
		return false;
	}

	if (FrameIndex <= PendingShadowAtlasDownscaleStartFrame)
	{
		return false;
	}

	return FrameIndex - PendingShadowAtlasDownscaleStartFrame >= StableFrameCount;
}

void ULightComponent::ClearShadowAtlasDownscaleCandidate()
{
	PendingShadowAtlasDownscaleResolution = 0;
	PendingShadowAtlasDownscaleStartFrame = 0;
}

void ULightComponent::Serialize(FArchive& Ar)
{
	ULightComponentBase::Serialize(Ar);
	Ar << ShadowResolutionScale;
	Ar << ShadowBias;
	Ar << ShadowSlopeBias;
	Ar << ShadowSharpen;

	if (Ar.IsLoading())
	{
		ShadowSharpen = FMath::Clamp(ShadowSharpen, 0.0f, 0.5f);
	}
}
