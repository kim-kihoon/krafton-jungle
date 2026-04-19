#include "pch.h"
#include "ArrowComponent.h"
#include "Mesh.h"


IMPLEMENT_CLASS(UArrowComponent, UPrimitiveComponent)

void UArrowComponent::Render(ID3D11DeviceContext& DevcieContext)
{
    MeshData->Draw(DevcieContext);
}

