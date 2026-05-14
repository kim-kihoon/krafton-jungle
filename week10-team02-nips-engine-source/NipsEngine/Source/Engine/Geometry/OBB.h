#pragma once

#include "Core/Containers/Array.h"
#include "Geometry/AABB.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

struct FOBB
{
    FVector Center;
    FVector Extents;
    FQuat Rotation;

    FOBB();
    FOBB(const FVector& InCenter, const FVector& InExtents, const FQuat& InRotation);
    FOBB(const FVector& InCenter, const FVector& InExtents, const FMatrix& InMatrix);

    void Reset();
    bool IsValid() const;

    static FOBB FromAABB(const FAABB& InAABB, const FMatrix& InTransform);

    inline void GetAxes(FVector& OutX, FVector& OutY, FVector& OutZ) const;
    inline void GetVertices(TArray<FVector>& OutVertices) const;
    inline FMatrix GetTransform() const;

    inline bool Contains(const FVector& Point) const;
    FVector ClosestPoint(const FVector& Point) const;

    inline bool Intersects(const FAABB& AABB) const;
    inline bool Intersects(const FOBB& OBB) const;
};

inline void FOBB::GetAxes(FVector& OutX, FVector& OutY, FVector& OutZ) const
{
    const FMatrix RotMat = Rotation.ToMatrix();
    OutX = RotMat.GetScaledAxis(EAxis::X);
    OutY = RotMat.GetScaledAxis(EAxis::Y);
    OutZ = RotMat.GetScaledAxis(EAxis::Z);
}

inline void FOBB::GetVertices(TArray<FVector>& OutVertices) const
{
    FVector X, Y, Z;
    GetAxes(X, Y, Z);

    X *= Extents.X;
    Y *= Extents.Y;
    Z *= Extents.Z;

    OutVertices.resize(8);
    OutVertices[0] = Center - X - Y - Z;
    OutVertices[1] = Center + X - Y - Z;
    OutVertices[2] = Center - X + Y - Z;
    OutVertices[3] = Center + X + Y - Z;
    OutVertices[4] = Center - X - Y + Z;
    OutVertices[5] = Center + X - Y + Z;
    OutVertices[6] = Center - X + Y + Z;
    OutVertices[7] = Center + X + Y + Z;
}

inline FMatrix FOBB::GetTransform() const
{
    const FMatrix RotMat = Rotation.ToMatrix();
    FMatrix Transform = RotMat;
    Transform.SetOrigin(Center);
    return Transform;
}

inline bool FOBB::Contains(const FVector& Point) const
{
    FVector LocalPoint = Rotation.Inverse() * (Point - Center);
    return MathUtil::Abs(LocalPoint.X) <= Extents.X && MathUtil::Abs(LocalPoint.Y) <= Extents.Y &&
        MathUtil::Abs(LocalPoint.Z) <= Extents.Z;
}

inline FVector FOBB::ClosestPoint(const FVector& Point) const
{
    FVector AxisX, AxisY, AxisZ;
    GetAxes(AxisX, AxisY, AxisZ);

    const FVector Axes[3] = { AxisX, AxisY, AxisZ };
    FVector Result = Center;
    const FVector Delta = Point - Center;

    for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
    {
        const float Distance = FVector::DotProduct(Delta, Axes[AxisIndex]);
        const float ClampedDistance = std::max(-Extents[AxisIndex], std::min(Distance, Extents[AxisIndex]));
        Result += Axes[AxisIndex] * ClampedDistance;
    }

    return Result;
}

inline bool FOBB::Intersects(const FAABB& AABB) const
{
    // Separating Axis Theorem (SAT) for OBB-AABB intersection
    FVector OBBAxes[3]; GetAxes(OBBAxes[0], OBBAxes[1], OBBAxes[2]);
    FVector AABBAxes[3] = { FVector::UnitX(), FVector::UnitY(), FVector::UnitZ() };
    
    FVector OBBExtents = Extents;
    FVector AABBExtents = AABB.GetExtent();

    FVector T = Center - AABB.GetCenter();

    // AABB axes
    for (int i = 0; i < 3; ++i)
    {
        float rA = AABBExtents[i];
        float rB = OBBExtents.X * MathUtil::Abs(OBBAxes[0][i]) +
            OBBExtents.Y * MathUtil::Abs(OBBAxes[1][i]) +
            OBBExtents.Z * MathUtil::Abs(OBBAxes[2][i]);
        if (MathUtil::Abs(T[i]) > rA + rB) return false;
    }

    // OBB axes
    for (int i = 0; i < 3; ++i)
    {
        float rA = AABBExtents.X * MathUtil::Abs(OBBAxes[i].X) +
            AABBExtents.Y * MathUtil::Abs(OBBAxes[i].Y) +
            AABBExtents.Z * MathUtil::Abs(OBBAxes[i].Z);
        float rB = OBBExtents[i];
        if (MathUtil::Abs(T.DotProduct(OBBAxes[i])) > rA + rB) return false; 
    }

    // AABB cross OBB axes
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            FVector L;
            if (i == 0) L = FVector(0, -OBBAxes[j].Z, OBBAxes[j].Y);
            else if (i == 1) L = FVector(OBBAxes[j].Z, 0, -OBBAxes[j].X);
            else L = FVector(-OBBAxes[j].Y, OBBAxes[j].X, 0);

            if (L.SizeSquared() > MathUtil::Epsilon)
            {
                float rA = AABBExtents[(i + 1) % 3] * MathUtil::Abs(L[(i + 1) % 3]) +
                    AABBExtents[(i + 2) % 3] * MathUtil::Abs(L[(i + 2) % 3]);

                float rB = OBBExtents.X * MathUtil::Abs(L.DotProduct(OBBAxes[0])) +
                    OBBExtents.Y * MathUtil::Abs(L.DotProduct(OBBAxes[1])) +
                    OBBExtents.Z * MathUtil::Abs(L.DotProduct(OBBAxes[2]));

                if (MathUtil::Abs(T.DotProduct(L)) > rA + rB) return false;
            }
        }
    }

    return true;
}

