#include "Shader.h"
#include "ShaderInclude.h"
#include "Profiling/MemoryStats.h"
#include "Materials/Material.h"
#include "Core/Log.h"
#include "Core/Notification.h"
#include "Platform/Paths.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	constexpr uint64 ShaderCacheVersion = 1;

	std::filesystem::path GetShaderCacheDirectory()
	{
		std::filesystem::path CacheDir = std::filesystem::path(FPaths::RootDir()) / L"Build" / L"ShaderCache";
		std::error_code Error;
		std::filesystem::create_directories(CacheDir, Error);
		return CacheDir;
	}

	FString NormalizeUtf8Path(const FString& InPath)
	{
		FString Result = InPath;
		for (char& Ch : Result)
		{
			if (Ch == '\\')
			{
				Ch = '/';
			}
		}
		return Result;
	}

	FString NormalizeWidePath(const wchar_t* InPath)
	{
		if (!InPath)
		{
			return {};
		}

		return NormalizeUtf8Path(FPaths::ToUtf8(std::filesystem::path(InPath).generic_wstring()));
	}

	std::filesystem::path ResolveTrackedPath(const FString& InPath)
	{
		const FString Normalized = NormalizeUtf8Path(InPath);
		std::filesystem::path Path = std::filesystem::path(FPaths::ToWide(Normalized));
		if (Path.is_absolute())
		{
			return Path;
		}

		return std::filesystem::path(FPaths::RootDir()) / Path;
	}

	void NormalizeIncludeList(TArray<FString>& InOutIncludes)
	{
		for (FString& Include : InOutIncludes)
		{
			Include = NormalizeUtf8Path(Include);
		}

		std::sort(InOutIncludes.begin(), InOutIncludes.end());
		InOutIncludes.erase(std::unique(InOutIncludes.begin(), InOutIncludes.end()), InOutIncludes.end());
	}

	uint64 HashStringFNV1a(std::string_view Text)
	{
		constexpr uint64 OffsetBasis = 14695981039346656037ull;
		constexpr uint64 Prime = 1099511628211ull;

		uint64 Hash = OffsetBasis;
		for (unsigned char Ch : Text)
		{
			Hash ^= static_cast<uint64>(Ch);
			Hash *= Prime;
		}
		return Hash;
	}

	FString SerializeDefines(const D3D_SHADER_MACRO* Defines)
	{
		if (!Defines)
		{
			return {};
		}

		FString Result;
		for (const D3D_SHADER_MACRO* Define = Defines; Define->Name != nullptr; ++Define)
		{
			Result += Define->Name;
			Result += "=";
			if (Define->Definition)
			{
				Result += Define->Definition;
			}
			Result += ";";
		}
		return Result;
	}

	FString GetCacheStem(const FString& SourcePath, const char* EntryPointA, const char* EntryPointB, const char* ProfileA, const char* ProfileB, const D3D_SHADER_MACRO* Defines)
	{
		std::ostringstream Builder;
		Builder << NormalizeUtf8Path(SourcePath)
			<< '|'
			<< (EntryPointA ? EntryPointA : "")
			<< '|'
			<< (ProfileA ? ProfileA : "")
			<< '|'
			<< (EntryPointB ? EntryPointB : "")
			<< '|'
			<< (ProfileB ? ProfileB : "")
			<< '|'
			<< SerializeDefines(Defines);

		std::ostringstream Hex;
		Hex << std::hex << HashStringFNV1a(Builder.str());

		std::filesystem::path Source = std::filesystem::path(FPaths::ToWide(SourcePath));
		const FString Stem = Source.stem().string();
		return Stem + "_" + Hex.str();
	}

	std::filesystem::file_time_type GetCacheReferenceTime(const TArray<std::filesystem::path>& CacheFiles, bool& bOutValid)
	{
		bOutValid = false;
		std::filesystem::file_time_type ReferenceTime = std::filesystem::file_time_type::max();
		std::error_code Error;

		for (const std::filesystem::path& CacheFile : CacheFiles)
		{
			if (!std::filesystem::exists(CacheFile, Error))
			{
				return {};
			}

			const std::filesystem::file_time_type Time = std::filesystem::last_write_time(CacheFile, Error);
			if (Error)
			{
				return {};
			}

			if (Time < ReferenceTime)
			{
				ReferenceTime = Time;
			}
		}

		bOutValid = true;
		return ReferenceTime;
	}

	bool ReadCacheMetadata(
		const std::filesystem::path& MetaPath,
		const FString& SourcePath,
		const TArray<std::filesystem::path>& CacheFiles,
		TArray<FString>& OutIncludes)
	{
		OutIncludes.clear();

		std::ifstream MetaFile(MetaPath);
		if (!MetaFile.is_open())
		{
			return false;
		}

		FString Line;
		FString ExpectedSource = NormalizeUtf8Path(SourcePath);
		bool bVersionMatched = false;
		bool bSourceMatched = false;

		while (std::getline(MetaFile, Line))
		{
			if (Line.rfind("version=", 0) == 0)
			{
				bVersionMatched = std::strtoull(Line.c_str() + 8, nullptr, 10) == ShaderCacheVersion;
			}
			else if (Line.rfind("source=", 0) == 0)
			{
				bSourceMatched = NormalizeUtf8Path(Line.substr(7)) == ExpectedSource;
			}
			else if (Line.rfind("include=", 0) == 0)
			{
				OutIncludes.push_back(NormalizeUtf8Path(Line.substr(8)));
			}
		}

		if (!bVersionMatched || !bSourceMatched)
		{
			return false;
		}

		NormalizeIncludeList(OutIncludes);

		bool bCacheFilesValid = false;
		const std::filesystem::file_time_type CacheReferenceTime = GetCacheReferenceTime(CacheFiles, bCacheFilesValid);
		if (!bCacheFilesValid)
		{
			return false;
		}

		std::error_code Error;
		const std::filesystem::file_time_type SourceTime = std::filesystem::last_write_time(ResolveTrackedPath(SourcePath), Error);
		if (Error || SourceTime > CacheReferenceTime)
		{
			return false;
		}

		for (const FString& Include : OutIncludes)
		{
			const std::filesystem::file_time_type IncludeTime = std::filesystem::last_write_time(
				std::filesystem::path(FPaths::ShaderDir()) / FPaths::ToWide(Include),
				Error);
			if (Error || IncludeTime > CacheReferenceTime)
			{
				return false;
			}
		}

		return true;
	}

	bool WriteCacheMetadata(const std::filesystem::path& MetaPath, const FString& SourcePath, TArray<FString> Includes)
	{
		NormalizeIncludeList(Includes);

		std::ofstream MetaFile(MetaPath, std::ios::trunc);
		if (!MetaFile.is_open())
		{
			return false;
		}

		MetaFile << "version=" << ShaderCacheVersion << '\n';
		MetaFile << "source=" << NormalizeUtf8Path(SourcePath) << '\n';
		for (const FString& Include : Includes)
		{
			MetaFile << "include=" << Include << '\n';
		}

		return MetaFile.good();
	}

	bool TryLoadCachedBlob(const std::filesystem::path& BlobPath, ID3DBlob** OutBlob)
	{
		if (!OutBlob)
		{
			return false;
		}

		*OutBlob = nullptr;
		return SUCCEEDED(D3DReadFileToBlob(BlobPath.c_str(), OutBlob)) && *OutBlob != nullptr;
	}

	bool WriteBlobCache(const std::filesystem::path& BlobPath, ID3DBlob* Blob)
	{
		return Blob && SUCCEEDED(D3DWriteBlobToFile(Blob, BlobPath.c_str(), TRUE));
	}

	bool TryLoadGraphicsShaderCache(
		const wchar_t* SourceFilePath,
		const char* VSEntryPoint,
		const char* PSEntryPoint,
		const D3D_SHADER_MACRO* Defines,
		ID3DBlob** OutVSBlob,
		ID3DBlob** OutPSBlob,
		TArray<FString>* OutIncludes)
	{
		if (!OutVSBlob || !OutPSBlob)
		{
			return false;
		}

		*OutVSBlob = nullptr;
		*OutPSBlob = nullptr;

		TArray<FString> Includes;
		const FString SourcePath = NormalizeWidePath(SourceFilePath);
		const FString CacheStem = GetCacheStem(SourcePath, VSEntryPoint, PSEntryPoint, "vs_5_0", "ps_5_0", Defines);
		const std::filesystem::path CacheDir = GetShaderCacheDirectory();
		const std::filesystem::path VSPath = CacheDir / FPaths::ToWide(CacheStem + ".vs.cso");
		const std::filesystem::path PSPath = CacheDir / FPaths::ToWide(CacheStem + ".ps.cso");
		const std::filesystem::path MetaPath = CacheDir / FPaths::ToWide(CacheStem + ".meta");
		const TArray<std::filesystem::path> CacheFiles = { VSPath, PSPath, MetaPath };

		if (!ReadCacheMetadata(MetaPath, SourcePath, CacheFiles, Includes))
		{
			return false;
		}

		if (!TryLoadCachedBlob(VSPath, OutVSBlob) || !TryLoadCachedBlob(PSPath, OutPSBlob))
		{
			if (*OutVSBlob) { (*OutVSBlob)->Release(); *OutVSBlob = nullptr; }
			if (*OutPSBlob) { (*OutPSBlob)->Release(); *OutPSBlob = nullptr; }
			return false;
		}

		if (OutIncludes)
		{
			*OutIncludes = std::move(Includes);
		}
		return true;
	}

	void StoreGraphicsShaderCache(
		const wchar_t* SourceFilePath,
		const char* VSEntryPoint,
		const char* PSEntryPoint,
		const D3D_SHADER_MACRO* Defines,
		ID3DBlob* VSBlob,
		ID3DBlob* PSBlob,
		TArray<FString> Includes)
	{
		if (!VSBlob || !PSBlob)
		{
			return;
		}

		const FString SourcePath = NormalizeWidePath(SourceFilePath);
		const FString CacheStem = GetCacheStem(SourcePath, VSEntryPoint, PSEntryPoint, "vs_5_0", "ps_5_0", Defines);
		const std::filesystem::path CacheDir = GetShaderCacheDirectory();
		const std::filesystem::path VSPath = CacheDir / FPaths::ToWide(CacheStem + ".vs.cso");
		const std::filesystem::path PSPath = CacheDir / FPaths::ToWide(CacheStem + ".ps.cso");
		const std::filesystem::path MetaPath = CacheDir / FPaths::ToWide(CacheStem + ".meta");

		if (!WriteBlobCache(VSPath, VSBlob) || !WriteBlobCache(PSPath, PSBlob))
		{
			return;
		}

		WriteCacheMetadata(MetaPath, SourcePath, std::move(Includes));
	}

	bool TryLoadComputeShaderCache(
		const wchar_t* SourceFilePath,
		const char* EntryPoint,
		ID3DBlob** OutCSBlob,
		TArray<FString>* OutIncludes)
	{
		if (!OutCSBlob)
		{
			return false;
		}

		*OutCSBlob = nullptr;

		TArray<FString> Includes;
		const FString SourcePath = NormalizeWidePath(SourceFilePath);
		const FString CacheStem = GetCacheStem(SourcePath, EntryPoint, nullptr, "cs_5_0", nullptr, nullptr);
		const std::filesystem::path CacheDir = GetShaderCacheDirectory();
		const std::filesystem::path CSPath = CacheDir / FPaths::ToWide(CacheStem + ".cs.cso");
		const std::filesystem::path MetaPath = CacheDir / FPaths::ToWide(CacheStem + ".meta");
		const TArray<std::filesystem::path> CacheFiles = { CSPath, MetaPath };

		if (!ReadCacheMetadata(MetaPath, SourcePath, CacheFiles, Includes))
		{
			return false;
		}

		if (!TryLoadCachedBlob(CSPath, OutCSBlob))
		{
			return false;
		}

		if (OutIncludes)
		{
			*OutIncludes = std::move(Includes);
		}
		return true;
	}

	void StoreComputeShaderCache(
		const wchar_t* SourceFilePath,
		const char* EntryPoint,
		ID3DBlob* CSBlob,
		TArray<FString> Includes)
	{
		if (!CSBlob)
		{
			return;
		}

		const FString SourcePath = NormalizeWidePath(SourceFilePath);
		const FString CacheStem = GetCacheStem(SourcePath, EntryPoint, nullptr, "cs_5_0", nullptr, nullptr);
		const std::filesystem::path CacheDir = GetShaderCacheDirectory();
		const std::filesystem::path CSPath = CacheDir / FPaths::ToWide(CacheStem + ".cs.cso");
		const std::filesystem::path MetaPath = CacheDir / FPaths::ToWide(CacheStem + ".meta");

		if (!WriteBlobCache(CSPath, CSBlob))
		{
			return;
		}

		WriteCacheMetadata(MetaPath, SourcePath, std::move(Includes));
	}
}

