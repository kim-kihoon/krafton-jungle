#include "pch.h"
#include "TriangleComponent.h"
#include "Mesh.h"

IMPLEMENT_CLASS(UTriangleComponent, UPrimitiveComponent)

UTriangleComponent::UTriangleComponent()
{
    Type = "Triangle";
}

void UTriangleComponent::Render(ID3D11DeviceContext& DeviceContext)
{
    MeshData->Draw(DeviceContext);
}