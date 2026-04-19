#include "Resource/ResourceManager.h"
#include "Platform/Paths.h"
#include "SimpleJSON/json.hpp"

#include <fstream>
#include <filesystem>
#include <d3d11.h>
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "UI/EditorConsoleWidget.h"
#include "Profiling/MemoryStats.h"

namespace ResourceKey
{
	constexpr const char* Font     = "Font";
	constexpr const char* Particle = "Particle";
	constexpr const char* Texture  = "Texture";
	constexpr const char* Path     = "Path";
	constexpr const char* Columns  = "Columns";
	constexpr const char* Rows     = "Rows";
}

void FResourceManager::LoadFromFile(const FString& Path, ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
{
	using namespace json;

	std::ifstream File(std::filesystem::path(FPaths::ToWide(Path)));
	if (!File.is_open())
	{
		return;
	}

	FString Content((std::istreambuf_iterator<char>(File)),
		std::istreambuf_iterator<char>());

	JSON Root = JSON::Load(Content);

	// Font
	if (Root.hasKey(ResourceKey::Font))
	{
		JSON FontSection = Root[ResourceKey::Font];
		for (auto& Pair : FontSection.ObjectRange())
		{
			JSON Entry = Pair.second;
			FFontResource Resource;
			Resource.Name    = FName(Pair.first.c_str());
			Resource.Path    = Entry[ResourceKey::Path].ToString();
			Resource.Columns = static_cast<uint32>(Entry[ResourceKey::Columns].ToInt());
			Resource.Rows    = static_cast<uint32>(Entry[ResourceKey::Rows].ToInt());
			Resource.SRV     = nullptr;
			FontResources[Pair.first] = Resource;
		}
	}

	// Particle
	if (Root.hasKey(ResourceKey::Particle))
	{
		JSON ParticleSection = Root[ResourceKey::Particle];
		for (auto& Pair : ParticleSection.ObjectRange())
		{
			JSON Entry = Pair.second;
			FParticleResource Resource;
			Resource.Name    = FName(Pair.first.c_str());
			Resource.Path    = Entry[ResourceKey::Path].ToString();
			Resource.Columns = static_cast<uint32>(Entry[ResourceKey::Columns].ToInt());
			Resource.Rows    = static_cast<uint32>(Entry[ResourceKey::Rows].ToInt());
			Resource.SRV     = nullptr;
			ParticleResources[Pair.first] = Resource;
		}
	}

	// Texture
	if (Root.hasKey(ResourceKey::Texture))
	{
		JSON TextureSection = Root[ResourceKey::Texture];
		for (auto& Pair : TextureSection.ObjectRange())
		{
			JSON Entry = Pair.second;
			FTextureResource Resource;
			Resource.Name    = FName(Pair.first.c_str());
			Resource.Path    = Entry[ResourceKey::Path].ToString();
			Resource.Columns = 1;
			Resource.Rows    = 1;
			Resource.SRV     = nullptr;
			TextureResources[Pair.first] = Resource;
		}
	}

	if (LoadGPUResources(InDevice, InContext))
	{
		ScanTextureAssets(InDevice, InContext);
		UE_LOG("Complete Load Resources!");
	}
	else
	{
		UE_LOG("Failed to Load Resources...");
	}
}

bool FResourceManager::LoadGPUResources(ID3D11Device* Device, ID3D11DeviceContext* Context)
{
	if (!Device) return false;

	for (auto& [Key, Resource] : FontResources)
	{
		if (!LoadSingleGPUResource(Resource, Device, Context)) return false;
	}

	for (auto& [Key, Resource] : ParticleResources)
	{
		if (!LoadSingleGPUResource(Resource, Device, Context)) return false;
	}

	for (auto& [Key, Resource] : TextureResources)
	{
		if (!LoadSingleGPUResource(Resource, Device, Context)) return false;
	}

	return true;
}

bool FResourceManager::LoadSingleGPUResource(FTextureAtlasResource& Resource, ID3D11Device* Device, ID3D11DeviceContext* Context)
{
	if (Resource.SRV)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		Resource.SRV->Release();
		Resource.SRV = nullptr;
		Resource.PixelData.clear();
	}

	std::wstring FullPath = FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(Resource.Path));

	if (!std::filesystem::exists(FullPath))
	{
		return false;
	}

	std::filesystem::path Ext = std::filesystem::path(Resource.Path).extension();
	FString ExtStr = Ext.string();
	for (char& c : ExtStr) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

	HRESULT hr;
	if (ExtStr == ".dds")
	{
		hr = DirectX::CreateDDSTextureFromFileEx(
			Device,
			FullPath.c_str(),
			0,
			D3D11_USAGE_IMMUTABLE,
			D3D11_BIND_SHADER_RESOURCE,
			0, 0,
			DirectX::DDS_LOADER_DEFAULT,
			nullptr,
			&Resource.SRV
		);
	}
	else
	{
		// 1. GPU 리소스 생성
		hr = DirectX::CreateWICTextureFromFileEx(
			Device,
			FullPath.c_str(),
			0,
			D3D11_USAGE_IMMUTABLE,
			D3D11_BIND_SHADER_RESOURCE,
			0, 0,
			DirectX::WIC_LOADER_FORCE_RGBA32,
			nullptr,
			&Resource.SRV
		);

		// 2. CPU용 픽셀 데이터 추출 (피킹용)
		if (SUCCEEDED(hr) && Context)
		{
			ID3D11Resource* TexRes = nullptr;
			Resource.SRV->GetResource(&TexRes);
			if (TexRes)
			{
				ID3D11Texture2D* Tex2D = nullptr;
				if (SUCCEEDED(TexRes->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&Tex2D)))
				{
					D3D11_TEXTURE2D_DESC Desc;
					Tex2D->GetDesc(&Desc);
					Resource.Width = Desc.Width;
					Resource.Height = Desc.Height;

					// Staging Texture 생성 (CPU 읽기용)
					D3D11_TEXTURE2D_DESC StagingDesc = Desc;
					StagingDesc.Usage = D3D11_USAGE_STAGING;
					StagingDesc.BindFlags = 0;
					StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					StagingDesc.MiscFlags = 0;

					ID3D11Texture2D* StagingTex = nullptr;
					if (SUCCEEDED(Device->CreateTexture2D(&StagingDesc, nullptr, &StagingTex)))
					{
						// GPU -> Staging 복사
						Context->CopyResource(StagingTex, Tex2D);

						// Map & Copy to CPU buffer
						D3D11_MAPPED_SUBRESOURCE Mapped;
						if (SUCCEEDED(Context->Map(StagingTex, 0, D3D11_MAP_READ, 0, &Mapped)))
						{
							Resource.PixelData.resize(Resource.Width * Resource.Height * 4);
							uint8_t* Dest = Resource.PixelData.data();
							uint8_t* Src = static_cast<uint8_t*>(Mapped.pData);

							for (uint32_t y = 0; y < Resource.Height; ++y)
							{
								memcpy(Dest + (y * Resource.Width * 4), Src + (y * Mapped.RowPitch), Resource.Width * 4);
							}
							Context->Unmap(StagingTex, 0);
						}
						StagingTex->Release();
					}
					Tex2D->Release();
				}
				TexRes->Release();
			}
		}
	}

	if (FAILED(hr) || !Resource.SRV)
	{
		return false;
	}

	ID3D11Resource* TextureResource = nullptr;
	Resource.SRV->GetResource(&TextureResource);
	Resource.TrackedMemoryBytes = MemoryStats::CalculateTextureMemory(TextureResource);
	if (TextureResource)
	{
		TextureResource->Release();
	}

	if (Resource.TrackedMemoryBytes > 0)
	{
		MemoryStats::AddTextureMemory(Resource.TrackedMemoryBytes);
	}

	return true;
}

