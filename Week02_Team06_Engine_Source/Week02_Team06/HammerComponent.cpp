#include "pch.h"
#include "HammerComponent.h"
#include "Mesh.h"

IMPLEMENT_CLASS(UHammerComponent, UPrimitiveComponent)

UHammerComponent::UHammerComponent()
{
    Type = "Hammer";
}

void UHammerComponent::Render(ID3D11DeviceContext& DeviceContext)
{
        MeshData->Draw(DeviceContext);
}