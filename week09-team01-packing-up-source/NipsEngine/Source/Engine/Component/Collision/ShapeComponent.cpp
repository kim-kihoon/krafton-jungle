#include "ShapeComponent.h"

#include "Core/PropertyTypes.h"
#include "Engine/Serialization/Archive.h"

#include <algorithm>
#include <cstring>

DEFINE_CLASS(UShapeComponent, UPrimitiveComponent)

namespace
{
	bool IsScaleProperty(const FPropertyDescriptor& Prop)
	{
		return Prop.Name != nullptr && std::strcmp(Prop.Name, "Scale") == 0;
	}
}

UShapeComponent::UShapeComponent()
{
    // UShapeComponent는 메시처럼 그려지지 않고 디버그 와이어로만 표시되므로 컬링을 끈다.
    bEnableCull = false;
    bGenerateOverlapEvents = false;
    bBlockComponent = true;
}

void UShapeComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UPrimitiveComponent::GetEditableProperties(OutProps);

    OutProps.erase(std::remove_if(OutProps.begin(), OutProps.end(), IsScaleProperty), OutProps.end());

    OutProps.push_back({ "Shape Color", EPropertyType::Color, &ShapeColor });
    OutProps.push_back({ "Line Thickness", EPropertyType::Float, &LineThickness, 1.0f, 8.0f, 0.1f });
    OutProps.push_back({ "Draw Only If Selected", EPropertyType::Bool, &bDrawOnlyIfSelected });
}

void UShapeComponent::PostEditProperty(const char* PropertyName)
{
    UPrimitiveComponent::PostEditProperty(PropertyName);
}

void UShapeComponent::Serialize(FArchive& Ar)
{
    UPrimitiveComponent::Serialize(Ar);

    Ar << "ShapeColor" << ShapeColor;
    Ar << "LineThickness" << LineThickness;
    Ar << "DrawOnlyIfSelected" << bDrawOnlyIfSelected;
}

// BoxExtent/Radius/HalfHeight를 변경한 뒤 호출한다.
void UShapeComponent::UpdateBodySetup()
{
    // UE5 BodySetup은 구현되어 있지 않고, BVH 트리에 충돌체 크기 변경을 반영하는 역할만 수행한다.
    // 추후 중복되는 충돌체를 가진 액터가 아주 많은 씬의 경우 BodySetup을 구현했을 때 성능이 개선될 수 있다.
    NotifySpatialIndexDirty();
}
