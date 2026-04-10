#include "BillboardComponent.h"
#include "PrimitiveComponent.h"
#include "Renderer/MeshData.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialManager.h"
#include "Object/Object.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(UBillboardComponent, UPrimitiveComponent)

void UBillboardComponent::PostConstruct()
{
	bDrawDebugBounds = false;
	bBillboard = true;
	BillboardMesh = std::make_shared<FDynamicMesh>();

	MaterialInstance = FMaterialManager::Get().FindByName("M_Default_Texture")->CreateDynamicMaterial();
}

FBoxSphereBounds UBillboardComponent::GetWorldBounds() const
{
	const FVector Center = GetWorldLocation();
	const FVector WorldScale = GetWorldTransform().GetScaleVector();

	const float HalfW = Size.X * 0.5f * WorldScale.X;
	const float HalfH = Size.Y * 0.5f * WorldScale.Y;
	const float HalfZ = ((HalfW > HalfH) ? HalfW : HalfH);

	const FVector BoxExtent(HalfW, HalfH, HalfZ);
	return { Center, BoxExtent.Size(), BoxExtent };
}

FRenderMesh* UBillboardComponent::GetRenderMesh() const
{
	return BillboardMesh.get();
}

void UBillboardComponent::FixupReferences(const FDuplicateionContext& Context)
{
	UPrimitiveComponent::FixupReferences(Context);

	// 2. 포인터 공유 방지 (PIE 복제본에게만 새 리소스 발급)
	this->BillboardMesh = std::make_shared<FDynamicMesh>();
	if (this->MaterialInstance)
	{
		this->MaterialInstance = this->MaterialInstance->CreateDynamicMaterial();
	}
}

void UBillboardComponent::SetSpriteTexture(std::shared_ptr<FMaterialTexture> InTexture)
{
	if (MaterialInstance)
	{
		MaterialInstance->SetMaterialTexture(InTexture);
	}
}

void UBillboardComponent::ResetMaterial(const FString& MaterialName)
{
	auto Base = FMaterialManager::Get().FindByName(MaterialName);
	if (Base)
	{
		MaterialInstance = Base->CreateDynamicMaterial();
	}
}
