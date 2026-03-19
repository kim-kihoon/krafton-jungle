#include "pch.h"
#include "RingComponent.h"
#include "Mesh.h"

IMPLEMENT_CLASS(URingComponent, UPrimitiveComponent)

URingComponent::URingComponent()
{
    Type = "Ring";
}

void URingComponent::Render(ID3D11DeviceContext& DeviceContext)
{
        MeshData->Draw(DeviceContext);
}