// ============================================================
// FComputeShader
// ============================================================

bool FComputeShader::Create(ID3D11Device* InDevice, const wchar_t* Path, const char* EntryPoint,
	TArray<FString>* OutIncludes)
{
	Release();

	ID3DBlob* CSBlob = nullptr;
	ID3DBlob* ErrBlob = nullptr;
	TArray<FString> LocalIncludes;
	bool bLoadedFromCache = TryLoadComputeShaderCache(Path, EntryPoint, &CSBlob, &LocalIncludes);

	if (!bLoadedFromCache)
	{
		FShaderInclude IncludeHandler;
		IncludeHandler.OutIncludes = &LocalIncludes;

		HRESULT hr = D3DCompileFromFile(Path, nullptr, &IncludeHandler,
			EntryPoint, "cs_5_0", 0, 0, &CSBlob, &ErrBlob);

		if (FAILED(hr))
		{
			if (ErrBlob)
			{
				UE_LOG("[Shader] CS Compile Error: %s", (const char*)ErrBlob->GetBufferPointer());
				FNotificationManager::Get().AddNotification("CS Compile Error (see log)", ENotificationType::Error, 5.0f);
				ErrBlob->Release();
			}
			return false;
		}

		NormalizeIncludeList(LocalIncludes);
		StoreComputeShaderCache(Path, EntryPoint, CSBlob, LocalIncludes);
	}

	if (OutIncludes)
	{
		*OutIncludes = LocalIncludes;
	}

	HRESULT hr = InDevice->CreateComputeShader(CSBlob->GetBufferPointer(), CSBlob->GetBufferSize(), nullptr, &CS);
	CSBlob->Release();

	return SUCCEEDED(hr) && CS != nullptr;
}

