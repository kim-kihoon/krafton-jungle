#include "pch.h"
#include "CubeComponent.h"
#include "Mesh.h"

IMPLEMENT_CLASS(UCubeComponent, UPrimitiveComponent)

UCubeComponent::UCubeComponent()
{
    Type = "Cube";
}

void UCubeComponent::Render(ID3D11DeviceContext& DeviceContext)
{
    MeshData->Draw(DeviceContext);
}
