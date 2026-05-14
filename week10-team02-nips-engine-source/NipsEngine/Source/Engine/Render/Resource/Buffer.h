#pragma once

/*
	Vertex Buffer와 Constant Buffer를 관리하는 Class 입니다.
	추후에 Index Buffer, Structured Buffer 등 다양한 Buffer들을 관리하는 Class로 확장할 수 있습니다.
	또한 코드가 길어지면, 각 Buffer들의 source를 분리할 수도 있습니다. (이후)
*/

#include "Render/Common/ComPtr.h"

#include "Core/CoreMinimal.h"
#include "Render/Resource/VertexTypes.h"

#include <d3d11.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;

class FVertexBufferBase
{
public:
    virtual ~FVertexBufferBase() = default;
    virtual void Release() = 0;
    virtual uint32 GetVertexCount() const = 0;
    virtual uint32 GetStride() const = 0;
    virtual ID3D11Buffer* GetBuffer() const = 0;
};

class FVertexBuffer : public FVertexBufferBase
{
public:
    template<typename TVertex>
    void Create(ID3D11Device* InDevice, const TArray<TVertex>& InData);

    void SetRaw(ID3D11Buffer* InBuffer, uint32 InVertexCount, uint32 InStride);
    void Release() override;

    uint32 GetVertexCount() const override { return VertexCount; }
    uint32 GetStride() const override { return Stride; }

    ID3D11Buffer* GetBuffer() const override;

private:
    TComPtr<ID3D11Buffer> Buffer;
    uint32 VertexCount = 0;
    uint32 Stride = 0;
};

class FDynamicVertexBuffer : public FVertexBufferBase
{
public:
    template<typename TVertex>
    void Create(ID3D11Device* InDevice, const TArray<TVertex>& InData);

    template<typename TVertex>
    void Update(ID3D11DeviceContext* InContext, const TArray<TVertex>& InData);

    void SetRaw(ID3D11Buffer* InBuffer, uint32 InVertexCount, uint32 InStride);
    void Release() override;

    uint32 GetVertexCount() const override { return VertexCount; }
    uint32 GetStride() const override { return Stride; }

    ID3D11Buffer* GetBuffer() const override;

private:
    TComPtr<ID3D11Buffer> Buffer;
    uint32 VertexCount = 0;
    uint32 Stride = 0;
    uint32 MaxVertexCount = 0;
};

class FConstantBuffer
{
public:
	void Create(ID3D11Device* InDevice, uint32 InByteWidth);
	void Release();

	void Update(ID3D11DeviceContext* InDeviceContext, const void * InData, uint32 InByteWidth);

	ID3D11Buffer* GetBuffer();

private:
	TComPtr<ID3D11Buffer> Buffer;
};

class FIndexBuffer
{
public:
	void Create(ID3D11Device* InDevice, const TArray<uint32>& InData);
	void Release();

	uint32 GetIndexCount() const { return IndexCount; }
	ID3D11Buffer* GetBuffer() const;

private:
	TComPtr<ID3D11Buffer> Buffer;
	uint32 IndexCount = 0;
};

//	하나의 PrimitiveComponent에서 사용할 Mesh Buffer입니다. Vertex Buffer와 Index Buffer를 포함합니다.
class FMeshBuffer
{
public:
    virtual ~FMeshBuffer() = default;

    template<typename TVertex>
    void Create(ID3D11Device* InDevice, const TArray<TVertex>& InVertices, const TArray<uint32>& InIndices);

    virtual void Release();

    virtual FVertexBufferBase& GetVertexBuffer() { return VertexBuffer; }
    virtual FIndexBuffer& GetIndexBuffer() { return IndexBuffer; }
    virtual const FVertexBufferBase& GetVertexBuffer() const { return VertexBuffer; }
    virtual const FIndexBuffer& GetIndexBuffer() const { return IndexBuffer; }
    virtual bool IsValid() const { return GetVertexBuffer().GetBuffer() != nullptr && GetVertexBuffer().GetVertexCount() > 0; }

protected:
    FVertexBuffer VertexBuffer;
    FIndexBuffer IndexBuffer;
};

class FDynamicMeshBuffer : public FMeshBuffer
{
public:
    template<typename TVertex>
    void Create(ID3D11Device* InDevice, const TArray<TVertex>& InVertices, const TArray<uint32>& InIndices);

    template<typename TVertex>
    void Update(ID3D11DeviceContext* InContext, const TArray<TVertex>& InVertices);

    void Release() override;

    FVertexBufferBase& GetVertexBuffer() override { return DynamicVertexBuffer; }
    const FVertexBufferBase& GetVertexBuffer() const override { return DynamicVertexBuffer; }
    bool IsValid() const override { return DynamicVertexBuffer.GetBuffer() != nullptr && DynamicVertexBuffer.GetVertexCount() > 0; }

private:
    FDynamicVertexBuffer DynamicVertexBuffer;
};

