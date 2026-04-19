#pragma once
#include <DirectXMath.h>

namespace Graphics
{
    struct FDebugRenderSettings
    {
        bool bDrawGizmo = true;
        bool bDrawWorldAxes = true;
        bool bDrawGrid = true;
        bool bDrawBVH = false;
        bool bDrawUniformGrid = false;
        float GridPlaneHalfExtent = 100.0f;
        float GridPlaneSpacing = 1.0f;
    };

    struct FCameraState
    {
        DirectX::XMFLOAT3 Position = { -28.107929f, -27.869390f, 25.837776f };
        float PitchRadians = DirectX::XMConvertToRadians(0.770000f);
        float YawRadians = DirectX::XMConvertToRadians(0.926283f);
        float FOVDegrees = 60.0f;
        float NearClip = 0.1f;
        float FarClip = 100.0f;
        float MoveSpeed = 20.0f;
        float WheelSpeed = 6.0f;
        float LookSensitivity = 0.005f;
    };

    struct alignas(16) FObjectBoundsGPU
    {
        DirectX::XMFLOAT3 BoundsMin;
        uint32_t          ObjectIndex;
        DirectX::XMFLOAT3 BoundsMax;
        uint32_t          _pad;
    };
}
