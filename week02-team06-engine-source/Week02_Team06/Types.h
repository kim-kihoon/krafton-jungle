#pragma once

using int8   = signed char;
using int32  = int;
using uint8  = unsigned char;
using uint32 = unsigned int;
using uint64 = unsigned long long;

using FString = std::string;

#include "Math.h"

#pragma once
struct FVertexSimple
{
	float X, Y, Z;
	float R, G, B, A;

	static constexpr int ElementNum = 2;
	static constexpr D3D11_INPUT_ELEMENT_DESC Elements[ElementNum] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
};


//VertexSimplePass
struct FConstantData
{
	FMatrix MVP;
	FVector4 Color;
};

// GPU에 보낼 상수 버퍼 16비트 정렬 강제
struct alignas(16) FTransformConstantBuffer
{
	FMatrix WorldMatrix;
	FMatrix ViewMatrix;
	FMatrix ProjectionMatrix;
};