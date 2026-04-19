#include "CameraArrowComponent.h"
#include "Renderer/MeshData.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(UCameraArrowComponent, UPrimitiveComponent)

void UCameraArrowComponent::PostConstruct()
{
	bDrawDebugBounds = false;
}

FRenderMesh* UCameraArrowComponent::GetRenderMesh() const
{
	return ArrowMesh.get();
}

FBoxSphereBounds UCameraArrowComponent::GetWorldBounds() const
{
	const FVector Center = GetWorldLocation();
	const FVector WorldScale = GetWorldTransform().GetScaleVector();
	// 기즈모 축 길이(35) * 스케일로 바운드 계산
	const float HalfLen = 35.0f * WorldScale.X;
	const FVector Extent(HalfLen, HalfLen, HalfLen);
	return { Center, Extent.Size(), Extent };
}

