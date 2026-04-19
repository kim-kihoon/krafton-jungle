#include "SubUVComponent.h"
#include "Object/Class.h"
#include "Renderer/MeshData.h"

IMPLEMENT_RTTI(USubUVComponent, UPrimitiveComponent)

void USubUVComponent::PostConstruct()
{
	// SubUV 렌더링용 메시 객체 생성
	bDrawDebugBounds = false;
	SubUVMesh = std::make_shared<FDynamicMesh>();
}

FRenderMesh* USubUVComponent::GetRenderMesh() const { return SubUVMesh.get(); }

void USubUVComponent::FixupReferences(const FDuplicateionContext& Context)
{
	UPrimitiveComponent::FixupReferences(Context);

	// 2. 포인터 공유 방지 (PIE 복제본에게만 새 메시 발급)
	this->SubUVMesh = std::make_shared<FDynamicMesh>();
}

FBoxSphereBounds USubUVComponent::GetWorldBounds() const
{
	const FVector Center = GetWorldLocation();
	const FVector WorldScale = GetWorldTransform().GetScaleVector();

	const float HalfW = Size.X * 0.5f * WorldScale.X;
	const float HalfH = Size.Y * 0.5f * WorldScale.Y;
	const float HalfZ = ((HalfW > HalfH) ? HalfW : HalfH);

	const FVector BoxExtent(HalfW, HalfH, HalfZ);
	return { Center, BoxExtent.Size(), BoxExtent };
}
