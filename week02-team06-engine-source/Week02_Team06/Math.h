#pragma once

struct FVector
{
	float X, Y, Z;

	static const FVector Zero;

	// 벡터 연산
	FVector operator+(const FVector& other) const { return { X + other.X, Y + other.Y, Z + other.Z }; }
	FVector operator-(const FVector& other) const { return { X - other.X, Y - other.Y, Z - other.Z }; }
	FVector operator*(float scalar) const { return { X * scalar, Y * scalar, Z * scalar }; }
	FVector operator/(float scalar) const { return { X / scalar, Y / scalar, Z / scalar }; }

	float Dot(const FVector& other) const { return X * other.X + Y * other.Y + Z * other.Z; }
	FVector Cross(const FVector& other) const
	{
		return
		{
			Y * other.Z - Z * other.Y,
			Z * other.X - X * other.Z,
			X * other.Y - Y * other.X
		};
	}

	float Length() const { return sqrtf(X * X + Y * Y + Z * Z); }
	void Normalize()
	{
		float len = Length();
		if (len > 0)
		{
			X /= len;
			Y /= len;
			Z /= len;
		}
	}
};

struct FVector2D
{
	float X, Y;

	FVector2D() : X(0.0f), Y(0.0f) {}
	FVector2D(float InX, float InY) : X(InX), Y(InY) {}

	FVector2D operator+(const FVector2D& Other) const { return { X + Other.X, Y + Other.Y }; }
	FVector2D operator-(const FVector2D& Other) const { return { X - Other.X, Y - Other.Y }; }
	FVector2D operator*(float Scalar) const { return { X * Scalar, Y * Scalar }; }
	FVector2D operator/(float Scalar) const { return { X / Scalar, Y / Scalar }; }

	float Dot(const FVector2D& Other) const { return X * Other.X + Y * Other.Y; }
	float Length() const { return sqrtf(X * X + Y * Y); }

	void Normalize() 
	{
		float len = Length();
		if (len > 0.0f) { X /= len; Y /= len; }
	}
};

struct FVector4
{
	float X, Y, Z, W;

	FVector4() : X(0.0f), Y(0.0f), Z(0.0f), W(0.0f) {}
	FVector4(const FVector& InV, float InW = 1.0f) : X(InV.X), Y(InV.Y), Z(InV.Z), W(InW) {}

	// 추가: (x,y,z,w) 생성자
	FVector4(float InX, float InY, float InZ, float InW)
		: X(InX), Y(InY), Z(InZ), W(InW) {}

	float Dot(const FVector4& Other) const { return X * Other.X + Y * Other.Y + Z * Other.Z + W * Other.W; }
	float Length() const { return sqrtf(X * X + Y * Y + Z * Z + W * W); }
	float Length3() const { return sqrtf(X * X + Y * Y + Z * Z); }
};

struct FIntPoint
{
	int32 X, Y;

	FIntPoint() : X(0), Y(0) {}
	FIntPoint(int32 InX, int32 InY) : X(InX), Y(InY) {}

	FIntPoint operator+(const FIntPoint& Other) const { return { X + Other.X, Y + Other.Y }; }
	FIntPoint operator-(const FIntPoint& Other) const { return { X - Other.X, Y - Other.Y }; }
	FIntPoint operator*(int32 Scalar) const { return { X * Scalar, Y * Scalar }; }
	FIntPoint operator/(int32 Scalar) const { return { X / Scalar, Y / Scalar }; }

	bool operator==(const FIntPoint& Other) const { return X == Other.X && Y == Other.Y; }
	bool operator!=(const FIntPoint& Other) const { return !(*this == Other); }
};

struct FIntVector
{
	int32 X, Y, Z;

	FIntVector() : X(0), Y(0), Z(0) {}
	FIntVector(int32 InX, int32 InY, int32 InZ) : X(InX), Y(InY), Z(InZ) {}

	FIntVector operator+(const FIntVector& Other) const { return { X + Other.X, Y + Other.Y, Z + Other.Z }; }
	FIntVector operator-(const FIntVector& Other) const { return { X - Other.X, Y - Other.Y, Z - Other.Z }; }
	FIntVector operator*(int32 Scalar) const { return { X * Scalar, Y * Scalar, Z * Scalar }; }
	FIntVector operator/(int32 Scalar) const { return { X / Scalar, Y / Scalar, Z / Scalar }; }

	bool operator==(const FIntVector& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z; }
	bool operator!=(const FIntVector& Other) const { return !(*this == Other); }
};

struct FMatrix
{
	float M[4][4];

	FMatrix() { *this = Identity; }

	FMatrix(float _00, float _01, float _02, float _03,
			float _10, float _11, float _12, float _13,
			float _20, float _21, float _22, float _23,
			float _30, float _31, float _32, float _33);

	// 단위 행렬
	static const FMatrix Identity;

	// 행렬 연산
	FMatrix operator*(const FMatrix& Other) const;

	// 변환 행렬
	FMatrix Inverse() const;
	FMatrix Transpose() const;

	static FMatrix MakeScale(const FVector& S);
	static FMatrix MakeRotationX(float Degree);
	static FMatrix MakeRotationY(float Degree);
	static FMatrix MakeRotationZ(float Degree);
	static FMatrix MakeRotation(const FVector& Rotation);
	static FMatrix MakeTranslation(const FVector& T);
	static FMatrix MakeLookAt(const FVector& Eye, const FVector& At, const FVector& Up);
	static FMatrix MakePerspective(float FovAngleDeg, float AspectRatio, float NearZ, float FarZ);
	static FMatrix MakeOrthographic(float OrthoWidth, float OrthoHeight, float NearZ, float FarZ);
	static FVector TransformCoord(const FVector& V, const FMatrix& M);
	static FVector TransformNormal(const FVector& V, const FMatrix& M);
};

namespace Math
{
	template<typename T>
	static T Lerp(const T& A, const T& B, float Alpha) { return A + (B - A) * Alpha; }
	static constexpr float PI = 3.1415926535f;
	static inline float ToRadians(float Degree) { return Degree * (PI / 180.0f); }
	static inline float ToDegrees(float Radian) { return Radian * (180.0f / PI); }
	FVector MatrixToEuler(const FMatrix& M);
	bool RayIntersectsTriangle(const FVector& RayOrigin, const FVector& RayDir, const FVector& V0, const FVector& V1, const FVector& V2, float& OutT);
	FVector RayPlaneIntersection(const FVector& RayOrigin, const FVector& RayDir, const FVector& PlaneNormal, const FVector& PlaneOrigin);
	FVector ClosestPointOnLine(const FVector& RayOrigin, const FVector& RayDir, const FVector& LineOrigin, const FVector& LineDir);
}
