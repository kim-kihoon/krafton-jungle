#include "LightComponentBase.h"
#include "Serialization/Archive.h"
#include "Object/Reflection/ObjectFactory.h"
#include "GameFramework/AActor.h"
#include "Component/Primitive/BillboardComponent.h"
#include "Materials/MaterialManager.h"

HIDE_FROM_COMPONENT_LIST(ULightComponentBase)

UBillboardComponent* ULightComponentBase::EnsureEditorBillboard()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	const char* IconMaterialPath = nullptr;
	switch (GetLightType())
	{
	case ELightComponentType::Ambient:
		IconMaterialPath = "Content/Material/Editor/AmbientLight.uasset";
		break;
	case ELightComponentType::Directional:
		IconMaterialPath = "Content/Material/Editor/DirectionalLight.uasset";
		break;
	case ELightComponentType::Point:
		IconMaterialPath = "Content/Material/Editor/PointLight.uasset";
		break;
	case ELightComponentType::Spot:
		IconMaterialPath = "Content/Material/Editor/SpotLight.uasset";
		break;
	}

	if (!IconMaterialPath)
	{
		return nullptr;
	}

	for (USceneComponent* Child : GetChildren())
	{
		UBillboardComponent* Billboard = Cast<UBillboardComponent>(Child);
		if (Billboard && Billboard->IsEditorOnlyComponent())
		{
			// 에디터 아이콘 빌보드는 부모 스케일과 컴포넌트 트리 기본 표시에서 분리한다.
			Billboard->SetAbsoluteScale(true);
			Billboard->SetCastShadow(false);
			Billboard->SetHiddenInComponentTree(true);
			return Billboard;
		}
	}

	UBillboardComponent* Billboard = OwnerActor->AddComponent<UBillboardComponent>();
	if (Billboard)
	{
		Billboard->AttachToComponent(this);
		// 에디터 아이콘 빌보드는 부모 스케일과 컴포넌트 트리 기본 표시에서 분리한다.
		Billboard->SetAbsoluteScale(true);
		Billboard->SetEditorOnlyComponent(true);
		Billboard->SetCastShadow(false);
		Billboard->SetHiddenInComponentTree(true);
		auto Material = FMaterialManager::Get().GetOrCreateMaterial(IconMaterialPath);
		Billboard->SetMaterial(Material);
	}

	return Billboard;
}
