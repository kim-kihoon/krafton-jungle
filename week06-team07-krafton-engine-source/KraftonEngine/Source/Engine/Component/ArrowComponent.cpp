#include "ArrowComponent.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Serialization/Archive.h"
#include "Resource/ResourceManager.h"
#include "Core/ResourceTypes.h"

IMPLEMENT_CLASS(UArrowComponent, UPrimitiveComponent)

UArrowComponent::UArrowComponent()
{
	bActiveGizmo = false;
	bInheritScale = false;
}

FMeshBuffer* UArrowComponent::GetMeshBuffer() const
{
	EMeshShape Shape = EMeshShape::Arrow;
	return &FMeshBufferManager::Get().GetMeshBuffer(Shape);
}

FPrimitiveSceneProxy* UArrowComponent::CreateSceneProxy()
{
	return new FArrowSceneProxy(this);
}

void UArrowComponent::SetSelected(bool bNewSelected)
{
	if(bNewSelected)
	{
		GetSceneProxy()->bVisible = true;
	}
	else
	{
		GetSceneProxy()->bVisible = false;
	}
}

void UArrowComponent::Serialize(FArchive& Ar)
{
}

void UArrowComponent::PostDuplicate()
{
}

void UArrowComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	OutProps.push_back({ "Visible", EPropertyType::Bool, &bIsVisible });
}
