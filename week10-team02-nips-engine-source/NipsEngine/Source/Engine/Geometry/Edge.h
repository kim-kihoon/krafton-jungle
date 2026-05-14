#pragma once

#include "Core/CoreTypes.h"
#include "Math/Utils.h"
#include "Math/Vector.h"
#include <functional>

// 두 FVector 정점으로 구성된 간선(Edge)을 표현하는 자료형
// {A, B}와 {B, A}는 동일한 간선으로 취급됨 (비방향 간선)
struct FEdge
{
public:
    FVector A;
    FVector B;

    //======================================//
    //				constructor				//
    //======================================//
public:
    constexpr FEdge() noexcept : A(), B() {}

    constexpr FEdge(const FVector& InA, const FVector& InB) noexcept
        : A(InA), B(InB)
    {
    }

    FEdge(const FEdge&) noexcept = default;
    FEdge(FEdge&&) noexcept = default;

    //======================================//
    //				operators				//
    //======================================//
public:
    FEdge& operator=(const FEdge&) noexcept = default;
    FEdge& operator=(FEdge&&) noexcept = default;

    // 비방향 간선: {A,B} == {B,A}
    bool operator==(const FEdge& Other) const noexcept
    {
        return (A == Other.A && B == Other.B)
            || (A == Other.B && B == Other.A);
    }

    bool operator!=(const FEdge& Other) const noexcept
    {
        return !(*this == Other);
    }

    //======================================//
    //				  method				//
    //======================================//
public:
    // 간선의 중간 지점을 반환함
    FVector Midpoint() const noexcept
    {
        return (A + B) * 0.5f;
    }

    // 간선의 길이를 반환함
    float Length() const noexcept
    {
        return FVector::Dist(A, B);
    }

    // 간선의 길이 제곱을 반환함
    float LengthSquared() const noexcept
    {
        return FVector::DistSquared(A, B);
    }

    // 주어진 점에서 간선 위의 가장 가까운 점을 반환함
    FVector ClosestPoint(const FVector& Point) const noexcept
    {
        const FVector Segment = B - A;
        const float SegmentLengthSquared = Segment.SizeSquared();
        if (SegmentLengthSquared <= MathUtil::Epsilon)
        {
            return A;
        }

        const float T = MathUtil::Clamp(FVector::DotProduct(Point - A, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
        return A + Segment * T;
    }

    // 두 간선 사이의 가장 가까운 점 쌍을 반환함
    static void ClosestPoints(const FEdge& EdgeA, const FEdge& EdgeB, FVector& OutA, FVector& OutB) noexcept
    {
        const FVector D1 = EdgeA.B - EdgeA.A;
        const FVector D2 = EdgeB.B - EdgeB.A;
        const FVector R = EdgeA.A - EdgeB.A;
        const float A = FVector::DotProduct(D1, D1);
        const float E = FVector::DotProduct(D2, D2);
        const float F = FVector::DotProduct(D2, R);

        float S = 0.0f;
        float T = 0.0f;

        if (A <= MathUtil::Epsilon && E <= MathUtil::Epsilon)
        {
            OutA = EdgeA.A;
            OutB = EdgeB.A;
            return;
        }

        if (A <= MathUtil::Epsilon)
        {
            T = MathUtil::Clamp(F / E, 0.0f, 1.0f);
        }
        else
        {
            const float C = FVector::DotProduct(D1, R);
            if (E <= MathUtil::Epsilon)
            {
                S = MathUtil::Clamp(-C / A, 0.0f, 1.0f);
            }
            else
            {
                const float B = FVector::DotProduct(D1, D2);
                const float Denom = A * E - B * B;
                if (Denom != 0.0f)
                {
                    S = MathUtil::Clamp((B * F - C * E) / Denom, 0.0f, 1.0f);
                }

                T = (B * S + F) / E;
                if (T < 0.0f)
                {
                    T = 0.0f;
                    S = MathUtil::Clamp(-C / A, 0.0f, 1.0f);
                }
                else if (T > 1.0f)
                {
                    T = 1.0f;
                    S = MathUtil::Clamp((B - C) / A, 0.0f, 1.0f);
                }
            }
        }

        OutA = EdgeA.A + D1 * S;
        OutB = EdgeB.A + D2 * T;
    }

    // 두 정점 중 더 작은 쪽이 A가 되도록 정규화된 복사본을 반환함
    FEdge Canonical() const noexcept
    {
        auto Less = [](const FVector& P, const FVector& Q) -> bool
        {
            if (P.X != Q.X) return P.X < Q.X;
            if (P.Y != Q.Y) return P.Y < Q.Y;
            return P.Z < Q.Z;
        };
        return Less(A, B) ? FEdge(A, B) : FEdge(B, A);
    }
};

namespace std
{
    template <>
    struct hash<FEdge>
    {
        size_t operator()(const FEdge& Edge) const noexcept
        {
            // 비방향 간선이므로 Canonical 형태로 정규화 후 해싱
            FEdge C = Edge.Canonical();
            auto Combine = [](size_t Seed, size_t Value) -> size_t
            {
                return Seed ^ (Value * 2654435761u + 0x9e3779b9u + (Seed << 6) + (Seed >> 2));
            };
            size_t H = std::hash<FVector>{}(C.A);
            H = Combine(H, std::hash<FVector>{}(C.B));
            return H;
        }
    };
}

// 두 정점의 인덱스(Index)로 구성된 간선(Edge)을 표현하는 자료형
// (A, B)와 (B, A)는 동일한 간선으로 취급됨 (비방향 간선)
struct FIndexEdge
{
public:
    uint32 A;
    uint32 B;

    //======================================//
    //                constructor           //
    //======================================//
public:
    constexpr FIndexEdge() noexcept : A(0), B(0) {}

    constexpr FIndexEdge(uint32 InA, uint32 InB) noexcept
        : A(InA), B(InB)
    {
    }

    FIndexEdge(const FIndexEdge&) noexcept = default;
    FIndexEdge(FIndexEdge&&) noexcept = default;

    //======================================//
    //                operators             //
    //======================================//
public:
    FIndexEdge& operator=(const FIndexEdge&) noexcept = default;
    FIndexEdge& operator=(FIndexEdge&&) noexcept = default;

    // 비방향 간선: {A,B} == {B,A}
    bool operator==(const FIndexEdge& Other) const noexcept
    {
        return (A == Other.A && B == Other.B)
            || (A == Other.B && B == Other.A);
    }

    bool operator!=(const FIndexEdge& Other) const noexcept
    {
        return !(*this == Other);
    }

    //======================================//
    //                 method               //
    //======================================//
public:
    // 두 인덱스 중 더 작은 쪽이 A가 되도록 정규화된 복사본을 반환함
    FIndexEdge Canonical() const noexcept
    {
        return A < B ? FIndexEdge(A, B) : FIndexEdge(B, A);
    }
};

namespace std
{
    template <>
    struct hash<FIndexEdge>
    {
        size_t operator()(const FIndexEdge& Edge) const noexcept
        {
            // 비방향 간선이므로 Canonical 형태로 정규화 후 해싱
            FIndexEdge C = Edge.Canonical();
            auto Combine = [](size_t Seed, size_t Value) -> size_t
            {
                return Seed ^ (Value * 2654435761u + 0x9e3779b9u + (Seed << 6) + (Seed >> 2));
            };
            
            size_t H = std::hash<uint32>{}(C.A);
            H = Combine(H, std::hash<uint32>{}(C.B));
            return H;
        }
    };
}
