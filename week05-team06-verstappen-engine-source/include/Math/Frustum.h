#pragma once
#include <Math/MathTypes.h>
#include <DirectXCollision.h>
#include <algorithm> // std::max, std::min 사용을 위함

namespace Math
{
    enum class ECullingResult
    {
        Outside = 0,
        Intersecting = 1,
        FullyInside = 2
    };

    struct alignas(16) FFrustum
    {
        DirectX::BoundingFrustum NativeFrustum;

        FVector4 Planes[6];
        FVector Corners[8];
        XMFLOAT3 AABBMin;
        XMFLOAT3 AABBMax;

        // AVX2 최적화를 위한 평면별 컴포넌트 분리 저장
        float SIMDPlanesX[6];
        float SIMDPlanesY[6];
        float SIMDPlanesZ[6];
        float SIMDPlanesW[6];

        inline void Update(const FMatrix& View, const FMatrix& Projection)
        {
            BuildFromViewProjection(View, Projection);
            BuildFromMatrix(XMMatrixMultiply(View, Projection));
        }

        inline void BuildFromViewProjection(const FMatrix& View, const FMatrix& Projection)
        {
            DirectX::BoundingFrustum LocalFrustum;
            DirectX::BoundingFrustum::CreateFromMatrix(LocalFrustum, Projection);

            const FMatrix InverseView = XMMatrixInverse(nullptr, View);
            LocalFrustum.Transform(NativeFrustum, InverseView);
        }

        inline void BuildFromMatrix(const FMatrix& ViewProj)
        {
            FMatrix T = XMMatrixTranspose(ViewProj);
            FVector C0 = T.r[0]; FVector C1 = T.r[1]; FVector C2 = T.r[2]; FVector C3 = T.r[3];

            auto Extract = [&](int i, XMVECTOR p) {
                XMVECTOR n = XMPlaneNormalize(p);
                SIMDPlanesX[i] = XMVectorGetX(n);
                SIMDPlanesY[i] = XMVectorGetY(n);
                SIMDPlanesZ[i] = XMVectorGetZ(n);
                SIMDPlanesW[i] = XMVectorGetW(n);
                Planes[i] = n;
            };

            Extract(0, XMVectorAdd(C3, C0));      // Left
            Extract(1, XMVectorSubtract(C3, C0)); // Right
            Extract(2, XMVectorAdd(C3, C1));      // Bottom
            Extract(3, XMVectorSubtract(C3, C1)); // Top
            Extract(4, C2);                       // Near
            Extract(5, XMVectorSubtract(C3, C2)); // Far

            FMatrix InvVP = XMMatrixInverse(nullptr, ViewProj);
            FVector NDC[8] = {
                XMVectorSet(-1,-1,0,1), XMVectorSet(1,-1,0,1), XMVectorSet(-1,1,0,1), XMVectorSet(1,1,0,1),
                XMVectorSet(-1,-1,1,1), XMVectorSet(1,-1,1,1), XMVectorSet(-1,1,1,1), XMVectorSet(1,1,1,1)
            };
            FVector vMin = XMVectorReplicate(FLT_MAX), vMax = XMVectorReplicate(-FLT_MAX);
            for (int i = 0; i < 8; i++) {
                Corners[i] = XMVector3TransformCoord(NDC[i], InvVP);
                vMin = XMVectorMin(vMin, Corners[i]); vMax = XMVectorMax(vMax, Corners[i]);
            }
            FVector vPad = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
            XMStoreFloat3(&AABBMin, XMVectorSubtract(vMin, vPad));
            XMStoreFloat3(&AABBMax, XMVectorAdd(vMax, vPad));
        }

        //// SoA Min/Max 데이터를 DirectX API에 맞게 Center/Extents로 변환 -> SIMD계산에 좋지는 않지만 나쁘지 않다고 함. 
        //// 원래 방식도 해볼것을 고려해봐야 할듯. 추후에 CPU를 극한으로 끌어올리겠다면.
        //inline ECullingResult TestBox(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ) const
        //{
        //    // SoA Min/Max 데이터를 DirectX API에 맞게 Center/Extents로 변환
        //    const DirectX::XMFLOAT3 Center = {
        //        (MinX + MaxX) * 0.5f,
        //        (MinY + MaxY) * 0.5f,
        //        (MinZ + MaxZ) * 0.5f
        //    };
        //    const DirectX::XMFLOAT3 Extents = {
        //        (MaxX - MinX) * 0.5f,
        //        (MaxY - MinY) * 0.5f,
        //        (MaxZ - MinZ) * 0.5f
        //    };

        //    const DirectX::BoundingBox Box(Center, Extents);
        //    const DirectX::ContainmentType Result = NativeFrustum.Contains(Box);

        //    if (Result == DirectX::DISJOINT) return ECullingResult::Outside;
        //    if (Result == DirectX::CONTAINS) return ECullingResult::FullyInside;
        //    return ECullingResult::Intersecting;
        //}

        inline ECullingResult TestBox(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ) const
        {
            bool bFullyInside = true;

            for (int i = 0; i < 6; ++i)
            {
                // plane 식 값이 최대가 되는 점
                const float px = (SIMDPlanesX[i] >= 0.0f) ? MaxX : MinX;
                const float py = (SIMDPlanesY[i] >= 0.0f) ? MaxY : MinY;
                const float pz = (SIMDPlanesZ[i] >= 0.0f) ? MaxZ : MinZ;

                // plane 식 값이 최소가 되는 점
                const float nx = (SIMDPlanesX[i] >= 0.0f) ? MinX : MaxX;
                const float ny = (SIMDPlanesY[i] >= 0.0f) ? MinY : MaxY;
                const float nz = (SIMDPlanesZ[i] >= 0.0f) ? MinZ : MaxZ;

                // 최대가 되는 점 조차 < 0 -> 박스 전체가 평면 바깥
                if (SIMDPlanesX[i] * px + SIMDPlanesY[i] * py + SIMDPlanesZ[i] * pz + SIMDPlanesW[i] < 0.0f)
                    return ECullingResult::Outside;

                // 최소가 되는 점 < 0 -> 완전히 inside는 아님. 완전히 outside도 아님 -> Intersecting
                if (SIMDPlanesX[i] * nx + SIMDPlanesY[i] * ny + SIMDPlanesZ[i] * nz + SIMDPlanesW[i] < 0.0f)
                    bFullyInside = false;
            }

            return bFullyInside ? ECullingResult::FullyInside : ECullingResult::Intersecting;
        }

        inline ECullingResult TestBox(const FBox& Box) const
        {
            return TestBox(Box.Min.x, Box.Min.y, Box.Min.z, Box.Max.x, Box.Max.y, Box.Max.z);
        }

        inline Math::ECullingResult TestSphere(float CenterX, float CenterY, float CenterZ, float Radius) const
        {
            const DirectX::BoundingSphere Sphere(DirectX::XMFLOAT3(CenterX, CenterY, CenterZ), Radius);
            const DirectX::ContainmentType Result = NativeFrustum.Contains(Sphere);

            if (Result == DirectX::DISJOINT) return ECullingResult::Outside;
            if (Result == DirectX::CONTAINS) return ECullingResult::FullyInside;
            return ECullingResult::Intersecting;
        }
    };
}
