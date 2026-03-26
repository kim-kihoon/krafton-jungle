#pragma once

#include "Vector.h"

struct FRotator;

struct FMatrix {
public:
	float m[4][4];
	FMatrix() {
		for (int i{ 0 }; i < 4; i++)
			for (int j{ 0 }; j < 4; j++) m[i][j] = (i == j) ? 1.0f : 0.0f;
	}
	FMatrix(
		float m00, float m01, float m02, float m03,
		float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23,
		float m30, float m31, float m32, float m33)
	{
		m[0][0] = m00; m[0][1] = m01; m[0][2] = m02; m[0][3] = m03;
		m[1][0] = m10; m[1][1] = m11; m[1][2] = m12; m[1][3] = m13;
		m[2][0] = m20; m[2][1] = m21; m[2][2] = m22; m[2][3] = m23;
		m[3][0] = m30; m[3][1] = m31; m[3][2] = m32; m[3][3] = m33;
	}
public:
	static FMatrix IdentityMatrix()
	{
		return FMatrix();
	}

	static FMatrix ZeroMatrix();
	static FMatrix TranslationMatrix(FVector InVec) { return TranslationMatrix(InVec.x, InVec.y, InVec.z); }
	static FMatrix TranslationMatrix(float InX = 0.f, float InY = 0.f, float InZ = 0.f);

	static FMatrix ScaleMatrix(FVector InVec) { return ScaleMatrix(InVec.x, InVec.y, InVec.z); }
	static FMatrix ScaleMatrix(float InX = 1.f, float InY = 1.f, float InZ = 1.f);

	FVector TransformPoint(const FVector& Vector) const;
	FVector TransformVector(const FVector& Vector) const;

	//Euler Angle
	static FMatrix RotationXMatrix(float InRoll);
	static FMatrix RotationYMatrix(float InPitch);
	static FMatrix RotationZMatrix(float InYaw);

	static FMatrix EulerRotationMatrix(const FRotator& InRot);
	static FMatrix EulerRotationMatrix(float InRoll = 0.0f, float InPitch = 0.0f, float InYaw = 0.0f);

	FMatrix operator*(const FMatrix& Other) const;

	static FMatrix ViewMatrix(FVector Eye, FVector Target, FVector Up);

	static FMatrix PerspectiveMatrix(float FovDegree, float AspectRatio, float Near, float Far);
	static FMatrix OrthographicMatrix(float orthoHeight, float AspectRatio, float Near, float Far);

	FMatrix Inverse() const;

	float* operator[](int Row) { return m[Row]; }

	const float* operator[](int Row)const { return m[Row]; }
};