#include "pch.h"
#include "SphereComponent.h"

#include "Mesh.h"

IMPLEMENT_CLASS(USphereComponent, UPrimitiveComponent)

USphereComponent::USphereComponent()
{
    Type = "Sphere";
}

void USphereComponent::Render(ID3D11DeviceContext& DeviceContext)
{
    MeshData->Draw(DeviceContext);
}