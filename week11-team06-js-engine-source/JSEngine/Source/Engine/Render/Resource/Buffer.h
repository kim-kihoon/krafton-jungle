#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CoreTypes.h"
#include "Core/ResourceMemoryReporter.h"
#include "Render/Common/ComPtr.h"
#include "Render/Resource/VertexTypes.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;

class FVertexBuffer
{
public:
	void Create(ID3D11Device* InDevice, const TArray<FVertex>& InData, uint32 InByteWidth, uint32 InStride, EGpuResourceCategory InCategory = EGpuResourceCategory::Mesh, const FString& InDebugName = "VertexBuffer");

	// Vertex 타입을 고정하지 않고 raw pointer + stride로 생성합니다.
	// StaticMesh(FNormalVertex) / SkeletalMesh(FSkeletalMeshVertex)를 같은 버퍼 클래스로 처리하기 위한 경로입니다.
	void CreateRaw(ID3D11Device* InDevice, const void* InData, uint32 InVertexCount, uint32 InStride, bool bDynamic, EGpuResourceCategory InCategory = EGpuResourceCategory::Mesh, const FString& InDebugName = "VertexBuffer");
	void SetRaw(ID3D11Buffer* InBuffer, uint32 InVertexCount, uint32 InStride);
	void Release();

	void Update(ID3D11DeviceContext* InDeviceContext, const TArray<uint32>& InData, uint32 InByteWidth);

	// CPU Skinning처럼 매 프레임 Vertex Data가 바뀌는 경우 Dynamic VB를 갱신합니다.
	void UpdateRaw(ID3D11DeviceContext* InDeviceContext, const void* InData, uint32 InVertexCount);

	uint32 GetVertexCount() const { return VertexCount; }
	uint32 GetVertexCapacity() const { return VertexCapacity; }
	uint32 GetStride() const { return Stride; }
	size_t GetByteSize() const { return static_cast<size_t>(VertexCapacity) * Stride; }
	void AppendGpuMemoryStats(FGpuResourceMemoryStats& OutStats, const FString& OwnerName = "FVertexBuffer") const;

	ID3D11Buffer* GetBuffer() const;

private:
	TComPtr<ID3D11Buffer> Buffer;
	uint32 VertexCount = 0;
	uint32 VertexCapacity = 0;
	uint32 Stride = 0;
	EGpuResourceCategory Category = EGpuResourceCategory::Mesh;
	FString DebugName;
};

// 원본/source vertex를 렌더 입력과 shader input으로 공유하기 위한 Vertex Buffer
// Skin Cache compute shader가 bind-pose vertex를 읽어야 하므로 VB + SRV 조합 사용
class FVertexSourceBuffer
{
public:
	bool Create(ID3D11Device* InDevice, const void* InData, uint32 InVertexCount, uint32 InStride, EGpuResourceCategory InCategory, const FString& InDebugName);
	void Release();

	ID3D11Buffer* GetBuffer() const { return Buffer.Get(); }
	ID3D11ShaderResourceView* GetSRV() const { return SRV.Get(); }
	uint32 GetStride() const { return Stride; }
	uint32 GetCount() const { return Count; }
	size_t GetByteSize() const { return static_cast<size_t>(Count) * Stride; }
	bool IsValid() const { return Buffer.Get() != nullptr && SRV.Get() != nullptr && Count > 0 && Stride > 0; }
	void AppendGpuMemoryStats(FGpuResourceMemoryStats& OutStats, const FString& OwnerName = "FVertexSourceBuffer") const;

private:
	TComPtr<ID3D11Buffer> Buffer;
	TComPtr<ID3D11ShaderResourceView> SRV;
	uint32 Count = 0;
	uint32 Stride = 0;
	EGpuResourceCategory Category = EGpuResourceCategory::Skeletal;
	FString DebugName;
};

// Skin Cache 시에 사용되는 Vertex Buffer
// Compute shader의 output이면서 동시에 메인 렌더 패스의 input이므로 VB + SRV + UAV 조합 사용
class FRWVertexBuffer
{
public:
	bool Create(ID3D11Device* InDevice, uint32 InStride, uint32 InCount, EGpuResourceCategory InCategory, const FString& InDebugName);
	void Release();

