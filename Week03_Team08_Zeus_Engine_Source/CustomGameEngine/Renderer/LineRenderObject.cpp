#include "LineRenderObject.h"
#include "Geometry.h"
#include "Material.h"
#include "EngineStatics.h"

bool LineRenderObject::Render(ID3D11DeviceContext* Context)
{
	if (!Material || LineVertices.empty()) return false;

	Material->BindShaders(Context);

	UINT stride = sizeof(FVertexSimple);
	UINT offset = 0;

	Context->IASetVertexBuffers(0, 1, Geometry->VertexBuffer.GetAddressOf(), &stride, &offset);
	Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	const UINT MAX_VERTICES_PER_BATCH = 1000;
	
	UINT totalVertices = static_cast<UINT>(LineVertices.size());	// CPU에 쌓여있는 전체 vertex
	UINT currentOffset = 0;											// 현재 CPU 배열에서 어디까지 읽었는지

	while (currentOffset < totalVertices)
	{
		// 이번에 얼마나 그릴지 Count
		UINT drawCount = MAX_VERTICES_PER_BATCH;

		if (MAX_VERTICES_PER_BATCH > totalVertices - currentOffset)
		{
			drawCount = totalVertices - currentOffset;
		}

		D3D11_MAPPED_SUBRESOURCE mappedResource;
		HRESULT hr = Context->Map(Geometry->VertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource); // GPU의 VRAM에 CPU가 접근할 수 있는 통로 열기

		if (SUCCEEDED(hr))
		{
			memcpy(mappedResource.pData, &LineVertices[currentOffset], sizeof(FVertexSimple) * drawCount);
			Context->Unmap(Geometry->VertexBuffer.Get(), 0);	// 통로 닫기

			Context->Draw(drawCount, 0);
		}

		currentOffset += drawCount;

		UEngineStatics::TotalDrawCalls++;
	}

	LineVertices.clear();

	return true;
}