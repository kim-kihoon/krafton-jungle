#include "Component/Primitive/PhotoPolaroidComponent.h"

#include "Render/Proxy/PhotoPolaroidSceneProxy.h"

#include <algorithm>

FPrimitiveSceneProxy* UPhotoPolaroidComponent::CreateSceneProxy()
{
	return new FPhotoPolaroidSceneProxy(this);
}

void UPhotoPolaroidComponent::SetTextures(ID3D11ShaderResourceView* InPhotoSRV, ID3D11ShaderResourceView* InFrameSRV)
{
	PhotoSRV = InPhotoSRV;
	FrameSRV = InFrameSRV;
	MarkProxyDirty(EDirtyFlag::Material);
}

void UPhotoPolaroidComponent::SetDisplayTime(float InDisplayTime)
{
	DisplayTime = InDisplayTime;
}

void UPhotoPolaroidComponent::SetDevelopTime(float InDevelopTime)
{
	DevelopTime = InDevelopTime;
	MarkProxyDirty(EDirtyFlag::Material);
}

void UPhotoPolaroidComponent::UpdateWorldAABB() const
{
	const FVector WorldCenter = GetWorldLocation();
	const float Radius = (std::max)(GetWorldScale().Y, GetWorldScale().Z) * 0.75f;
	WorldAABBMinLocation = WorldCenter - FVector(Radius, Radius, Radius);
	WorldAABBMaxLocation = WorldCenter + FVector(Radius, Radius, Radius);
	bHasValidWorldAABB = true;
	bWorldAABBDirty = false;
}
