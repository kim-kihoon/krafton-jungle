#pragma once

#include "EngineTypes.h"
#include "Math/Vector.h"
#include <d3d11.h>

struct FGeometry
{
	TComPtr<ID3D11Buffer> VertexBuffer = nullptr;
	TComPtr<ID3D11Buffer> IndexBuffer = nullptr;
	TArray<FVector> Vertices;
	UINT VertexCount = 0;
	UINT VertexStride = 0;
	UINT Offset = 0;
	TArray<uint32> Indices;
	UINT IndexCount = 0;
	FGeometry() {}
	~FGeometry()
	{
		VertexBuffer.Reset();

		if (IndexBuffer)
		{
			IndexBuffer.Reset();
		}
	}

	void BindBuffers(ID3D11DeviceContext* Context)
	{
		Context->IASetVertexBuffers(0, 1, VertexBuffer.GetAddressOf(), &VertexStride, &Offset);

		if (IndexBuffer)
		{
			Context->IASetIndexBuffer(IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		}
	}
};