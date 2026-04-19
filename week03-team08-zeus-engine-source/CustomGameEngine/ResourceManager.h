#pragma once

#include "EngineTypes.h"
#include "Renderer/Geometry.h"
#include "Renderer/Material.h"
#include "Renderer/StaticFontAtlas.h"
#include "Renderer/DynamicFontAtlas.h"
#include "Renderer/Renderer.h"
#include "Math/MathHelper.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "VertexSimple.h"
#include <d3d11.h>
#include <d3dcompiler.h>

#include "Logger.h"

class URenderer;

class ResourceManager
{
public:
	static ResourceManager* GetInstance()
	{
		static ResourceManager instance;
		return &instance;
	};

	~ResourceManager() {};

	void Initialize(URenderer* InRenderer) { Renderer = InRenderer; Device = Renderer->Device.Get(); }

	template <typename T>
	void CreateGeometry(ID3D11Device* device, const FString& resourceName, const TArray<T>& vertices, const TArray<uint32>& indices)
	{
		D3D11_BUFFER_DESC vertexbufferdesc = {};
		vertexbufferdesc.ByteWidth = sizeof(T) * vertices.size();
		vertexbufferdesc.Usage = D3D11_USAGE_IMMUTABLE;
		vertexbufferdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

		FGeometry& Geometry = GeometryMap[resourceName];

		D3D11_SUBRESOURCE_DATA vertexbufferSRD = { vertices.data() };
		device->CreateBuffer(&vertexbufferdesc, &vertexbufferSRD, Geometry.VertexBuffer.GetAddressOf());

		D3D11_BUFFER_DESC indexbufferdesc = {};
		indexbufferdesc.ByteWidth = sizeof(uint32) * indices.size();
		indexbufferdesc.Usage = D3D11_USAGE_IMMUTABLE;
		indexbufferdesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

		D3D11_SUBRESOURCE_DATA indexbufferSRD = { indices.data() };
		device->CreateBuffer(&indexbufferdesc, &indexbufferSRD, Geometry.IndexBuffer.GetAddressOf());

		TArray<FVector> posVertices(vertices.size());
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			posVertices[i] = FVector(vertices[i].x, vertices[i].y, vertices[i].z);
		}

		Geometry.VertexStride = sizeof(T);
		Geometry.VertexCount = vertices.size();
		Geometry.Offset = 0;
		Geometry.Vertices = posVertices;