void FComputeShader::Release()
{
	if (CS) { CS->Release(); CS = nullptr; }
}

// ============================================================
// FShader
// ============================================================

FShader::FShader(FShader&& Other) noexcept
	: VertexShader(Other.VertexShader)
	, PixelShader(Other.PixelShader)
	, InputLayout(Other.InputLayout)
	, CachedVertexShaderSize(Other.CachedVertexShaderSize)
	, CachedPixelShaderSize(Other.CachedPixelShaderSize)
	, ShaderParameterLayout(std::move(Other.ShaderParameterLayout))
{
	Other.VertexShader = nullptr;
	Other.PixelShader = nullptr;
	Other.InputLayout = nullptr;
	Other.CachedVertexShaderSize = 0;
	Other.CachedPixelShaderSize = 0;
}

FShader& FShader::operator=(FShader&& Other) noexcept
{
	if (this != &Other)
	{
		Release();
		VertexShader = Other.VertexShader;
		PixelShader = Other.PixelShader;
		InputLayout = Other.InputLayout;
		CachedVertexShaderSize = Other.CachedVertexShaderSize;
		CachedPixelShaderSize = Other.CachedPixelShaderSize;
		ShaderParameterLayout = std::move(Other.ShaderParameterLayout);
		Other.VertexShader = nullptr;
		Other.PixelShader = nullptr;
		Other.InputLayout = nullptr;
		Other.CachedVertexShaderSize = 0;
		Other.CachedPixelShaderSize = 0;
	}
	return *this;
}

