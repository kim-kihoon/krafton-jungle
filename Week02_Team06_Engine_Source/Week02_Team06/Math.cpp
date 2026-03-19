#include "pch.h"

bool Math::RayIntersectsTriangle(const FVector& RayOrigin, const FVector& RayDir, const FVector& V0, const FVector& V1, const FVector& V2, float& OutT)
{
    FVector Edge1 = V1 - V0;
    FVector Edge2 = V2 - V0;
    FVector Normal = Edge1.Cross(Edge2);

    //if (Normal.Dot(RayDir) > 0) return false; // 후면 판정

    constexpr float epsilon = 1e-6f;
    FVector RayCrossE2 = RayDir.Cross(Edge2);
    float Det = Edge1.Dot(RayCrossE2);
    if (std::abs(Det) < epsilon) return false;

    float InvDet = 1.0f / Det;
    FVector S = RayOrigin - V0;
    float U = InvDet * S.Dot(RayCrossE2);
    if (U < 0.f || U > 1.f) return false;

    FVector SCrossE1 = S.Cross(Edge1);
    float V = InvDet * RayDir.Dot(SCrossE1);
    if (V < 0.f || U + V > 1.f) return false;

    float T = InvDet * Edge2.Dot(SCrossE1);

    if (T > epsilon) { OutT = T; return true; }
    return false;
}

FVector Math::RayPlaneIntersection(const FVector& RayOrigin, const FVector& RayDir, const FVector& PlaneNormal, const FVector& PlaneOrigin)
{
    float Denom = RayDir.Dot(PlaneNormal);
    if (std::abs(Denom) < 1e-6f) return PlaneOrigin;

    FVector Diff = PlaneOrigin - RayOrigin;
    float t = Diff.Dot(PlaneNormal) / Denom;
    return RayOrigin + (RayDir * t);
}

FVector Math::ClosestPointOnLine(const FVector& RayOrigin, const FVector& RayDir, const FVector& LineOrigin, const FVector& LineDir)
{
    FVector w0 = RayOrigin - LineOrigin;
    float a = RayDir.Dot(RayDir);
    float b = RayDir.Dot(LineDir);
    float c = LineDir.Dot(LineDir);
    float d = RayDir.Dot(w0);
    float e = LineDir.Dot(w0);

    float Denom = a * c - b * b;
    if (std::abs(Denom) < 1e-6f) return LineOrigin;

    float t2 = (a * e - b * d) / Denom;
    return LineOrigin + (LineDir * t2);
}

FVector Math::MatrixToEuler(const FMatrix& M)
{
    FVector Euler;

    // FMatrix::MakeRotation (Rx * Ry * Rz) 구조를 역산합니다.
    float sy = -M.M[0][2];

    // 짐벌락(Gimbal Lock) 판단 기준: Y축이 거의 90도나 -90도로 꺾였을 때
    bool bGimbalLock = (std::abs(sy) > 0.9999f);

    if (!bGimbalLock)
    {
        Euler.Y = asinf(sy);
        Euler.X = atan2f(M.M[1][2], M.M[2][2]);
        Euler.Z = atan2f(M.M[0][1], M.M[0][0]);
    }
    else
    {
        //짐벌락 발생 시: X를 0으로 고정하고 Z만으로 회전 상태를 표현
        Euler.X = 0.0f;
        Euler.Y = (sy > 0.0f) ? (PI / 2.0f) : (-PI / 2.0f);
        Euler.Z = atan2f(-M.M[1][0], M.M[1][1]);
    }

    // 라디안으로 계산된 값을 다시 우리가 쓰는 '디그리(Degree)'로 변환
    Euler.X = Math::ToDegrees(Euler.X);
    Euler.Y = Math::ToDegrees(Euler.Y);
    Euler.Z = Math::ToDegrees(Euler.Z);

    return Euler;
}

const FVector FVector::Zero = { 0.0f, 0.0f, 0.0f };

