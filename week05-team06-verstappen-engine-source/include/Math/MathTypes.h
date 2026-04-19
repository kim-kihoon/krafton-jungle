#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <cstdint>

namespace Math
{
    using namespace DirectX;

    using FVector = XMVECTOR;
    using FVector4 = XMVECTOR;
    using FMatrix = XMMATRIX;

    /**
     * [전략 반영] GPU 대역폭 25% 절감을 위한 3x4 압축 행렬.
     */
    struct alignas(16) FPacked3x4Matrix
    {
        FVector Row0;
        FVector Row1;
        FVector Row2;

        inline void Store(const FMatrix& InMatrix)
        {
            FMatrix Transposed = XMMatrixTranspose(InMatrix);
            Row0 = Transposed.r[0];
            Row1 = Transposed.r[1];
            Row2 = Transposed.r[2];
        }
    };

    /**
     * [전략 반영] SIMD Culling에 최적화된 Min/Max 방식의 AABB.
     * SoA 구조로 풀어서 관리하기 전, 데이터 전달용으로 사용.
     */
    struct FBox
    {
        XMFLOAT3 Min;
        XMFLOAT3 Max;

        FBox() : Min(FLT_MAX, FLT_MAX, FLT_MAX), Max(-FLT_MAX, -FLT_MAX, -FLT_MAX) {}
        FBox(const XMFLOAT3& InMin, const XMFLOAT3& InMax) : Min(InMin), Max(InMax) { }
        void Expand(const XMFLOAT3& Point)
        {
            Min.x = (std::min)(Min.x, Point.x);
            Min.y = (std::min)(Min.y, Point.y);
            Min.z = (std::min)(Min.z, Point.z);
            Max.x = (std::max)(Max.x, Point.x);
            Max.y = (std::max)(Max.y, Point.y);
            Max.z = (std::max)(Max.z, Point.z);
        }

        void Expand(const FBox& Other)
        {
            Min.x = (std::min)(Min.x, Other.Min.x);
            Min.y = (std::min)(Min.y, Other.Min.y);
            Min.z = (std::min)(Min.z, Other.Min.z);
            Max.x = (std::max)(Max.x, Other.Max.x);
            Max.y = (std::max)(Max.y, Other.Max.y);
            Max.z = (std::max)(Max.z, Other.Max.z);
        }
    };

	struct FRay
	{
		XMFLOAT3 Origin;
		XMFLOAT3 Direction;
		XMFLOAT3 InvDirection; // [최적화] 나눗셈을 곱셈으로 바꾸기 위한 역수

		FRay(const XMFLOAT3& InOrigin, const XMFLOAT3& InDirection)
		{
			Origin = InOrigin;

			// 방향 벡터 정규화(길이 1) 및 역수 캐싱
			XMVECTOR Dir = XMVector3Normalize(XMLoadFloat3(&InDirection));
			XMStoreFloat3(&Direction, Dir);

			// 0으로 나누기 방지를 위해 아주 작은 값(epsilon) 추가
			InvDirection.x = 1.0f / (Direction.x != 0.0f ? Direction.x : 0.000001f);
			InvDirection.y = 1.0f / (Direction.y != 0.0f ? Direction.y : 0.000001f);
			InvDirection.z = 1.0f / (Direction.z != 0.0f ? Direction.z : 0.000001f);
		}

        bool Intersects(const FBox& Box, float& OutT) const
        {
            float t1 = (Box.Min.x - Origin.x) * InvDirection.x;
            float t2 = (Box.Max.x - Origin.x) * InvDirection.x;
            float t3 = (Box.Min.y - Origin.y) * InvDirection.y;
            float t4 = (Box.Max.y - Origin.y) * InvDirection.y;
            float t5 = (Box.Min.z - Origin.z) * InvDirection.z;
            float t6 = (Box.Max.z - Origin.z) * InvDirection.z;

            float tmin = (std::max)((std::max)((std::min)(t1, t2), (std::min)(t3, t4)), (std::min)(t5, t6));
            float tmax = (std::min)((std::min)((std::max)(t1, t2), (std::max)(t3, t4)), (std::max)(t5, t6));

            if (tmax < 0 || tmin > tmax) return false;
            
            OutT = (tmin < 0.0f) ? 0.0f : tmin;
            return true;
        }
	};
}