void FShader::Create(ID3D11Device* InDevice, const wchar_t* InFilePath, const char* InVSEntryPoint, const char* InPSEntryPoint,
	const D3D_SHADER_MACRO* InDefines, TArray<FString>* OutIncludes)
{
	Release();

	ID3DBlob* vertexShaderCSO = nullptr;
	ID3DBlob* pixelShaderCSO = nullptr;
	ID3DBlob* errorBlob = nullptr;
	TArray<FString> LocalIncludes;
	bool bLoadedFromCache = TryLoadGraphicsShaderCache(
		InFilePath,
		InVSEntryPoint,
		InPSEntryPoint,
		InDefines,
		&vertexShaderCSO,
		&pixelShaderCSO,
		&LocalIncludes);

	if (!bLoadedFromCache)
	{
		FShaderInclude IncludeHandler;
		IncludeHandler.OutIncludes = &LocalIncludes;

		// Vertex Shader 컴파일
		HRESULT hr = D3DCompileFromFile(InFilePath, InDefines, &IncludeHandler, InVSEntryPoint, "vs_5_0", 0, 0, &vertexShaderCSO, &errorBlob);
		if (FAILED(hr))
		{
			if (errorBlob)
			{
				const char* Msg = (const char*)errorBlob->GetBufferPointer();
				UE_LOG("[Shader] VS Compile Error: %s", Msg);
				FNotificationManager::Get().AddNotification("VS Compile Error (see log)", ENotificationType::Error, 5.0f);
				errorBlob->Release();
			}
			return;
		}

		// Pixel Shader 컴파일
		hr = D3DCompileFromFile(InFilePath, InDefines, &IncludeHandler, InPSEntryPoint, "ps_5_0", 0, 0, &pixelShaderCSO, &errorBlob);
		if (FAILED(hr))
		{
			if (errorBlob)
			{
				const char* Msg = (const char*)errorBlob->GetBufferPointer();
				UE_LOG("[Shader] PS Compile Error: %s", Msg);
				FNotificationManager::Get().AddNotification("PS Compile Error (see log)", ENotificationType::Error, 5.0f);
				errorBlob->Release();
			}
			vertexShaderCSO->Release();
			return;
		}

		NormalizeIncludeList(LocalIncludes);
		StoreGraphicsShaderCache(InFilePath, InVSEntryPoint, InPSEntryPoint, InDefines, vertexShaderCSO, pixelShaderCSO, LocalIncludes);
	}

	if (OutIncludes)
	{
		*OutIncludes = LocalIncludes;
	}

	// Vertex Shader 생성
	HRESULT hr = InDevice->CreateVertexShader(vertexShaderCSO->GetBufferPointer(), vertexShaderCSO->GetBufferSize(), nullptr, &VertexShader);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create Vertex Shader (HRESULT: " << hr << ")" << std::endl;
		vertexShaderCSO->Release();
		pixelShaderCSO->Release();
		return;
	}

	CachedVertexShaderSize = vertexShaderCSO->GetBufferSize();
	MemoryStats::AddVertexShaderMemory(static_cast<uint32>(CachedVertexShaderSize));

	// Pixel Shader 생성
	hr = InDevice->CreatePixelShader(pixelShaderCSO->GetBufferPointer(), pixelShaderCSO->GetBufferSize(), nullptr, &PixelShader);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create Pixel Shader (HRESULT: " << hr << ")" << std::endl;
		Release();
		vertexShaderCSO->Release();
		pixelShaderCSO->Release();
		return;
	}

	CachedPixelShaderSize = pixelShaderCSO->GetBufferSize();
	MemoryStats::AddPixelShaderMemory(static_cast<uint32>(CachedPixelShaderSize));

	// Input Layout 생성 (VS input signature로부터 자동 추출)
	CreateInputLayoutFromReflection(InDevice, vertexShaderCSO);

	ExtractCBufferInfo(vertexShaderCSO, ShaderParameterLayout);
	ExtractCBufferInfo(pixelShaderCSO, ShaderParameterLayout);

	vertexShaderCSO->Release();
	pixelShaderCSO->Release();
}

