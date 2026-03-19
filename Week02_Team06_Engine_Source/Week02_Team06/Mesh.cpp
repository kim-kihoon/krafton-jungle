#include "pch.h"
#include "Mesh.h"

void UMesh::Release()
{
	if (VertexBuffer)
	{
		VertexBuffer->Release();
		VertexBuffer = nullptr;
	}

	if (IndexBuffer)
	{
		IndexBuffer->Release();
		IndexBuffer = nullptr;
	}

	CpuVertices.Clear();
}

void UMesh::Draw(ID3D11DeviceContext& DeviceContext)
{
	//버퍼 바인딩 후 렌더링

	UINT offset = 0;
	DeviceContext.IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &offset);

	if (IndexBuffer)
	{
		DeviceContext.IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
		DeviceContext.DrawIndexed(NumIndices, 0, 0);
	}
	else
	{
		DeviceContext.Draw(NumVertices, 0);
	}
}

