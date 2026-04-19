#pragma once

class UMesh
{
public:
	UMesh() = default;
	~UMesh() = default;
	//mesh 컴포넌트로 바뀌면서 수정해야함

public:
	const FVertexSimple* GetVertexData() const { return &CpuVertices[0]; };
	const uint32 GetVertexCount() const { return NumVertices;};

public:
	void Load(ID3D11Device& Device, const FVertexSimple* InVertices, UINT VertexCount, const uint32* Indices, UINT IndexCount)
	{
		CpuVertices.SetNum(VertexCount);
		for (UINT i = 0; i < VertexCount; ++i)
		{
			CpuVertices[i] = InVertices[i];
		}

		NumVertices = VertexCount;
		Stride = sizeof(FVertexSimple);
		ByteWidth = sizeof(FVertexSimple) * VertexCount;
		// 정점 버퍼 생성
		D3D11_BUFFER_DESC VertexBufferDesc = {};
		VertexBufferDesc.ByteWidth = ByteWidth;
		VertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
		VertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		const FVertexSimple* Vertices = &CpuVertices[0];

		D3D11_SUBRESOURCE_DATA VertexBufferSRD = { Vertices };

		HRESULT hr = Device.CreateBuffer(&VertexBufferDesc, &VertexBufferSRD, &VertexBuffer);
		assert(SUCCEEDED(hr));

		if (Indices)
		{
			NumIndices = IndexCount; // 개수 저장
			D3D11_BUFFER_DESC IndexBufferDesc = {};
			IndexBufferDesc.ByteWidth = sizeof(uint32) * IndexCount;
			IndexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
			IndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

			D3D11_SUBRESOURCE_DATA IndexBufferData = { Indices };
			HRESULT hr = Device.CreateBuffer(&IndexBufferDesc, &IndexBufferData, &IndexBuffer);
			assert(SUCCEEDED(hr));
		}
	}

	void Release();
	void Draw(ID3D11DeviceContext& DeviceContext);

private:
	ID3D11Buffer* VertexBuffer = { nullptr };
	ID3D11Buffer* IndexBuffer = { nullptr };

	TArray<FVertexSimple> CpuVertices;

	UINT Stride;
	UINT ByteWidth;
	UINT NumVertices;
	UINT NumIndices = 0;
};

