#include "Trail.h"

Trail::Trail() : UPrimitive()
{
}

Trail::~Trail()
{
}

DirectX::XMMATRIX Trail::GetTransformMatrix()
{
    float angle = atan2f(Velocity.y, Velocity.x);

    DirectX::XMMATRIX scaleMat = DirectX::XMMatrixScaling(0.01f, 0.01f, 1.0f);
    DirectX::XMMATRIX rotationMat = DirectX::XMMatrixRotationZ(0);
    DirectX::XMMATRIX translationMat = DirectX::XMMatrixTranslation(Location.x, Location.y, 0.0f);

    worldMatrix = scaleMat * rotationMat * translationMat;

    return DirectX::XMMatrixTranspose(worldMatrix);
}