	ID3D11Buffer* GetBuffer() const { return Buffer.Get(); }
	ID3D11ShaderResourceView* GetSRV() const { return SRV.Get(); }
	ID3D11UnorderedAccessView* GetUAV() const { return UAV.Get(); }
	uint32 GetStride() const { return Stride; }
	uint32 GetCount() const { return Count; }
	size_t GetByteSize() const { return static_cast<size_t>(Count) * Stride; }
	bool IsValid() const { return Buffer.Get() != nullptr && SRV.Get() != nullptr && UAV.Get() != nullptr && Count > 0 && Stride > 0; }
	void AppendGpuMemoryStats(FGpuResourceMemoryStats& OutStats, const FString& OwnerName = "FRWVertexBuffer") const;

private:
	TComPtr<ID3D11Buffer> Buffer;
	TComPtr<ID3D11ShaderResourceView> SRV;
	TComPtr<ID3D11UnorderedAccessView> UAV;
	uint32 Count = 0;
	uint32 Stride = 0;
	EGpuResourceCategory Category = EGpuResourceCategory::SkinCache;
	FString DebugName;
};

class FConstantBuffer
{
public:
	void Create(ID3D11Device* InDevice, uint32 InByteWidth);
	void Release();

	void Update(ID3D11DeviceContext* InDeviceContext, const void* InData, uint32 InByteWidth);

	ID3D11Buffer* GetBuffer();

private:
	TComPtr<ID3D11Buffer> Buffer;
};

class FIndexBuffer
{
public:
	void Create(ID3D11Device* InDevice, const TArray<uint32>& InData, uint32 InByteWidth, EGpuResourceCategory InCategory = EGpuResourceCategory::Mesh, const FString& InDebugName = "IndexBuffer");
	void Release();

	void Update(ID3D11DeviceContext* InDeviceContext, const TArray<uint32>& InData, uint32 InByteWidth);

	uint32 GetIndexCount() const { return IndexCount; }
	size_t GetByteSize() const { return static_cast<size_t>(IndexCount) * sizeof(uint32); }
	void AppendGpuMemoryStats(FGpuResourceMemoryStats& OutStats, const FString& OwnerName = "FIndexBuffer") const;
	ID3D11Buffer* GetBuffer() const;

private:
	TComPtr<ID3D11Buffer> Buffer;
	uint32 IndexCount = 0;
	EGpuResourceCategory Category = EGpuResourceCategory::Mesh;
	FString DebugName;
};

class FMeshBuffer
{
public:
	void Create(ID3D11Device* InDevice, const FMeshData& InMeshData);

	// GPU에 한 번 올리고 거의 바뀌지 않는 메시용입니다. StaticMesh / GPU Skinning 원본 버퍼에 사용합니다.
	void CreateImmutableVertexBuffer(ID3D11Device* InDevice, const void* InVertexData, uint32 InVertexCount, uint32 InVertexStride, const TArray<uint32>& InIndices, EGpuResourceCategory InCategory = EGpuResourceCategory::Mesh, const FString& InDebugName = "MeshBuffer");
	void CreateImmutableSourceVertexBuffer(ID3D11Device* InDevice, const void* InVertexData, uint32 InVertexCount, uint32 InVertexStride, const TArray<uint32>& InIndices, EGpuResourceCategory InCategory = EGpuResourceCategory::Skeletal, const FString& InDebugName = "SourceMeshBuffer");

	// CPU Skinning / Procedural처럼 Vertex Data를 계속 갱신해야 하는 메시용입니다.
	void CreateDynamicVertexBuffer(ID3D11Device* InDevice, uint32 InMaxVertexCount, uint32 InVertexStride, const TArray<uint32>& InIndices, EGpuResourceCategory InCategory = EGpuResourceCategory::Mesh, const FString& InDebugName = "MeshBuffer");
	void UpdateDynamicVertexBuffer(ID3D11DeviceContext* InDeviceContext, const void* InVertexData, uint32 InVertexCount);
	void Release();