void FResourceManager::ScanTextureAssets(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
{
	std::filesystem::path RootPath = FPaths::RootDir();
	
	// Asset 폴더
	ScanDirectory(RootPath / L"Asset", InDevice, InContext);

	// Data 폴더
	ScanDirectory(RootPath / L"Data", InDevice, InContext);
}

void FResourceManager::ScanDirectory(const std::filesystem::path& DirPath, ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
{
	if (!std::filesystem::exists(DirPath)) return;

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(DirPath))
	{
		if (!Entry.is_regular_file()) continue;

		auto Ext = Entry.path().extension();
		if (Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg" || Ext == ".dds" || Ext == ".bmp")
		{
			std::filesystem::path RootPath = FPaths::RootDir();
			std::filesystem::path RelativePath = std::filesystem::relative(Entry.path(), RootPath);
			FString PathStr = FPaths::ToUtf8(RelativePath.wstring());
			FString FileName = FPaths::ToUtf8(Entry.path().stem().wstring());

			// 에디터 아이콘 (_64x) 처리: 이름에서 _64x 접미사를 제거하여 기존 코드와 호환성을 유지합니다.
			if (FileName.size() > 4 && FileName.substr(FileName.size() - 4) == "_64x")
			{
				FileName = FileName.substr(0, FileName.size() - 4);
			}

			if (TextureResources.find(FileName) != TextureResources.end()) continue;

			FTextureResource Resource;
			Resource.Name = FName(FileName);
			Resource.Path = PathStr;
			Resource.Columns = 1;
			Resource.Rows = 1;
			Resource.SRV = nullptr;

			if (LoadSingleGPUResource(Resource, InDevice, InContext))
			{
				TextureResources[FileName] = Resource;
			}
		}
	}
}

void FResourceManager::ReleaseGPUResources()
{
	for (auto& [Key, Resource] : FontResources)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		if (Resource.SRV) { Resource.SRV->Release(); Resource.SRV = nullptr; }
	}
	for (auto& [Key, Resource] : ParticleResources)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		if (Resource.SRV) { Resource.SRV->Release(); Resource.SRV = nullptr; }
	}
	for (auto& [Key, Resource] : TextureResources)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		if (Resource.SRV) { Resource.SRV->Release(); Resource.SRV = nullptr; }
	}
}

