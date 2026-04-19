#include "FireBallComponent.h"
#include "Serialization/Archive.h"
IMPLEMENT_CLASS(UFireBallComponent, UPrimitiveComponent)

void UFireBallComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);
	Ar << Intensity;
	Ar << Radius;
	Ar << RadiusFallOff;
	Ar << Color;
	Ar << RadiusOfSource;
}

void UFireBallComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Intensity", EPropertyType::Float, &Intensity });
	OutProps.push_back({ "Radius",EPropertyType::Float,&Radius });
	OutProps.push_back({ "Radius FallOff",EPropertyType::Float,&RadiusFallOff });
	OutProps.push_back({ "Color",EPropertyType::Vec4,&Color });
}