		Geometry.Indices = indices;
		Geometry.IndexCount = indices.size();
	}

	template <typename T>
	void CreateBatchGeometry(ID3D11Device* device, const FString& geoName, uint32 vbSize, uint32 ibSize)
	{
		FGeometry& Geometry = GeometryMap[geoName];

		D3D11_BUFFER_DESC vertexbufferdesc = {};
		vertexbufferdesc.ByteWidth = sizeof(T) * vbSize;
		vertexbufferdesc.Usage = D3D11_USAGE_DYNAMIC;
		vertexbufferdesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertexbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		device->CreateBuffer(&vertexbufferdesc, nullptr, Geometry.VertexBuffer.GetAddressOf());

		D3D11_BUFFER_DESC indexbufferdesc = {};
		indexbufferdesc.ByteWidth = sizeof(uint32) * ibSize;
		indexbufferdesc.Usage = D3D11_USAGE_DYNAMIC;
		indexbufferdesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		device->CreateBuffer(&indexbufferdesc, nullptr, Geometry.IndexBuffer.GetAddressOf());

		Geometry.VertexStride = sizeof(T);
		Geometry.Offset = 0;
	}

	void CreateMaterial(const FWString& VertexShaderName, const FWString& PixelShaderName, ID3D11Device* Device, D3D11_INPUT_ELEMENT_DESC* layout, UINT layoutCount)
	{
		FMaterial& Mat = MaterialMap[VertexShaderName];
	
		ID3DBlob* vertexshaderCSO = nullptr;
		D3DCompileFromFile(VertexShaderName.c_str(), nullptr, nullptr, "mainVS", "vs_5_0", 0, 0, &vertexshaderCSO, nullptr);
		Device->CreateVertexShader(vertexshaderCSO->GetBufferPointer(), vertexshaderCSO->GetBufferSize(), nullptr, Mat.VertexShader.GetAddressOf());
		if (layout != nullptr)
			Device->CreateInputLayout(
				layout, layoutCount, vertexshaderCSO->GetBufferPointer(),
				vertexshaderCSO->GetBufferSize(), Mat.InputLayout.GetAddressOf());
	
		ID3DBlob* pixelshaderCSO = nullptr;
		D3DCompileFromFile(PixelShaderName.c_str(), nullptr, nullptr, "mainPS", "ps_5_0", 0,
			0, &pixelshaderCSO, nullptr);
		Device->CreatePixelShader(pixelshaderCSO->GetBufferPointer(),
			pixelshaderCSO->GetBufferSize(), nullptr,
			Mat.PixelShader.GetAddressOf());
	
	
		pixelshaderCSO->Release();
		vertexshaderCSO->Release();
	}

	void CreateDynamicFontAtlas(const FWString& FontPath, ID3D11Device* Device, ID3D11DeviceContext* DeviceContext)
	{
		FDynamicFontAtlas& fontAtlas = DynamicFontAtlasMap[FontPath];

		bool ok = fontAtlas.Initialize(
			Device,
			DeviceContext,
			FontPath.c_str(),
			32.0f,
			1024,
			1024);

		if (!ok)
		{
			__debugbreak();
		}
		fontAtlas.Sampler = Renderer->SamplerState.Get();
	}

	void CreateStaticFontAtlas(const FWString& FontPath, ID3D11Device* Device, ID3D11DeviceContext* DeviceContext)
	{
		FStaticFontAtlas& fontAtlas = StaticFontAtlasMap[FontPath];

		bool ok = fontAtlas.Initialize(
			Device,
			DeviceContext,
			FontPath.c_str(),
			16.0f,
			512,
			512,
			32,
			32);

		if (!ok)
		{
			__debugbreak();
		}

		fontAtlas.Sampler = Renderer->SamplerState.Get();
	}

	void AddCubeMesh(ID3D11Device* Device);
	void AddSphereMesh(ID3D11Device* Device);
	void AddTriangleMesh(ID3D11Device* Device);
	void AddPlaneMesh(ID3D11Device* Device);
	void AddGizmoTranslationMesh(ID3D11Device* Device);
	void AddGizmoRotationMesh(ID3D11Device* Device);
	void AddGizmoScaleMesh(ID3D11Device* Device);
	void AddLineMesh(ID3D11Device* Device);
	void AddTextBatchMesh(ID3D11Device* Device);
	void AddParticleSubUVMesh(ID3D11Device* Device);

	TArray<FVertexSimple> CreateCubeVertices();
	TArray<uint32> CreateCubeIndices();

	TArray<FVertexSimple> CreateSphereVertices(float radius = 0.5f, int sectorCount = 36, int stackCount = 18);
	TArray<uint32> CreateSphereIndices(int sectorCount = 36, int stackCount = 18); 

	TArray<FVertexSimple> CreateTorusVertices(float majorRadius = 1.0f, float minorRadius = 0.1f, int radialSegments = 32, int tubularSegments = 16);
	TArray<uint32> CreateTorusIndices(int radialSegments = 32, int tubularSegments = 16);

	TArray<FVertexSimple> CreateCylinderVertices(int sliceCount = 20, float radius = 0.5f, float height = 1.0f);
	TArray<uint32> CreateCylinderIndices(int sliceCount = 20);

	TArray<FVertexSimple> CreateConeVertices(int sliceCount = 20, float radius = 0.5f, float height = 1.0f);
	TArray<uint32> CreateConeIndices(int sliceCount = 20);

	FMaterial& GetMaterial(const FWString& ResourceName)
	{
		return MaterialMap[ResourceName];
	}
	FGeometry& GetGeometry(const FString& ResourceName)
	{
		return GeometryMap[ResourceName];
	}
	FStaticFontAtlas& GetStaticFontAtlas(const FWString& ResourceName)
	{
		return StaticFontAtlasMap[ResourceName];
	}
	FDynamicFontAtlas& GetDynamicFontAtlas(const FWString& ResourceName)
	{
		return DynamicFontAtlasMap[ResourceName];
	}

	TComPtr<ID3D11ShaderResourceView> LoadTextureSRV(const wchar_t* path);

	void Release()
	{
		GeometryMap.clear();
		MaterialMap.clear();
	}
private:
	ResourceManager() {};
	TMap<std::wstring, TComPtr<ID3D11ShaderResourceView>> TextureCache;
	ID3D11ShaderResourceView* CreateFallbackSRV();
	TMap<FString, FGeometry> GeometryMap;
	TMap<FWString, FMaterial> MaterialMap;
	TMap<FWString, FStaticFontAtlas> StaticFontAtlasMap;
	TMap<FWString, FDynamicFontAtlas> DynamicFontAtlasMap;

	URenderer* Renderer = nullptr;
	ID3D11Device* Device = nullptr;
};