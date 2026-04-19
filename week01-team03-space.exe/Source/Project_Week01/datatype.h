#pragma once

#include <cmath>



struct FVector
{
	float x, y, z;
	FVector(float _x = 0, float _y = 0, float _z = 0)
		:x(_x), y(_y), z(_z) {
	}

	// 복사 생성자
	FVector(const FVector& _fVector)
	{
		x = _fVector.x;
		y = _fVector.y;
		z = _fVector.z;
	}

	// 벡터 덧셈
	FVector operator+(const FVector& other) const {
		return FVector(x + other.x, y + other.y, z + other.z);
	}
	// 벡터 뺄셈
	FVector operator-(const FVector& other) const {
		return FVector(x - other.x, y - other.y, z - other.z);
	}
	// 벡터 스칼라 곱
	FVector operator*(float scalar) const {
		return FVector(x * scalar, y * scalar, z * scalar);
	}
	// 벡터 스칼라 나눗셈
	FVector operator/(float scalar) const {
		return FVector(x / scalar, y / scalar, z / scalar);
	}
	// 복합 대입 연산자 (+=)
	FVector& operator+=(const FVector& other) {
		x += other.x; y += other.y; z += other.z;
		return *this;
	}
	// 복합 대입 연산자 (-=)
	FVector& operator-=(const FVector& other) {
		x -= other.x; y -= other.y; z -= other.z;
		return *this;
	}

	// 내적 (Dot Product)
	static float Dot(const FVector& a, const FVector& b) {
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	// 길이 (Magnitude)
	float Size() const {
		return sqrtf(x * x + y * y + z * z);
	}

	// 정규화 (Normalize)
	FVector GetSafeNormal() const {
		float size = Size();
		if (size < 1.e-4f) return FVector(0, 0, 0);
		return FVector(x / size, y / size, z / size);
	}
};

struct FConstants
{
	FVector fVector;
	float scale;
	FConstants(FVector _fVector)
		:fVector(_fVector), scale(1.0f) {
	}

};


struct FTextureVertex {

	float x, y, z;
	float r, g, b, a;
	float u, v;
};



enum ImageName
{
	ROCKET,
	PLANET1,
	PLANET2,
	PLANET3,
	PLANET4,
	PLANET5,
	PLANET6,
	PLANET7,
	PLANET8,
	PLANET9,
	PLANET10,
	PLANET11,
	PLANET13,
	GOAL,
	SUN,
	COUNT
};