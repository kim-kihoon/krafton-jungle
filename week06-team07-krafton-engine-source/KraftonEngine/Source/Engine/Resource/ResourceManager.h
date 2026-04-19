#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "Core/ResourceTypes.h"
#include "Object/FName.h"
#include <filesystem>

// 리소스를 관리하는 싱글턴.
// Resource.ini에서 리소스 경로/그리드 정보를 읽고, GPU 리소스를 로드/캐싱합니다.
// 컴포넌트는 소유하지 않고 포인터로 공유 데이터를 참조합니다.

struct ID3D11Device;
struct ID3D11DeviceContext;

class FResourceManager : public TSingleton<FResourceManager>
{
	friend class TSingleton<FResourceManager>;

public:
	// Resource.ini에서 경로/그리드 정보 로드 후 GPU 리소스 생성
	void LoadFromFile(const FString& Path, ID3D11Device* InDevice, ID3D11DeviceContext* InContext);

	// GPU 리소스 로드 (Device 필요)
	bool LoadGPUResources(ID3D11Device* Device, ID3D11DeviceContext* Context);

	// 모든 GPU 리소스 해제
	void ReleaseGPUResources();

	// --- Font ---
	FFontResource* FindFont(const FName& FontName);
	const FFontResource* FindFont(const FName& FontName) const;
	void RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns = 16, uint32 Rows = 16);

	// --- Font names ---
	TArray<FString> GetFontNames() const;

	// --- Particle ---
	FParticleResource* FindParticle(const FName& ParticleName);
	const FParticleResource* FindParticle(const FName& ParticleName) const;
	void RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns = 1, uint32 Rows = 1);

	// --- Particle names ---
	TArray<FString> GetParticleNames() const;

	// --- Texture (단일 정적 이미지, 1x1 atlas) ---
	FTextureResource* FindTexture(const FName& TextureName);
	const FTextureResource* FindTexture(const FName& TextureName) const;
	void RegisterTexture(const FName& TextureName, const FString& InPath);

	// --- Texture names ---
	TArray<FString> GetTextureNames() const;

	/** Asset/Data 폴더 내의 텍스처 파일(.png, .jpg, .dds)을 자동으로 스캔하여 등록합니다. */
	void ScanTextureAssets(ID3D11Device* InDevice, ID3D11DeviceContext* InContext);

private:
	FResourceManager() = default;
	~FResourceManager() { ReleaseGPUResources(); }

	/** 특정 디렉토리에서 텍스처 파일을 재귀적으로 스캔합니다. */
	void ScanDirectory(const std::filesystem::path& DirPath, ID3D11Device* InDevice, ID3D11DeviceContext* InContext);
	bool LoadSingleGPUResource(FTextureAtlasResource& Resource, ID3D11Device* Device, ID3D11DeviceContext* Context);

	TMap<FString, FFontResource>     FontResources;
	TMap<FString, FParticleResource> ParticleResources;
	TMap<FString, FTextureResource>  TextureResources;
};