void FShader::Release()
{
	if (InputLayout)
	{
		InputLayout->Release();
		InputLayout = nullptr;
	}
	if (PixelShader)
	{
		MemoryStats::SubPixelShaderMemory(static_cast<uint32>(CachedPixelShaderSize));
		CachedPixelShaderSize = 0;

		PixelShader->Release();
		PixelShader = nullptr;
	}
	if (VertexShader)
	{
		MemoryStats::SubVertexShaderMemory(static_cast<uint32>(CachedVertexShaderSize));
		CachedVertexShaderSize = 0;

		VertexShader->Release();
		VertexShader = nullptr;
	}
}

void FShader::Bind(ID3D11DeviceContext* InDeviceContext) const
{
	InDeviceContext->IASetInputLayout(InputLayout);
	InDeviceContext->VSSetShader(VertexShader, nullptr, 0);
	InDeviceContext->PSSetShader(PixelShader, nullptr, 0);
}


namespace
{
	DXGI_FORMAT MaskToFormat(D3D_REGISTER_COMPONENT_TYPE ComponentType, BYTE Mask)
	{
		// Mask 비트 수 세기 (사용되는 컴포넌트 개수)
		int Count = 0;
		if (Mask & 0x1) ++Count;
		if (Mask & 0x2) ++Count;
		if (Mask & 0x4) ++Count;
		if (Mask & 0x8) ++Count;

		if (ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
		{
			switch (Count)
			{
			case 1: return DXGI_FORMAT_R32_FLOAT;
			case 2: return DXGI_FORMAT_R32G32_FLOAT;
			case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
			case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
			}
		}
		else if (ComponentType == D3D_REGISTER_COMPONENT_UINT32)
		{
			switch (Count)
			{
			case 1: return DXGI_FORMAT_R32_UINT;
			case 2: return DXGI_FORMAT_R32G32_UINT;
			case 3: return DXGI_FORMAT_R32G32B32_UINT;
			case 4: return DXGI_FORMAT_R32G32B32A32_UINT;
			}
		}
		else if (ComponentType == D3D_REGISTER_COMPONENT_SINT32)
		{
			switch (Count)
			{
			case 1: return DXGI_FORMAT_R32_SINT;
			case 2: return DXGI_FORMAT_R32G32_SINT;
			case 3: return DXGI_FORMAT_R32G32B32_SINT;
			case 4: return DXGI_FORMAT_R32G32B32A32_SINT;
			}
		}
		return DXGI_FORMAT_UNKNOWN;
	}
}

void FShader::CreateInputLayoutFromReflection(ID3D11Device* InDevice, ID3DBlob* VSBlob)
{
	ID3D11ShaderReflection* Reflector = nullptr;
	HRESULT hr = D3DReflect(VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(),
		IID_ID3D11ShaderReflection, (void**)&Reflector);
	if (FAILED(hr)) return;

	D3D11_SHADER_DESC ShaderDesc;
	Reflector->GetDesc(&ShaderDesc);

	TArray<D3D11_INPUT_ELEMENT_DESC> Elements;

	for (UINT i = 0; i < ShaderDesc.InputParameters; ++i)
	{
		D3D11_SIGNATURE_PARAMETER_DESC ParamDesc;
		Reflector->GetInputParameterDesc(i, &ParamDesc);

		// SV_VertexID, SV_InstanceID 등 시스템 시맨틱은 스킵
		if (ParamDesc.SystemValueType != D3D_NAME_UNDEFINED)
			continue;

		D3D11_INPUT_ELEMENT_DESC Elem = {};
		Elem.SemanticName = ParamDesc.SemanticName;
		Elem.SemanticIndex = ParamDesc.SemanticIndex;
		Elem.Format = MaskToFormat(ParamDesc.ComponentType, ParamDesc.Mask);
		Elem.InputSlot = 0;
		Elem.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		Elem.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		Elem.InstanceDataStepRate = 0;

		Elements.push_back(Elem);
	}

	// Fullscreen quad 등 vertex input이 없는 셰이더는 InputLayout 불필요
	if (Elements.empty())
	{
		Reflector->Release();
		return;
	}

	hr = InDevice->CreateInputLayout(Elements.data(), (UINT)Elements.size(),
		VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), &InputLayout);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create Input Layout from reflection (HRESULT: " << hr << ")" << std::endl;
	}

	Reflector->Release();
}

