#include "Buffer.h"

#pragma region __FMESHBUFFER__

void FMeshBuffer::Release()
{
	VertexBuffer.Release();
	IndexBuffer.Release();
}

#pragma endregion

#pragma region __FVERTEXBUFFER__

void FVertexBuffer::SetRaw(ID3D11Buffer* InBuffer, uint32 InVertexCount, uint32 InStride)
{
	Release();
	Buffer.Attach(InBuffer);
	VertexCount = InVertexCount;
	Stride      = InStride;
}

void FVertexBuffer::Release()
{
	Buffer.Reset();
}

ID3D11Buffer* FVertexBuffer::GetBuffer() const
{
	return Buffer.Get();
}

#pragma endregion

#pragma region __FCONSTANTBUFFER__

//	Constant buffer는 Dynamic으로 생성하여 업데이트가 가능하도록 설정
void FConstantBuffer::Create(ID3D11Device* InDevice, uint32 InByteWidth)
{
	D3D11_BUFFER_DESC Desc = {};
	Desc.ByteWidth      = (InByteWidth + 0xf) & 0xfffffff0;
	Desc.Usage          = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	InDevice->CreateBuffer(&Desc, nullptr, Buffer.ReleaseAndGetAddressOf());
}

void FConstantBuffer::Release()
{
	Buffer.Reset();
}

void FConstantBuffer::Update(ID3D11DeviceContext* InDeviceContext, const void* InData, uint32 InByteWidth)
{
	if (Buffer)
	{
		D3D11_MAPPED_SUBRESOURCE MSR;
		InDeviceContext->Map(Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MSR);
		std::memcpy(MSR.pData, InData, InByteWidth);
		InDeviceContext->Unmap(Buffer.Get(), 0);
	}
}

ID3D11Buffer* FConstantBuffer::GetBuffer()
{
	return Buffer.Get();
}

#pragma endregion

#pragma region __FINDEXBUFFER__

void FIndexBuffer::Create(ID3D11Device* InDevice, const TArray<uint32>& InData)
{
	if (InData.empty() || !InDevice)
	{
		Release();
		IndexCount = 0;
		return;
	}

	D3D11_BUFFER_DESC Desc = {};
	Desc.Usage     = D3D11_USAGE_IMMUTABLE;
	Desc.ByteWidth = static_cast<uint32>(sizeof(uint32) * InData.size());
	Desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA SRD = { InData.data() };

	HRESULT hr = InDevice->CreateBuffer(&Desc, &SRD, Buffer.ReleaseAndGetAddressOf());
	if (FAILED(hr))
	{
		Release();
		IndexCount = 0;
		return;
	}

	IndexCount = static_cast<uint32>(InData.size());
}

void FIndexBuffer::Release()
{
	Buffer.Reset();
}

ID3D11Buffer* FIndexBuffer::GetBuffer() const
{
	return Buffer.Get();
}

void FStructuredBuffer::Create(ID3D11Device* InDevice, uint32 InElementSize, uint32 InMaxElements)
{
	if (InDevice == nullptr || InElementSize == 0 || InMaxElements == 0)
	{
		Release();
		return;
	}

	ElementSize = InElementSize;
	MaxElements = InMaxElements;

	D3D11_BUFFER_DESC Desc = {};
	Desc.Usage = D3D11_USAGE_DYNAMIC;
	Desc.ByteWidth = InElementSize * InMaxElements;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	Desc.StructureByteStride = InElementSize;

	InDevice->CreateBuffer(&Desc, nullptr, Buffer.ReleaseAndGetAddressOf());

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement = 0;
	SRVDesc.Buffer.NumElements  = InMaxElements;
	InDevice->CreateShaderResourceView(Buffer.Get(), &SRVDesc, SRV.ReleaseAndGetAddressOf());
}

void FStructuredBuffer::Release()
{
	Buffer.Reset();
	SRV.Reset();
	Count       = 0;
	ElementSize = 0;
	MaxElements = 0;
}

void FStructuredBuffer::Update(ID3D11DeviceContext* InContext, const void* InData, uint32 InElementCount)
{
	if (!Buffer || InContext == nullptr)
	{
		return;
	}

	const uint32 ClampedElementCount = (InElementCount > MaxElements) ? MaxElements : InElementCount;
	Count = ClampedElementCount;

	if (ClampedElementCount == 0 || InData == nullptr)
	{
		return;
	}

	D3D11_MAPPED_SUBRESOURCE structuredMSR;
	InContext->Map(Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &structuredMSR);
	std::memcpy(structuredMSR.pData, InData, ClampedElementCount * ElementSize);
	InContext->Unmap(Buffer.Get(), 0);
}

ID3D11ShaderResourceView* FStructuredBuffer::GetSRV() const
{
	return SRV.Get();
}

#pragma endregion