	template <typename TVertex>
	void CreateImmutableVertices(ID3D11Device* InDevice, const TArray<TVertex>& InVertices, const TArray<uint32>& InIndices, EGpuResourceCategory InCategory = EGpuResourceCategory::Mesh, const FString& InDebugName = "MeshBuffer")
	{
		CreateImmutableVertexBuffer(InDevice, InVertices.data(), static_cast<uint32>(InVertices.size()), sizeof(TVertex), InIndices, InCategory, InDebugName);
	}

	template <typename TVertex>
	void CreateImmutableSourceVertices(ID3D11Device* InDevice, const TArray<TVertex>& InVertices, const TArray<uint32>& InIndices, EGpuResourceCategory InCategory = EGpuResourceCategory::Skeletal, const FString& InDebugName = "SourceMeshBuffer")
	{
		CreateImmutableSourceVertexBuffer(InDevice, InVertices.data(), static_cast<uint32>(InVertices.size()), sizeof(TVertex), InIndices, InCategory, InDebugName);
	}

	template <typename TVertex>
	void CreateDynamicVertices(ID3D11Device* InDevice, uint32 InMaxVertexCount, const TArray<uint32>& InIndices, EGpuResourceCategory InCategory = EGpuResourceCategory::Mesh, const FString& InDebugName = "MeshBuffer")
	{
		CreateDynamicVertexBuffer(InDevice, InMaxVertexCount, sizeof(TVertex), InIndices, InCategory, InDebugName);
	}

	template <typename TVertex>
	void UpdateDynamicVertices(ID3D11DeviceContext* InDeviceContext, const TArray<TVertex>& InVertices)
	{
		UpdateDynamicVertexBuffer(InDeviceContext, InVertices.data(), static_cast<uint32>(InVertices.size()));
	}

	FVertexBuffer& GetVertexBuffer() { return VertexBuffer; }
	FIndexBuffer& GetIndexBuffer() { return IndexBuffer; }
	const FVertexBuffer& GetVertexBuffer() const { return VertexBuffer; }
	const FIndexBuffer& GetIndexBuffer() const { return IndexBuffer; }
	ID3D11Buffer* GetDrawVertexBuffer() const;
	ID3D11ShaderResourceView* GetSourceVertexSRV() const;
	uint32 GetDrawVertexCount() const;
	uint32 GetDrawVertexStride() const;
	bool HasSourceVertexBuffer() const { return bUseSourceVertexBuffer && SourceVertexBuffer.IsValid(); }
	bool IsValid() const { return GetDrawVertexBuffer() != nullptr && GetDrawVertexCount() > 0; }
	void AppendGpuMemoryStats(FGpuResourceMemoryStats& OutStats, const FString& OwnerName = "FMeshBuffer") const;

private:
	FVertexBuffer VertexBuffer;
	FVertexSourceBuffer SourceVertexBuffer;
	FIndexBuffer IndexBuffer;
	bool bUseSourceVertexBuffer = false;
};

class FStructuredBuffer
{
public:
	void Create(ID3D11Device* InDevice, uint32 InElementSize, uint32 InMaxElements, bool bEnableUAV = false, EGpuResourceCategory InCategory = EGpuResourceCategory::Other, const FString& InDebugName = "StructuredBuffer");
	void Update(ID3D11DeviceContext* InDeviceContext, const void* InData, uint32 InElementCount);
	void Release();

	ID3D11ShaderResourceView* GetSRV() const;
	ID3D11UnorderedAccessView* GetUAV() const;
	uint32 GetCount() const { return Count; }
	uint32 GetCapacity() const { return Capacity; }
	uint32 GetElementSize() const { return ElementSize; }
	size_t GetByteSize() const { return static_cast<size_t>(Capacity) * ElementSize; }
	void AppendGpuMemoryStats(FGpuResourceMemoryStats& OutStats, const FString& OwnerName = "FStructuredBuffer") const;

private:
	TComPtr<ID3D11Buffer> Buffer;
	TComPtr<ID3D11ShaderResourceView> SRV;
	TComPtr<ID3D11UnorderedAccessView> UAV;
	uint32 Count = 0;
	uint32 Capacity = 0;
	uint32 ElementSize = 0;
	EGpuResourceCategory Category = EGpuResourceCategory::Other;
	FString DebugName;
};