// --- Font ---
FFontResource* FResourceManager::FindFont(const FName& FontName)
{
	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : nullptr;
}

const FFontResource* FResourceManager::FindFont(const FName& FontName) const
{
	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : nullptr;
}

void FResourceManager::RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FFontResource Resource;
	Resource.Name    = FontName;
	Resource.Path    = InPath;
	Resource.Columns = Columns;
	Resource.Rows    = Rows;
	Resource.SRV     = nullptr;
	FontResources[FontName.ToString()] = Resource;
}

// --- Particle ---
FParticleResource* FResourceManager::FindParticle(const FName& ParticleName)
{
	auto It = ParticleResources.find(ParticleName.ToString());
	return (It != ParticleResources.end()) ? &It->second : nullptr;
}

const FParticleResource* FResourceManager::FindParticle(const FName& ParticleName) const
{
	auto It = ParticleResources.find(ParticleName.ToString());
	return (It != ParticleResources.end()) ? &It->second : nullptr;
}

void FResourceManager::RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FParticleResource Resource;
	Resource.Name    = ParticleName;
	Resource.Path    = InPath;
	Resource.Columns = Columns;
	Resource.Rows    = Rows;
	Resource.SRV     = nullptr;
	ParticleResources[ParticleName.ToString()] = Resource;
}

TArray<FString> FResourceManager::GetFontNames() const
{
	TArray<FString> Names;
	Names.reserve(FontResources.size());
	for (const auto& [Key, _] : FontResources)
	{
		Names.push_back(Key);
	}
	return Names;
}

TArray<FString> FResourceManager::GetParticleNames() const
{
	TArray<FString> Names;
	Names.reserve(ParticleResources.size());
	for (const auto& [Key, _] : ParticleResources)
	{
		Names.push_back(Key);
	}
	return Names;
}

// --- Texture ---
FTextureResource* FResourceManager::FindTexture(const FName& TextureName)
{
	auto It = TextureResources.find(TextureName.ToString());
	return (It != TextureResources.end()) ? &It->second : nullptr;
}

const FTextureResource* FResourceManager::FindTexture(const FName& TextureName) const
{
	auto It = TextureResources.find(TextureName.ToString());
	return (It != TextureResources.end()) ? &It->second : nullptr;
}

void FResourceManager::RegisterTexture(const FName& TextureName, const FString& InPath)
{
	FTextureResource Resource;
	Resource.Name    = TextureName;
	Resource.Path    = InPath;
	Resource.Columns = 1;
	Resource.Rows    = 1;
	Resource.SRV     = nullptr;
	TextureResources[TextureName.ToString()] = Resource;
}

TArray<FString> FResourceManager::GetTextureNames() const
{
	TArray<FString> Names;
	Names.reserve(TextureResources.size());
	for (const auto& [Key, _] : TextureResources)
	{
		Names.push_back(Key);
	}
	return Names;
}