FMatrix::FMatrix(float _00, float _01, float _02, float _03,
                 float _10, float _11, float _12, float _13,
                 float _20, float _21, float _22, float _23,
                 float _30, float _31, float _32, float _33)
{
    M[0][0] = _00; M[0][1] = _01; M[0][2] = _02; M[0][3] = _03;
    M[1][0] = _10; M[1][1] = _11; M[1][2] = _12; M[1][3] = _13;
    M[2][0] = _20; M[2][1] = _21; M[2][2] = _22; M[2][3] = _23;
    M[3][0] = _30; M[3][1] = _31; M[3][2] = _32; M[3][3] = _33;
}

const FMatrix FMatrix::Identity = FMatrix
{
     1.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 1.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 1.0f, 0.0f,
     0.0f, 0.0f, 0.0f, 1.0f
};

FMatrix FMatrix::operator*(const FMatrix& Other) const
{
    FMatrix Result = {};
    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Col = 0; Col < 4; ++Col)
        {
            Result.M[Row][Col] =
                M[Row][0] * Other.M[0][Col] +
                M[Row][1] * Other.M[1][Col] +
                M[Row][2] * Other.M[2][Col] +
                M[Row][3] * Other.M[3][Col];
        }
    }
    return Result;
}

FMatrix FMatrix::Inverse() const
{
    FMatrix Result;
    float Det;

    // 각 원소에 대한 소행렬식(Cofactor)을 계산하여 수반 행렬(Adjugate) 생성
    Result.M[0][0] = M[1][1] * M[2][2] * M[3][3] - M[1][1] * M[2][3] * M[3][2] - M[2][1] * M[1][2] * M[3][3] + M[2][1] * M[1][3] * M[3][2] + M[3][1] * M[1][2] * M[2][3] - M[3][1] * M[1][3] * M[2][2];
    Result.M[1][0] = -M[1][0] * M[2][2] * M[3][3] + M[1][0] * M[2][3] * M[3][2] + M[2][0] * M[1][2] * M[3][3] - M[2][0] * M[1][3] * M[3][2] - M[3][0] * M[1][2] * M[2][3] + M[3][0] * M[1][3] * M[2][2];
    Result.M[2][0] = M[1][0] * M[2][1] * M[3][3] - M[1][0] * M[2][3] * M[3][1] - M[2][0] * M[1][1] * M[3][3] + M[2][0] * M[1][3] * M[3][1] + M[3][0] * M[1][1] * M[2][3] - M[3][0] * M[1][3] * M[2][1];
    Result.M[3][0] = -M[1][0] * M[2][1] * M[3][2] + M[1][0] * M[2][2] * M[3][1] + M[2][0] * M[1][1] * M[3][2] - M[2][0] * M[1][2] * M[3][1] - M[3][0] * M[1][1] * M[2][2] + M[3][0] * M[1][2] * M[2][1];

    Result.M[0][1] = -M[0][1] * M[2][2] * M[3][3] + M[0][1] * M[2][3] * M[3][2] + M[2][1] * M[0][2] * M[3][3] - M[2][1] * M[0][3] * M[3][2] - M[3][1] * M[0][2] * M[2][3] + M[3][1] * M[0][3] * M[2][2];
    Result.M[1][1] = M[0][0] * M[2][2] * M[3][3] - M[0][0] * M[2][3] * M[3][2] - M[2][0] * M[0][2] * M[3][3] + M[2][0] * M[0][3] * M[3][2] + M[3][0] * M[0][2] * M[2][3] - M[3][0] * M[0][3] * M[2][2];
    Result.M[2][1] = -M[0][0] * M[2][1] * M[3][3] + M[0][0] * M[2][3] * M[3][1] + M[2][0] * M[0][1] * M[3][3] - M[2][0] * M[0][3] * M[3][1] - M[3][0] * M[0][1] * M[2][3] + M[3][0] * M[0][3] * M[2][1];
    Result.M[3][1] = M[0][0] * M[2][1] * M[3][2] - M[0][0] * M[2][2] * M[3][1] - M[2][0] * M[0][1] * M[3][2] + M[2][0] * M[0][2] * M[3][1] + M[3][0] * M[0][1] * M[2][2] - M[3][0] * M[0][2] * M[2][1];

    Result.M[0][2] = M[0][1] * M[1][2] * M[3][3] - M[0][1] * M[1][3] * M[3][2] - M[1][1] * M[0][2] * M[3][3] + M[1][1] * M[0][3] * M[3][2] + M[3][1] * M[0][2] * M[1][3] - M[3][1] * M[0][3] * M[1][2];
    Result.M[1][2] = -M[0][0] * M[1][2] * M[3][3] + M[0][0] * M[1][3] * M[3][2] + M[1][0] * M[0][2] * M[3][3] - M[1][0] * M[0][3] * M[3][2] - M[3][0] * M[0][2] * M[1][3] + M[3][0] * M[0][3] * M[1][2];
    Result.M[2][2] = M[0][0] * M[1][1] * M[3][3] - M[0][0] * M[1][3] * M[3][1] - M[1][0] * M[0][1] * M[3][3] + M[1][0] * M[0][3] * M[3][1] + M[3][0] * M[0][1] * M[1][3] - M[3][0] * M[0][3] * M[1][1];
    Result.M[3][2] = -M[0][0] * M[1][1] * M[3][2] + M[0][0] * M[1][2] * M[3][1] + M[1][0] * M[0][1] * M[3][2] - M[1][0] * M[0][2] * M[3][1] - M[3][0] * M[0][1] * M[1][2] + M[3][0] * M[0][2] * M[1][1];

    Result.M[0][3] = -M[0][1] * M[1][2] * M[2][3] + M[0][1] * M[1][3] * M[2][2] + M[1][1] * M[0][2] * M[2][3] - M[1][1] * M[0][3] * M[2][2] - M[2][1] * M[0][2] * M[1][3] + M[2][1] * M[0][3] * M[1][2];
    Result.M[1][3] = M[0][0] * M[1][2] * M[2][3] - M[0][0] * M[1][3] * M[2][2] - M[1][0] * M[0][2] * M[2][3] + M[1][0] * M[0][3] * M[2][2] + M[2][0] * M[0][2] * M[1][3] - M[2][0] * M[0][3] * M[1][2];
    Result.M[2][3] = -M[0][0] * M[1][1] * M[2][3] + M[0][0] * M[1][3] * M[2][1] + M[1][0] * M[0][1] * M[2][3] - M[1][0] * M[0][3] * M[2][1] - M[2][0] * M[0][1] * M[1][3] + M[2][0] * M[0][3] * M[1][1];
    Result.M[3][3] = M[0][0] * M[1][1] * M[2][2] - M[0][0] * M[1][2] * M[2][1] - M[1][0] * M[0][1] * M[2][2] + M[1][0] * M[0][2] * M[2][1] + M[2][0] * M[0][1] * M[1][2] - M[2][0] * M[0][2] * M[1][1];

    // 행렬식(Determinant) 계산
    Det = M[0][0] * Result.M[0][0] + M[0][1] * Result.M[1][0] + M[0][2] * Result.M[2][0] + M[0][3] * Result.M[3][0];

    // 역행렬 존재 여부 확인
    if (fabs(Det) < 1e-6f)
    {
        return Identity;
    }

    // 행렬식의 역수를 모든 원소에 곱함
    float InvDet = 1.0f / Det;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            Result.M[i][j] *= InvDet;
        }
    }

    return Result;
}

