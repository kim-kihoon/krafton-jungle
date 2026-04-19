#include "UUIDBillboardComponent.h"
#include "Actor/Actor.h"
#include "Object/Class.h"
#include <limits>

IMPLEMENT_RTTI(UUUIDBillboardComponent, UTextRenderComponent)

void UUUIDBillboardComponent::PostConstruct()
{
	UTextRenderComponent::PostConstruct();
	SetBillboard(true);
	bDrawDebugBounds = false;
	SetTextScale(0.3f); // UUID 빌보드의 기본 스케일 설정
}

FString UUUIDBillboardComponent::GetDisplayText() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		OwnerActor = dynamic_cast<AActor*>(GetOuter());
	}

	if (!OwnerActor)
	{
		return "";
	}

	return FString("UUID: ") + OwnerActor->GetUUIDString();
}

FVector UUUIDBillboardComponent::GetRenderWorldPosition() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		// Owner 포인터가 아직 Fixup되지 않았을 경우를 대비해 Outer를 체크함 (Duplicate 시 Outer는 새 액터로 설정됨)
		OwnerActor = dynamic_cast<AActor*>(GetOuter());
	}

	// UUID가 변경되었는지 실시간으로 체크 (렌더링 직전에 호출됨)
	if (OwnerActor && OwnerActor->UUID != LastDisplayUUID)
	{
		UUUIDBillboardComponent* MutableThis = const_cast<UUUIDBillboardComponent*>(this);
		MutableThis->LastDisplayUUID = OwnerActor->UUID;
		MutableThis->MarkTextMeshDirty();
	}

	if (!OwnerActor) return WorldOffset;

	USceneComponent* Root = OwnerActor->GetRootComponent();
	if (!Root) return WorldOffset;

	const FVector RootLocation = Root->GetWorldLocation();

	bool bFoundPrimitiveBounds = false;
	float MaxTopZ = -std::numeric_limits<float>::infinity();

	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		// 자기 자신(UUID 컴포넌트)이거나 nullptr이면 패스
		if (!Component || Component == this) continue;

		// ⭐ 구형/신형 구분할 필요 없이 UPrimitiveComponent 하나로 통일!
		if (!Component->IsA(UPrimitiveComponent::StaticClass())) continue;

		UPrimitiveComponent* PrimitiveComponent = static_cast<UPrimitiveComponent*>(Component);
		FBoxSphereBounds Bounds = PrimitiveComponent->GetWorldBounds();

		const float TopZ = Bounds.Center.Z + Bounds.BoxExtent.Z;

		if (!bFoundPrimitiveBounds || TopZ > MaxTopZ)
		{
			MaxTopZ = TopZ;
			bFoundPrimitiveBounds = true;
		}
	}

	if (bFoundPrimitiveBounds)
	{
		return FVector(
			RootLocation.X + WorldOffset.X,
			RootLocation.Y + WorldOffset.Y,
			MaxTopZ + WorldOffset.Z
		);
	}

	return RootLocation + WorldOffset;
}

FVector UUUIDBillboardComponent::GetRenderWorldScale() const
{
	// 빌보드는 트랜스포메이션의 스케일과 상관없이 TextScale 만을 절대적으로 사용하는 것이 일반적임
	return FVector(TextScale, TextScale, TextScale);
}

FBoxSphereBounds UUUIDBillboardComponent::GetWorldBounds() const
{
	const FVector Center = GetRenderWorldPosition();
	// radius 를 정사각형 extent 에 맞게 줄이기
	const FVector Extent(TextScale * 3.0f * 0.707f, TextScale * 3.0f * 0.707f, TextScale * 3.0f * 0.707f);

	return { Center, Extent.Size(), Extent };
}

void UUUIDBillboardComponent::FixupReferences(const FDuplicateionContext& Context)
{
	UTextRenderComponent::FixupReferences(Context);

	// 복제 후 새로운 Owner의 UUID를 즉시 반영하기 위해 Dirty 마킹
	MarkTextMeshDirty();
}