//셰이더 컴파일 후 호출. 셰이더 코드의 cbuffer, 텍스처 샘플러 선언을 분석해서 outlayout에 채워넣음. 이 정보는 머티리얼 템플릿이 생성될 때 참조되어야 하므로 셰이더 내부에서 제공하는 형태로 존재해야 함.
void FShader::ExtractCBufferInfo(ID3DBlob* ShaderBlob, TMap<FString, FMaterialParameterInfo*>& OutLayout)
{
	ID3D11ShaderReflection* Reflector = nullptr;
	D3DReflect(ShaderBlob->GetBufferPointer(), ShaderBlob->GetBufferSize(),
		IID_ID3D11ShaderReflection, (void**)&Reflector);

	D3D11_SHADER_DESC ShaderDesc;
	Reflector->GetDesc(&ShaderDesc);

	for (UINT i = 0; i < ShaderDesc.ConstantBuffers; ++i)
	{
		auto* CB = Reflector->GetConstantBufferByIndex(i);
		D3D11_SHADER_BUFFER_DESC CBDesc;
		CB->GetDesc(&CBDesc);

		FString BufferName = CBDesc.Name;  // "PerMaterial", "PerFrame" 등

		//상수 버퍼의 바인딩 정보(Slot Index) 가져오기
		D3D11_SHADER_INPUT_BIND_DESC BindDesc;
		Reflector->GetResourceBindingDescByName(CBDesc.Name, &BindDesc);
		UINT SlotIndex = BindDesc.BindPoint; // 이것이 b0, b1의 숫자입니다.

		if (SlotIndex != 2 && SlotIndex != 3)  // b2, b3만 저장
			continue;

		for (UINT j = 0; j < CBDesc.Variables; ++j)
		{
			auto* Var = CB->GetVariableByIndex(j);
			D3D11_SHADER_VARIABLE_DESC VarDesc;
			Var->GetDesc(&VarDesc);

			FMaterialParameterInfo* Info = new FMaterialParameterInfo();
			Info->BufferName = BufferName;
			Info->SlotIndex = SlotIndex;
			Info->Offset = VarDesc.StartOffset;
			Info->Size = VarDesc.Size;
			
			Info->BufferSize = CBDesc.Size;//cbuffer 크기

			OutLayout[VarDesc.Name] = Info;
		}
	}
	Reflector->Release();
}