class FStructuredBuffer
{
public:
	void Create(ID3D11Device* InDevice, uint32 InElementSize, uint32 InMaxElements);
	void Update(ID3D11DeviceContext* InDeviceContext, const void* InData, uint32 InElementCount);
	void Release();

	ID3D11ShaderResourceView* GetSRV() const;
    uint32 GetCount() const { return Count; }
    uint32 GetMaxElements() const { return MaxElements; }

private:
	TComPtr<ID3D11Buffer> Buffer;
	TComPtr<ID3D11ShaderResourceView> SRV;
	uint32 Count = 0;
	uint32 ElementSize = 0;
    uint32 MaxElements = 0;
};

template <typename TVertex>
void FVertexBuffer::Create(ID3D11Device* InDevice, const TArray<TVertex>& InData)
{
    if (InData.empty() || !InDevice)
    {
        Release();
        VertexCount = 0;
        Stride = sizeof(TVertex);
        return;
    }

    const uint32 InStride = sizeof(TVertex);
    const uint32 InByteWidth = static_cast<uint32>(InStride * InData.size());

    D3D11_BUFFER_DESC Desc = {};
    Desc.ByteWidth = InByteWidth;
    Desc.Usage = D3D11_USAGE_IMMUTABLE;
    Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA SRD = { InData.data() };

    HRESULT hr = InDevice->CreateBuffer(&Desc, &SRD, Buffer.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        Release();
        VertexCount = 0;
        Stride = InStride;
        return;
    }

    VertexCount = static_cast<uint32>(InData.size());
    Stride = InStride;
}
template <typename TVertex>
void FDynamicVertexBuffer::Create(ID3D11Device* InDevice, const TArray<TVertex>& InData)
{
    if (InData.empty() || !InDevice)
    {
        Release();
        VertexCount = static_cast<uint32>(InData.size());
        MaxVertexCount = VertexCount;
        Stride = sizeof(TVertex);
        return;
    }

    const uint32 InStride = sizeof(TVertex);
    const uint32 InByteWidth = static_cast<uint32>(InStride * InData.size());

    D3D11_BUFFER_DESC Desc = {};
    Desc.ByteWidth = InByteWidth;
    Desc.Usage = D3D11_USAGE_DYNAMIC;
    Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA SRD = { InData.data() };

    HRESULT hr = InDevice->CreateBuffer(&Desc, &SRD, Buffer.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        Release();
        VertexCount = 0;
        Stride = InStride;
        return;
    }

    VertexCount = static_cast<uint32>(InData.size());
    MaxVertexCount = VertexCount;
    Stride = InStride;
}

template <typename TVertex>
void FDynamicVertexBuffer::Update(ID3D11DeviceContext* InContext, const TArray<TVertex>& InData)
{
    if (!Buffer || InData.empty()) return;

    uint32 NewVertexCount = static_cast<uint32>(InData.size());

    if (NewVertexCount > MaxVertexCount) { return; }

    D3D11_MAPPED_SUBRESOURCE MappedResource;
    HRESULT hr = InContext->Map(Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource);
    if (SUCCEEDED(hr))
    {
        const uint32 InByteWidth = static_cast<uint32>(sizeof(TVertex) * InData.size());
        memcpy(MappedResource.pData, InData.data(), InByteWidth);
        InContext->Unmap(Buffer.Get(), 0);
        VertexCount = NewVertexCount;
    }
}

template <typename TVertex>
void FMeshBuffer::Create(ID3D11Device* InDevice, const TArray<TVertex>& InVertices, const TArray<uint32>& InIndices)
{
    if (InVertices.empty() || !InDevice)
    {
        VertexBuffer.Release();
        IndexBuffer.Release();
        return;
    }
    VertexBuffer.Create(InDevice, InVertices);
    if (!InIndices.empty())
    {
        IndexBuffer.Create(InDevice, InIndices);
    }
}

template <typename TVertex>
void FDynamicMeshBuffer::Create(ID3D11Device* InDevice, const TArray<TVertex>& InVertices, const TArray<uint32>& InIndices)
{
    if (InVertices.empty() || !InDevice)
    {
        DynamicVertexBuffer.Release();
        IndexBuffer.Release();
        return;
    }
    DynamicVertexBuffer.Create(InDevice, InVertices);
    if (!InIndices.empty())
    {
        IndexBuffer.Create(InDevice, InIndices);
    }
}

template <typename TVertex>
void FDynamicMeshBuffer::Update(ID3D11DeviceContext* InContext, const TArray<TVertex>& InVertices)
{
    DynamicVertexBuffer.Update(InContext, InVertices);
}