FMatrix FMatrix::Transpose() const
{
    FMatrix Result;
    for (int Row = 0; Row < 4; ++Row)
    {
        for (int Col = 0; Col < 4; ++Col)
        {
            Result.M[Row][Col] = M[Col][Row];
        }
    }
    return Result;
}

FMatrix FMatrix::MakeScale(const FVector& S)
{  
    FMatrix Result;
    Result.M[0][0] = S.X;
    Result.M[1][1] = S.Y;
    Result.M[2][2] = S.Z;
    return Result;
}

FMatrix FMatrix::MakeRotationX(float Degree)
{
    FMatrix Result;
    float s = sinf(Math::ToRadians(Degree));
    float c = cosf(Math::ToRadians(Degree));

    Result.M[1][1] = c;  Result.M[1][2] = s;
    Result.M[2][1] = -s; Result.M[2][2] = c;
    return Result;
}

FMatrix FMatrix::MakeRotationY(float Degree)
{
    FMatrix Result;
    float s = sinf(Math::ToRadians(Degree));
    float c = cosf(Math::ToRadians(Degree));
    Result.M[0][0] = c;  Result.M[0][2] = -s;
    Result.M[2][0] = s;  Result.M[2][2] = c;
    return Result;
}

FMatrix FMatrix::MakeRotationZ(float Degree)
{
    FMatrix Result;
    float s = sinf(Math::ToRadians(Degree));
    float c = cosf(Math::ToRadians(Degree));
    Result.M[0][0] = c;  Result.M[0][1] = s;
    Result.M[1][0] = -s; Result.M[1][1] = c;
    return Result;
}

