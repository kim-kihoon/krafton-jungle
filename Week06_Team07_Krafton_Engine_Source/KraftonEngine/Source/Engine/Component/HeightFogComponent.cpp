#include "HeightFogComponent.h"
#include "Serialization/Archive.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Render/Proxy/FScene.h"

IMPLEMENT_CLASS(UHeightFogComponent, USceneComponent)

void UHeightFogComponent::CreateRenderState()
{
	USceneComponent::CreateRenderState();
	if (GetOwner() && GetOwner()->GetWorld())
	{
		GetOwner()->GetWorld()->GetScene().AddFog(this);
	}
}

void UHeightFogComponent::DestroyRenderState()
{
	if (GetOwner() && GetOwner()->GetWorld())
	{
		GetOwner()->GetWorld()->GetScene().RemoveFog(this);
	}
	USceneComponent::DestroyRenderState();
}

void UHeightFogComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	USceneComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "FogDensity",           EPropertyType::Float, &FogDensity,           0.0f,  5.0f,   0.001f });
	OutProps.push_back({ "FogHeightFalloff",     EPropertyType::Float, &FogHeightFalloff,     0.001f, 5.0f,  0.01f });
	OutProps.push_back({ "StartDistance",         EPropertyType::Float, &StartDistance,         0.0f, 100000.0f, 10.0f });
	OutProps.push_back({ "FogCutoffDistance",     EPropertyType::Float, &FogCutoffDistance,     0.0f, 100000.0f, 10.0f });
	OutProps.push_back({ "FogMaxOpacity",         EPropertyType::Float, &FogMaxOpacity,         0.0f,  1.0f,   0.01f });
	OutProps.push_back({ "FogInscatteringColor",  EPropertyType::Vec4,  &FogInscatteringColor });
}

void UHeightFogComponent::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar << FogDensity;
	Ar << FogHeightFalloff;
	Ar << StartDistance;
	Ar << FogCutoffDistance;
	Ar << FogMaxOpacity;
	Ar << FogInscatteringColor;
}
