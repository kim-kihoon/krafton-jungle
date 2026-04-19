#pragma once

#include <Graphics/Renderer.h>
#include <Graphics/RendererTypes.h>
#include <Math/MathTypes.h>
#include <Scene/SceneManager.h>
#include <Scene/SceneData.h>
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace UI
{
    inline DirectX::XMMATRIX UnpackWorldMatrix(const Math::FPacked3x4Matrix& InPackedMatrix)
    {
        return DirectX::XMMatrixSet(
            DirectX::XMVectorGetX(InPackedMatrix.Row0), DirectX::XMVectorGetX(InPackedMatrix.Row1), DirectX::XMVectorGetX(InPackedMatrix.Row2), 0.0f,
            DirectX::XMVectorGetY(InPackedMatrix.Row0), DirectX::XMVectorGetY(InPackedMatrix.Row1), DirectX::XMVectorGetY(InPackedMatrix.Row2), 0.0f,
            DirectX::XMVectorGetZ(InPackedMatrix.Row0), DirectX::XMVectorGetZ(InPackedMatrix.Row1), DirectX::XMVectorGetZ(InPackedMatrix.Row2), 0.0f,
            DirectX::XMVectorGetW(InPackedMatrix.Row0), DirectX::XMVectorGetW(InPackedMatrix.Row1), DirectX::XMVectorGetW(InPackedMatrix.Row2), 1.0f);
    }

    inline bool GetObjectWorldMatrix(const Scene::USceneManager& InSceneManager, uint32_t InObjectIndex, DirectX::XMMATRIX& OutWorldMatrix)
    {
        const Scene::FSceneDataSOA* SceneData = InSceneManager.GetSceneData();
        if (!SceneData || InObjectIndex >= SceneData->TotalObjectCount)
        {
            return false;
        }

        OutWorldMatrix = UnpackWorldMatrix(SceneData->WorldMatrices[InObjectIndex]);
        return true;
    }

    inline bool GetObjectTransform(
        const Scene::USceneManager& InSceneManager,
        uint32_t InObjectIndex,
        DirectX::XMFLOAT3& OutTranslation,
        DirectX::XMVECTOR& OutRotationQuaternion,
        DirectX::XMFLOAT3& OutScale)
    {
        const Scene::FSceneDataSOA* SceneData = InSceneManager.GetSceneData();
        if (!SceneData || InObjectIndex >= SceneData->TotalObjectCount)
        {
            return false;
        }

        OutTranslation = {
            DirectX::XMVectorGetW(SceneData->WorldMatrices[InObjectIndex].Row0),
            DirectX::XMVectorGetW(SceneData->WorldMatrices[InObjectIndex].Row1),
            DirectX::XMVectorGetW(SceneData->WorldMatrices[InObjectIndex].Row2)
        };
        OutRotationQuaternion = DirectX::XMLoadFloat4(&SceneData->RotationQuaternions[InObjectIndex]);
        OutScale = SceneData->Scale3D[InObjectIndex];
        return true;
    }

    inline DirectX::XMMATRIX BuildCameraViewMatrix(const Graphics::FCameraState& InCameraState)
    {
        const float CosPitch = std::cos(InCameraState.PitchRadians);
        const float SinPitch = std::sin(InCameraState.PitchRadians);
        const float CosYaw = std::cos(InCameraState.YawRadians);
        const float SinYaw = std::sin(InCameraState.YawRadians);

        const DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&InCameraState.Position);
        const DirectX::XMVECTOR Forward = DirectX::XMVector3Normalize(DirectX::XMVectorSet(
            CosPitch * CosYaw,
            CosPitch * SinYaw,
            SinPitch,
            0.0f));
        const DirectX::XMVECTOR CameraTarget = DirectX::XMVectorAdd(CameraPosition, Forward);
        const DirectX::XMVECTOR WorldUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        return DirectX::XMMatrixLookAtLH(CameraPosition, CameraTarget, WorldUp);
    }

    inline DirectX::XMMATRIX BuildCameraProjectionMatrix(const Graphics::FCameraState& InCameraState, int InViewportWidth, int InViewportHeight)
    {
        const float AspectRatio = (InViewportHeight > 0) ? (static_cast<float>(InViewportWidth) / static_cast<float>(InViewportHeight)) : 1.0f;
        return DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(InCameraState.FOVDegrees),
            AspectRatio,
            InCameraState.NearClip,
            InCameraState.FarClip);
    }

    inline Math::FRay ScreenToRay(
        const Graphics::FCameraState& InCameraState,
        int InLocalMouseX,
        int InLocalMouseY,
        int InViewportWidth,
        int InViewportHeight)
    {
        const float NdcX = (2.0f * static_cast<float>(InLocalMouseX)) / static_cast<float>((std::max)(InViewportWidth, 1)) - 1.0f;
        const float NdcY = 1.0f - (2.0f * static_cast<float>(InLocalMouseY)) / static_cast<float>((std::max)(InViewportHeight, 1));

        const DirectX::XMMATRIX View = BuildCameraViewMatrix(InCameraState);
        const DirectX::XMMATRIX Projection = BuildCameraProjectionMatrix(InCameraState, InViewportWidth, InViewportHeight);
        const DirectX::XMMATRIX InverseViewProjection = DirectX::XMMatrixInverse(nullptr, View * Projection);

        const DirectX::XMVECTOR NearPoint = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(NdcX, NdcY, 0.0f, 1.0f), InverseViewProjection);
        const DirectX::XMVECTOR FarPoint = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(NdcX, NdcY, 1.0f, 1.0f), InverseViewProjection);
        const DirectX::XMVECTOR Direction = DirectX::XMVectorSubtract(FarPoint, NearPoint);

        DirectX::XMFLOAT3 RayOrigin = {};
        DirectX::XMFLOAT3 RayDirection = {};
        DirectX::XMStoreFloat3(&RayOrigin, NearPoint);
        DirectX::XMStoreFloat3(&RayDirection, Direction);
        return Math::FRay(RayOrigin, RayDirection);
    }

    inline bool WorldToScreen(
        const DirectX::XMVECTOR& InWorldPosition,
        const Graphics::FCameraState& InCameraState,
        const FEditorViewportRect& InViewportRect,
        DirectX::XMFLOAT2& OutScreenPosition,
        float* OutClipW = nullptr)
    {
        if (!InViewportRect.IsValid())
        {
            return false;
        }

        const DirectX::XMMATRIX View = BuildCameraViewMatrix(InCameraState);
        const DirectX::XMMATRIX Projection = BuildCameraProjectionMatrix(InCameraState, InViewportRect.Width, InViewportRect.Height);
        const DirectX::XMVECTOR ClipPosition = DirectX::XMVector4Transform(DirectX::XMVectorSetW(InWorldPosition, 1.0f), View * Projection);
        const float ClipW = DirectX::XMVectorGetW(ClipPosition);
        if (OutClipW)
        {
            *OutClipW = ClipW;
        }
        if (ClipW <= 0.0f)
        {
            return false;
        }

        const float NdcX = DirectX::XMVectorGetX(ClipPosition) / ClipW;
        const float NdcY = DirectX::XMVectorGetY(ClipPosition) / ClipW;
        OutScreenPosition.x = static_cast<float>(InViewportRect.X) + (NdcX * 0.5f + 0.5f) * static_cast<float>(InViewportRect.Width);
        OutScreenPosition.y = static_cast<float>(InViewportRect.Y) + (1.0f - (NdcY * 0.5f + 0.5f)) * static_cast<float>(InViewportRect.Height);
        return true;
    }

    // [최적화] 호출자가 VP 행렬을 미리 한 번 계산하고 넘겨주는 오버로드.
    // DrawGizmoOverlay / HitTestGizmo 처럼 WorldToScreen을 수십~수백 번 연속 호출하는
    // 경우에 사용하면 View*Proj 행렬 빌드 비용을 호출 횟수만큼 절감할 수 있다.
    inline bool WorldToScreen(
        const DirectX::XMVECTOR& InWorldPosition,
        const DirectX::XMMATRIX& InViewProjection,
        const FEditorViewportRect& InViewportRect,
        DirectX::XMFLOAT2& OutScreenPosition)
    {
        if (!InViewportRect.IsValid())
        {
            return false;
        }

        const DirectX::XMVECTOR ClipPosition = DirectX::XMVector4Transform(DirectX::XMVectorSetW(InWorldPosition, 1.0f), InViewProjection);
        const float ClipW = DirectX::XMVectorGetW(ClipPosition);
        if (ClipW <= 0.0f)
        {
            return false;
        }

        const float NdcX = DirectX::XMVectorGetX(ClipPosition) / ClipW;
        const float NdcY = DirectX::XMVectorGetY(ClipPosition) / ClipW;
        OutScreenPosition.x = static_cast<float>(InViewportRect.X) + (NdcX * 0.5f + 0.5f) * static_cast<float>(InViewportRect.Width);
        OutScreenPosition.y = static_cast<float>(InViewportRect.Y) + (1.0f - (NdcY * 0.5f + 0.5f)) * static_cast<float>(InViewportRect.Height);
        return true;
    }

    inline float ComputeWorldUnitsPerPixel(const DirectX::XMVECTOR& InWorldPosition, const Graphics::FCameraState& InCameraState, int InViewportHeight)
    {
        const DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&InCameraState.Position);
        const DirectX::XMVECTOR CameraToPoint = DirectX::XMVectorSubtract(InWorldPosition, CameraPosition);
        const float Distance = (std::max)(1.0f, DirectX::XMVectorGetX(DirectX::XMVector3Length(CameraToPoint)));
        const float HalfFovRadians = DirectX::XMConvertToRadians(InCameraState.FOVDegrees) * 0.5f;
        const float VisibleHeight = 2.0f * Distance * std::tan(HalfFovRadians);
        return VisibleHeight / static_cast<float>((std::max)(InViewportHeight, 1));
    }

    inline DirectX::XMFLOAT3 QuaternionToEulerDegrees(const DirectX::XMVECTOR& InQuaternion)
    {
        const DirectX::XMFLOAT4 Quaternion = [] (const DirectX::XMVECTOR& InValue)
        {
            DirectX::XMFLOAT4 Result = {};
            DirectX::XMStoreFloat4(&Result, InValue);
            return Result;
        }(InQuaternion);

        const float SinPitch = 2.0f * (Quaternion.w * Quaternion.x - Quaternion.y * Quaternion.z);
        const float CosPitch = 1.0f - 2.0f * (Quaternion.x * Quaternion.x + Quaternion.y * Quaternion.y);
        const float Pitch = std::atan2(SinPitch, CosPitch);

        const float SinYaw = 2.0f * (Quaternion.w * Quaternion.y + Quaternion.z * Quaternion.x);
        float Yaw = 0.0f;
        if (std::abs(SinYaw) >= 1.0f)
        {
            Yaw = std::copysign(DirectX::XM_PIDIV2, SinYaw);
        }
        else
        {
            Yaw = std::asin(SinYaw);
        }

        const float SinRoll = 2.0f * (Quaternion.w * Quaternion.z - Quaternion.x * Quaternion.y);
        const float CosRoll = 1.0f - 2.0f * (Quaternion.y * Quaternion.y + Quaternion.z * Quaternion.z);
        const float Roll = std::atan2(SinRoll, CosRoll);

        return {
            DirectX::XMConvertToDegrees(Pitch),
            DirectX::XMConvertToDegrees(Yaw),
            DirectX::XMConvertToDegrees(Roll)
        };
    }

    inline DirectX::XMVECTOR EulerDegreesToQuaternion(const DirectX::XMFLOAT3& InEulerDegrees)
    {
        return DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(InEulerDegrees.x),
            DirectX::XMConvertToRadians(InEulerDegrees.y),
            DirectX::XMConvertToRadians(InEulerDegrees.z));
    }

    inline bool DecomposeTransformComponents(
        const DirectX::XMMATRIX& InWorldMatrix,
        DirectX::XMFLOAT3& OutTranslation,
        DirectX::XMVECTOR& OutRotationQuaternion,
        DirectX::XMFLOAT3& OutScale);

    inline bool DecomposeTransform(
        const DirectX::XMMATRIX& InWorldMatrix,
        DirectX::XMFLOAT3& OutTranslation,
        DirectX::XMFLOAT3& OutRotationDegrees,
        DirectX::XMFLOAT3& OutScale)
    {
        DirectX::XMVECTOR Rotation = {};
        if (!DecomposeTransformComponents(InWorldMatrix, OutTranslation, Rotation, OutScale))
        {
            return false;
        }

        OutRotationDegrees = QuaternionToEulerDegrees(Rotation);
        return true;
    }

    inline bool DecomposeTransformComponents(
        const DirectX::XMMATRIX& InWorldMatrix,
        DirectX::XMFLOAT3& OutTranslation,
        DirectX::XMVECTOR& OutRotationQuaternion,
        DirectX::XMFLOAT3& OutScale)
    {
        DirectX::XMVECTOR Scale = {};
        DirectX::XMVECTOR Rotation = {};
        DirectX::XMVECTOR Translation = {};
        if (!DirectX::XMMatrixDecompose(&Scale, &Rotation, &Translation, InWorldMatrix))
        {
            return false;
        }

        DirectX::XMStoreFloat3(&OutTranslation, Translation);
        DirectX::XMStoreFloat3(&OutScale, Scale);
        OutRotationQuaternion = DirectX::XMQuaternionNormalize(Rotation);
        return true;
    }

    inline DirectX::XMMATRIX ComposeTransformMatrix(
        const DirectX::XMFLOAT3& InTranslation,
        const DirectX::XMFLOAT3& InRotationDegrees,
        const DirectX::XMFLOAT3& InScale)
    {
        return DirectX::XMMatrixScaling(InScale.x, InScale.y, InScale.z) *
            DirectX::XMMatrixRotationQuaternion(EulerDegreesToQuaternion(InRotationDegrees)) *
            DirectX::XMMatrixTranslation(InTranslation.x, InTranslation.y, InTranslation.z);
    }

    inline DirectX::XMMATRIX ComposeTransformMatrix(
        const DirectX::XMFLOAT3& InTranslation,
        const DirectX::XMVECTOR& InRotationQuaternion,
        const DirectX::XMFLOAT3& InScale)
    {
        return DirectX::XMMatrixScaling(InScale.x, InScale.y, InScale.z) *
            DirectX::XMMatrixRotationQuaternion(DirectX::XMQuaternionNormalize(InRotationQuaternion)) *
            DirectX::XMMatrixTranslation(InTranslation.x, InTranslation.y, InTranslation.z);
    }

    inline Math::FBox TransformBounds(const Math::FBox& InLocalBounds, const DirectX::XMMATRIX& InWorldMatrix)
    {
        static constexpr std::array<DirectX::XMFLOAT3, 8> CornerMask = {
            DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f },
            DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f },
            DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f },
            DirectX::XMFLOAT3{ 1.0f, 1.0f, 0.0f },
            DirectX::XMFLOAT3{ 0.0f, 0.0f, 1.0f },
            DirectX::XMFLOAT3{ 1.0f, 0.0f, 1.0f },
            DirectX::XMFLOAT3{ 0.0f, 1.0f, 1.0f },
            DirectX::XMFLOAT3{ 1.0f, 1.0f, 1.0f }
        };

        Math::FBox WorldBounds = {};
        for (const DirectX::XMFLOAT3& Corner : CornerMask)
        {
            const DirectX::XMVECTOR LocalCorner = DirectX::XMVectorSet(
                Corner.x > 0.5f ? InLocalBounds.Max.x : InLocalBounds.Min.x,
                Corner.y > 0.5f ? InLocalBounds.Max.y : InLocalBounds.Min.y,
                Corner.z > 0.5f ? InLocalBounds.Max.z : InLocalBounds.Min.z,
                1.0f);

            DirectX::XMFLOAT3 WorldCorner = {};
            DirectX::XMStoreFloat3(&WorldCorner, DirectX::XMVector3TransformCoord(LocalCorner, InWorldMatrix));
            WorldBounds.Expand(WorldCorner);
        }

        return WorldBounds;
    }

    inline bool ApplyObjectWorldMatrix(
        Scene::USceneManager& InSceneManager,
        const Graphics::URenderer& InRenderer,
        uint32_t InObjectIndex,
        const DirectX::XMMATRIX& InWorldMatrix,
        bool bDeferSpatialRebuild = false)
    {
        Scene::FSceneDataSOA* SceneData = InSceneManager.GetSceneData();
        if (!SceneData || InObjectIndex >= SceneData->TotalObjectCount)
        {
            return false;
        }

        const uint32_t BaseMeshID = SceneData->BaseMeshIDs[InObjectIndex];
        const Graphics::URenderer::FMeshResource* MeshResource = InRenderer.GetMeshResource(BaseMeshID);

        Math::FBox LocalBounds = {};
        float LocalRadius = 0.5f;
        if (MeshResource)
        {
            LocalBounds = MeshResource->LocalAABB;
            LocalRadius = MeshResource->LocalRadius;
        }
        else
        {
            LocalBounds.Min = { -0.5f, -0.5f, -0.5f };
            LocalBounds.Max = { 0.5f, 0.5f, 0.5f };
        }

        const Math::FBox WorldBounds = TransformBounds(LocalBounds, InWorldMatrix);
        SceneData->WorldMatrices[InObjectIndex].Store(InWorldMatrix);
        SceneData->MinX[InObjectIndex] = WorldBounds.Min.x;
        SceneData->MinY[InObjectIndex] = WorldBounds.Min.y;
        SceneData->MinZ[InObjectIndex] = WorldBounds.Min.z;
        SceneData->MaxX[InObjectIndex] = WorldBounds.Max.x;
        SceneData->MaxY[InObjectIndex] = WorldBounds.Max.y;
        SceneData->MaxZ[InObjectIndex] = WorldBounds.Max.z;
        SceneData->MeshIDs[InObjectIndex] = SceneData->BaseMeshIDs[InObjectIndex];
        SceneData->IsVisible[InObjectIndex] = true;

        DirectX::XMVECTOR Scale = {};
        DirectX::XMVECTOR Rotation = {};
        DirectX::XMVECTOR Translation = {};
        if (DirectX::XMMatrixDecompose(&Scale, &Rotation, &Translation, InWorldMatrix))
        {
            SceneData->CenterX[InObjectIndex] = DirectX::XMVectorGetX(Translation);
            SceneData->CenterY[InObjectIndex] = DirectX::XMVectorGetY(Translation);
            SceneData->CenterZ[InObjectIndex] = DirectX::XMVectorGetZ(Translation);

            const float MaxScale = (std::max)({
                std::abs(DirectX::XMVectorGetX(Scale)),
                std::abs(DirectX::XMVectorGetY(Scale)),
                std::abs(DirectX::XMVectorGetZ(Scale))
            });
            SceneData->Radius[InObjectIndex] = LocalRadius * MaxScale;
        }

        // [최적화] 기즈모 드래그 중에는 매 프레임 전체 공간 구조를 재구축하지 않는다.
        // bDeferSpatialRebuild=true 시 드래그 종료(EndGizmoDrag) 시점에 한 번만 재구축한다.
        if (!bDeferSpatialRebuild)
        {
            InSceneManager.RebuildCentersAndRadii();
            if (Scene::UUniformGrid* Grid = InSceneManager.GetGrid())
            {
                Grid->BuildGrid();
            }
            InSceneManager.BuildSceneBVH();
        }

        if (InSceneManager.IsObjectSelected(InObjectIndex))
        {
            InSceneManager.RefreshSelectionMetadata();
        }

        return true;
    }

    inline bool ApplyObjectTransform(
        Scene::USceneManager& InSceneManager,
        const Graphics::URenderer& InRenderer,
        uint32_t InObjectIndex,
        const DirectX::XMFLOAT3& InTranslation,
        const DirectX::XMVECTOR& InRotationQuaternion,
        const DirectX::XMFLOAT3& InScale,
        bool bDeferSpatialRebuild = false)
    {
        Scene::FSceneDataSOA* SceneData = InSceneManager.GetSceneData();
        if (!SceneData || InObjectIndex >= SceneData->TotalObjectCount)
        {
            return false;
        }

        SceneData->Scale3D[InObjectIndex] = InScale;
        DirectX::XMStoreFloat4(&SceneData->RotationQuaternions[InObjectIndex], DirectX::XMQuaternionNormalize(InRotationQuaternion));

        const DirectX::XMMATRIX WorldMatrix = ComposeTransformMatrix(InTranslation, InRotationQuaternion, InScale);
        return ApplyObjectWorldMatrix(InSceneManager, InRenderer, InObjectIndex, WorldMatrix, bDeferSpatialRebuild);
    }
}