inline bool FOBB::Intersects(const FOBB& Other) const
{
    // SAT: 두 박스 축 간 내적 R[i][j] = AxesA[i]·AxesB[j]를 미리 캐싱해 face 6축 + edge cross 9축의 모든 투영을 R/AbsR/Ta만으로 표현한다.
    FVector AxesA[3], AxesB[3];
    GetAxes(AxesA[0], AxesA[1], AxesA[2]);
    Other.GetAxes(AxesB[0], AxesB[1], AxesB[2]);

    float R[3][3];
    float AbsR[3][3];
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            R[i][j] = AxesA[i].DotProduct(AxesB[j]);
            AbsR[i][j] = MathUtil::Abs(R[i][j]) + MathUtil::Epsilon;
        }
    }

    const FVector T = Other.Center - Center;
    const float Ta[3] = { T.DotProduct(AxesA[0]), T.DotProduct(AxesA[1]), T.DotProduct(AxesA[2]) };

    const FVector& Ea = Extents;
    const FVector& Eb = Other.Extents;

    // L = AxesA[i] (face axes of A)
    for (int i = 0; i < 3; ++i)
    {
        const float rA = Ea[i];
        const float rB = Eb.X * AbsR[i][0] + Eb.Y * AbsR[i][1] + Eb.Z * AbsR[i][2];
        if (MathUtil::Abs(Ta[i]) > rA + rB) return false;
    }

    // L = AxesB[j] (face axes of B); T·AxesB[j] = Σ Ta[k]·R[k][j]
    for (int j = 0; j < 3; ++j)
    {
        const float rA = Ea.X * AbsR[0][j] + Ea.Y * AbsR[1][j] + Ea.Z * AbsR[2][j];
        const float rB = Eb[j];
        const float Tb = Ta[0] * R[0][j] + Ta[1] * R[1][j] + Ta[2] * R[2][j];
        if (MathUtil::Abs(Tb) > rA + rB) return false;
    }

    // L = AxesA[i] × AxesB[j] (edge cross axes, 9개)
    if (MathUtil::Abs(Ta[2] * R[1][0] - Ta[1] * R[2][0]) > Ea.Y * AbsR[2][0] + Ea.Z * AbsR[1][0] + Eb.Y * AbsR[0][2] + Eb.Z * AbsR[0][1]) return false; 
    if (MathUtil::Abs(Ta[2] * R[1][1] - Ta[1] * R[2][1]) > Ea.Y * AbsR[2][1] + Ea.Z * AbsR[1][1] + Eb.X * AbsR[0][2] + Eb.Z * AbsR[0][0]) return false; 
    if (MathUtil::Abs(Ta[2] * R[1][2] - Ta[1] * R[2][2]) > Ea.Y * AbsR[2][2] + Ea.Z * AbsR[1][2] + Eb.X * AbsR[0][1] + Eb.Y * AbsR[0][0]) return false;
    if (MathUtil::Abs(Ta[0] * R[2][0] - Ta[2] * R[0][0]) > Ea.X * AbsR[2][0] + Ea.Z * AbsR[0][0] + Eb.Y * AbsR[1][2] + Eb.Z * AbsR[1][1]) return false;
    if (MathUtil::Abs(Ta[0] * R[2][1] - Ta[2] * R[0][1]) > Ea.X * AbsR[2][1] + Ea.Z * AbsR[0][1] + Eb.X * AbsR[1][2] + Eb.Z * AbsR[1][0]) return false;
    if (MathUtil::Abs(Ta[0] * R[2][2] - Ta[2] * R[0][2]) > Ea.X * AbsR[2][2] + Ea.Z * AbsR[0][2] + Eb.X * AbsR[1][1] + Eb.Y * AbsR[1][0]) return false;
    if (MathUtil::Abs(Ta[1] * R[0][0] - Ta[0] * R[1][0]) > Ea.X * AbsR[1][0] + Ea.Y * AbsR[0][0] + Eb.Y * AbsR[2][2] + Eb.Z * AbsR[2][1]) return false;
    if (MathUtil::Abs(Ta[1] * R[0][1] - Ta[0] * R[1][1]) > Ea.X * AbsR[1][1] + Ea.Y * AbsR[0][1] + Eb.X * AbsR[2][2] + Eb.Z * AbsR[2][0]) return false;
    if (MathUtil::Abs(Ta[1] * R[0][2] - Ta[0] * R[1][2]) > Ea.X * AbsR[1][2] + Ea.Y * AbsR[0][2] + Eb.X * AbsR[2][1] + Eb.Y * AbsR[2][0]) return false;

    return true;
}