FMatrix FMatrix::MakeRotation(const FVector& Rotation)
{
    return FMatrix::MakeRotationX(Rotation.X) * FMatrix::MakeRotationY(Rotation.Y) * FMatrix::MakeRotationZ(Rotation.Z);
}

FMatrix FMatrix::MakeTranslation(const FVector& T)
{
    FMatrix Result;
    Result.M[3][0] = T.X;
    Result.M[3][1] = T.Y;
    Result.M[3][2] = T.Z;
    return Result;
}

FMatrix FMatrix::MakeLookAt(const FVector& Eye, const FVector& At, const FVector& Up)
{
    FVector ZAxis = (At - Eye); ZAxis.Normalize();
    FVector XAxis = Up.Cross(ZAxis); XAxis.Normalize();
    FVector YAxis = ZAxis.Cross(XAxis);

    return FMatrix(
        XAxis.X, YAxis.X, ZAxis.X, 0.0f,
        XAxis.Y, YAxis.Y, ZAxis.Y, 0.0f,
        XAxis.Z, YAxis.Z, ZAxis.Z, 0.0f,
        -XAxis.Dot(Eye), -YAxis.Dot(Eye), -ZAxis.Dot(Eye), 1.0f
    );
}

FMatrix FMatrix::MakePerspective(float FovAngleDeg, float AspectRatio, float NearZ, float FarZ)
{
    float SinFov = sinf(Math::ToRadians(FovAngleDeg) * 0.5f);
    float CosFov = cosf(Math::ToRadians(FovAngleDeg) * 0.5f);
    float h = CosFov / SinFov;
    float w = h / AspectRatio;
    float r = FarZ / (FarZ - NearZ);

    return FMatrix(
        w, 0.0f, 0.0f, 0.0f,
        0.0f, h, 0.0f, 0.0f,
        0.0f, 0.0f, r, 1.0f,
        0.0f, 0.0f, -r * NearZ, 0.0f
    );
}

FMatrix FMatrix::MakeOrthographic(float OrthoWidth, float OrthoHeight, float NearZ, float FarZ)
{
    float r = 1.0f / (FarZ - NearZ);
    return FMatrix(
        2.0f / OrthoWidth, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / OrthoHeight, 0.0f, 0.0f,
        0.0f, 0.0f, r, 0.0f,
        0.0f, 0.0f, -r * NearZ, 1.0f
    );
}

FVector FMatrix::TransformCoord(const FVector& V, const FMatrix& M)
{
    // 점(Point)의 변환: V.w = 1.0f
    float x = V.X * M.M[0][0] + V.Y * M.M[1][0] + V.Z * M.M[2][0] + M.M[3][0];
    float y = V.X * M.M[0][1] + V.Y * M.M[1][1] + V.Z * M.M[2][1] + M.M[3][1];
    float z = V.X * M.M[0][2] + V.Y * M.M[1][2] + V.Z * M.M[2][2] + M.M[3][2];
    float w = V.X * M.M[0][3] + V.Y * M.M[1][3] + V.Z * M.M[2][3] + M.M[3][3];

    // 투영 변환(Perspective Divide)까지 고려
    if (w != 1.0f && w != 0.0f)
    {
        return FVector(x / w, y / w, z / w);
    }
    return FVector(x, y, z);
}

FVector FMatrix::TransformNormal(const FVector& V, const FMatrix& M)
{
    // 방향(Vector)의 변환: V.w = 0.0f (이동 성분인 M.M[3][x]를 무시)
    return FVector(
        V.X * M.M[0][0] + V.Y * M.M[1][0] + V.Z * M.M[2][0],
        V.X * M.M[0][1] + V.Y * M.M[1][1] + V.Z * M.M[2][1],
        V.X * M.M[0][2] + V.Y * M.M[1][2] + V.Z * M.M[2][2]
    );
}