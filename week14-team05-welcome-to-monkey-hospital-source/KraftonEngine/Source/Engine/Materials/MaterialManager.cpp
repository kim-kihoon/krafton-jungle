#include "MaterialManager.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialDomain.h"  // Phase 1: Domain/BlendMode 도출 (dormant)
#include "Materials/Graph/MaterialGraphCompiler.h"
#include "Platform/Paths.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Resource/Buffer.h"
#include "Texture/Texture2D.h"
#include "Render/Pipeline/Renderer.h"
#include "Serialization/WindowsArchive.h"  // Phase 4: 바이너리 .uasset
#include "Asset/AssetPackage.h"

namespace
{
    constexpr const char* MaterialGraphGeneratorVersion = "MaterialGraph";

	// hospital-map 등 legacy Auto 머티리얼: 잘못된 노멀/알베도 보정 후 디스크에 반영.
	static void SanitizeLoadedImportMaterial(UMaterial* Material, FMaterialManager& Manager)
	{
		if (!Material || Material->IsMaterialInstance())
		{
			return;
		}

		const FString AssetPath = Material->GetAssetPathFileName();
		if (AssetPath.find("Content/Material/Auto/") == FString::npos)
		{
			return;
		}

		if (Material->GetShaderPathForSerialize() != "Shaders/Geometry/UberLit.hlsl")
		{
			return;
		}

		bool bDirty = false;
		const bool bEmissive = Material->GetScalarParameterValue("bEmissive") >= 0.5f;

		if (bEmissive && AssetPath.find("Material.009_Emissive.uasset") != FString::npos)
		{
			const FVector4 TargetColor(196.0f / 255.0f, 125.0f / 255.0f, 124.0f / 255.0f, 1.0f);
			const FVector4 CurrentColor = Material->GetVector4ParameterValue("SectionColor");
			if (std::fabs(CurrentColor.X - TargetColor.X) > 0.001f ||
				std::fabs(CurrentColor.Y - TargetColor.Y) > 0.001f ||
				std::fabs(CurrentColor.Z - TargetColor.Z) > 0.001f ||
				std::fabs(CurrentColor.W - TargetColor.W) > 0.001f)
			{
				Material->SetVector4Parameter("SectionColor", TargetColor);
				bDirty = true;
			}

			if (std::fabs(Material->GetScalarParameterValue("EmissiveIntensity") - 1.0f) > 0.001f)
			{
				Material->SetScalarParameter("EmissiveIntensity", 1.0f);
				bDirty = true;
			}
		}

		UTexture2D* NormalTex = Material->GetTextureParameterValue("NormalTexture");
		const FString NormalPath = NormalTex ? NormalTex->GetSourcePath() : FString();
		if (Material->GetScalarParameterValue("HasNormalMap") >= 0.5f && NormalPath.empty())
		{
			Material->SetScalarParameter("HasNormalMap", 0.0f);
			if (NormalTex)
			{
				Material->SetTextureParameter("NormalTexture", nullptr);
			}
			bDirty = true;
		}

		UTexture2D* DiffuseTex = Material->GetTextureParameterValue("DiffuseTexture");
		FVector4 SectionColor = Material->GetVector4ParameterValue("SectionColor");
		if (!bEmissive)
		{
			if (DiffuseTex)
			{
				if (SectionColor.X < 0.99f || SectionColor.Y < 0.99f || SectionColor.Z < 0.99f)
				{
					Material->SetVector4Parameter("SectionColor", FVector4(1.0f, 1.0f, 1.0f, 1.0f));
					bDirty = true;
				}
			}
			else if (SectionColor.X < 0.2f && SectionColor.Y < 0.2f && SectionColor.Z < 0.2f)
			{
				Material->SetVector4Parameter("SectionColor", FVector4(0.2f, 0.2f, 0.2f, 1.0f));
				bDirty = true;
			}
		}

		const bool bIsBloodDecal =
			AssetPath.find("blood-decal") != FString::npos ||
			AssetPath.find("drag-blood-decal") != FString::npos;
		if (bIsBloodDecal && DiffuseTex)
		{
			if (Material->GetBlendMode() != EBlendMode::Masked)
			{
				Material->SetDomainBlend(EMaterialDomain::Surface, EBlendMode::Masked);
				bDirty = true;
			}
			if (!Material->IsTwoSided())
			{
				Material->SetTwoSided(true);
				bDirty = true;
			}
			if (Material->GetScalarParameterValue("Opacity") < 0.99f)
			{
				Material->SetScalarParameter("Opacity", 1.0f);
				bDirty = true;
			}
			if (Material->GetScalarParameterValue("bEmissive") < 0.5f)
			{
				Material->SetScalarParameter("bEmissive", 1.0f);
				bDirty = true;
			}
			if (std::fabs(Material->GetScalarParameterValue("EmissiveIntensity") - 1.0f) > 0.001f)
			{
				Material->SetScalarParameter("EmissiveIntensity", 1.0f);
				bDirty = true;
			}
			if (Material->GetScalarParameterValue("AlphaClip") < 0.49f)
			{
				Material->SetScalarParameter("AlphaClip", 0.5f);
				bDirty = true;
			}
			const FVector4 White(1.0f, 1.0f, 1.0f, 1.0f);
			if (SectionColor.X < 0.99f || SectionColor.Y < 0.99f || SectionColor.Z < 0.99f)
			{
				Material->SetVector4Parameter("SectionColor", White);
				bDirty = true;
			}
		}

		if (bDirty)
		{
			Material->RebuildCachedSRVs();
			Manager.SaveMaterial(Material, AssetPath);
		}
	}

	// ".mat" → ".uasset" 정규화(이미 .uasset 이면 그대로). 캐시 키 + 바이너리 타겟.
	// 메시 임베드/하드코딩 legacy ".mat" 참조가 자동으로 ".uasset" 을 가리키게 한다.
	FString NormalizeMatToUasset(const FString& Path)
	{
		std::filesystem::path P(FPaths::ToWide(Path));
		if (P.extension() == L".mat") P.replace_extension(L".uasset");
		return FPaths::ToUtf8(P.generic_wstring());
	}
}

static EMaterialGraphTarget ResolveMaterialGraphCompileTarget(const UMaterial* Material);
static FString BuildGeneratedMaterialShaderPath(const FString& MaterialPath, bool bPreview, EMaterialGraphTarget Target);

void FMaterialManager::ScanMaterialAssets()
{
	AvailableMaterialFiles.clear();

	const std::filesystem::path MaterialRoot = FPaths::RootDir() + L"Content/Material/";

	if (!std::filesystem::exists(MaterialRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(MaterialRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();

		if (Path.extension() != L".uasset") continue;
		if (Path.stem() == L"None") continue; // Fallback 머티리얼은 목록에서 제외

		FMaterialAssetListItem Item;
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());

		// .uasset 은 메시/파티클도 공유하는 확장자 → 헤더 Type 으로 Material 만 거른다.
		EAssetPackageType PkgType = EAssetPackageType::Unknown;
		if (!FAssetPackage::GetPackageType(Item.FullPath, PkgType) || PkgType != EAssetPackageType::Material)
			continue;

		Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
		AvailableMaterialFiles.push_back(std::move(Item));
	}
}

void FMaterialManager::ScanShaderPaths()
{
	AvailableShaderPaths.clear();

	const std::filesystem::path ShaderRoot = FPaths::RootDir() + L"Shaders/";
	if (!std::filesystem::exists(ShaderRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());
	for (const auto& Entry : std::filesystem::recursive_directory_iterator(ShaderRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		if (Path.extension() != L".hlsl") continue; // .hlsli(include) 제외 — 독립 셰이더만

		AvailableShaderPaths.push_back(
			FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring()));
	}
}

void FMaterialManager::ScanTexturePaths()
{
	AvailableTexturePaths.clear();

	const std::filesystem::path TextureRoot = FPaths::RootDir() + L"Content/";
	if (!std::filesystem::exists(TextureRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());
	for (const auto& Entry : std::filesystem::recursive_directory_iterator(TextureRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		std::wstring Ext = Path.extension().wstring();
		for (wchar_t& Ch : Ext) if (Ch >= L'A' && Ch <= L'Z') Ch += (L'a' - L'A'); // ASCII 소문자화
		if (Ext != L".png" && Ext != L".jpg" && Ext != L".jpeg" && Ext != L".tga" && Ext != L".bmp" && Ext != L".dds") continue; // common image formats, 대소문자 무관

		AvailableTexturePaths.push_back(
			FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring()));
	}
}

UMaterial* FMaterialManager::GetOrCreateMaterial(const FString& MatFilePath)
{
	// 0. 경로 정규화: .mat → .uasset (캐시 키 = .uasset). legacy .mat 참조 호환.
	const FString UassetPath = NormalizeMatToUasset(MatFilePath);

	// 1. 캐시 반환
	auto It = MaterialCache.find(UassetPath);
	if (It != MaterialCache.end())
	{
		if (IsValid(It->second))
		{
			return It->second;
		}
		MaterialCache.erase(It);
	}

	// 2. 바이너리 .uasset 존재 시 우선 로드
	if (std::filesystem::exists(FPaths::ToWide(FPaths::MakeProjectRelative(UassetPath))))
	{
		if (UMaterial* Bin = LoadMaterialBinary(UassetPath))
		{
			MaterialCache.emplace(UassetPath, Bin);
			return Bin;
		}
	}

	// 3. 바이너리가 없으면 기본(핑크) 머티리얼. JSON 변환 경로는 제거됨
	//    (에셋은 .uasset 로 마이그레이션 완료, .mat JSON 미지원).
	UMaterial* DefaultMaterial = UObjectManager::Get().CreateObject<UMaterial>();
	FMaterialTemplate* Template = GetOrCreateTemplate(DefaultShaderPath);
	TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> Buffers = CreateConstantBuffers(Template);
	DefaultMaterial->Create(UassetPath, Template, EMaterialDomain::Surface, EBlendMode::Opaque, std::move(Buffers));
	DefaultMaterial->SetShaderPathForSerialize(DefaultShaderPath);
	DefaultMaterial->SetVector4Parameter("SectionColor", FVector4(1.0f, 0.0f, 1.0f, 1.0f));
	DefaultMaterial->SetScalarParameter("HasNormalMap", 0.0f);
	DefaultMaterial->SetScalarParameter("Opacity", 1.0f);
	DefaultMaterial->SetScalarParameter("bEmissive", 0.0f);
	DefaultMaterial->SetScalarParameter("EmissiveIntensity", 1.0f);
	DefaultMaterial->SetScalarParameter("SpecularIntensity", 1.0f);
	DefaultMaterial->SetScalarParameter("Shininess", 32.0f);
	DefaultMaterial->SetScalarParameter("Metallic", 0.0f);
	MaterialCache.emplace(UassetPath, DefaultMaterial);
	return DefaultMaterial;
}

// ============================================================
// 바이너리(.uasset) 직렬화 — exemplar = FParticleSystemManager
// ============================================================
bool FMaterialManager::SaveMaterial(UMaterial* Material, const FString& UassetPath)
{
	if (!Material) return false;

	const FString NormalizedPath = FPaths::MakeProjectRelative(UassetPath);
	FWindowsBinWriter Ar(NormalizedPath);
	if (!Ar.IsValid()) return false;

	FAssetImportMetadata Metadata;
    FAssetPackageHeader  WrittenHeader;
    if (!FAssetPackage::WritePackagePrelude(Ar, EAssetPackageType::Material, Metadata, &WrittenHeader))
	{
		return false;
	}

	UMaterialInstance* MI = Cast<UMaterialInstance>(Material);
	bool bIsInstance = (MI != nullptr);
	Ar << bIsInstance;

	FString PathFileName = Material->GetAssetPathFileName();
	Ar << PathFileName;

	if (bIsInstance)
	{
		FString ParentPath = MI->GetParent() ? NormalizeMatToUasset(MI->GetParent()->GetAssetPathFileName()) : FString();
		Ar << ParentPath;
	}
	else
	{
		FString ShaderPath = Material->GetShaderPathForSerialize();
		Ar << ShaderPath;
	}

    Material->Serialize(Ar, WrittenHeader.Version); // legacy runtime payload + optional graph/source payload
	return Ar.IsValid();
}

UMaterial* FMaterialManager::LoadMaterialBinary(const FString& UassetPath)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(UassetPath);
	FWindowsBinReader Ar(NormalizedPath);
	if (!Ar.IsValid()) return nullptr;

	FAssetPackageHeader Header;
	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadPackagePrelude(Ar, EAssetPackageType::Material, Header, Metadata)) return nullptr;

	bool bIsInstance = false;
	Ar << bIsInstance;
	FString PathFileName;
	Ar << PathFileName; // 저장된 id — 파일 rename 시 stale 가능. 머티리얼 id 는 실제 로드 경로(UassetPath) 사용.

	if (bIsInstance)
	{
		FString ParentPath;
		Ar << ParentPath;
		UMaterial* ParentMat = GetOrCreateMaterial(ParentPath);
		if (!ParentMat) return nullptr;

		UMaterialInstance* MI = UObjectManager::Get().CreateObject<UMaterialInstance>();
        MI->InitializeFromParent(ParentMat, UassetPath); // Template/CB 를 Parent 에서 복제. id=로드 경로(rename 안전)
        MI->Serialize(Ar, Header.Version);               // 복제된 CB 에 override/CPUData/텍스처 기록
		if (!Ar.IsValid()) { UObjectManager::Get().DestroyObject(MI); return nullptr; }
		return MI;
	}

	FString ShaderPath;
	Ar << ShaderPath;
	FMaterialTemplate* Template = GetOrCreateTemplate(ShaderPath);  // 순서 의존성: 먼저 Template
	if (!Template) return nullptr;
	auto Buffers = CreateConstantBuffers(Template);                // 빈 CB 선생성

	UMaterial* Material = UObjectManager::Get().CreateObject<UMaterial>();
	Material->Create(UassetPath, Template, EMaterialDomain::Surface, EBlendMode::Opaque, std::move(Buffers)); // id=로드 경로(rename 안전)
	Material->SetShaderPathForSerialize(ShaderPath);
    Material->Serialize(Ar, Header.Version); // Domain/BlendMode 등을 덮어쓰고 CPUData/source payload 를 기록
	if (!Ar.IsValid()) { UObjectManager::Get().DestroyObject(Material); return nullptr; }

	if (Material->WasCustomShaderRequested())
		Material->SetCustomShader(Template->GetShader());

    if (Material->IsGraphMaterial())
    {
        const EMaterialGraphTarget CompileTarget = ResolveMaterialGraphCompileTarget(Material);
        Material->GetGraphDocument().Graph.RepairPinsForDomain(CompileTarget);
        const FString ExpectedShaderPath = BuildGeneratedMaterialShaderPath(Material->GetAssetPathFileName(), false, CompileTarget);
        if (Material->GetShaderPathForSerialize() != ExpectedShaderPath ||
            Material->GetGraphDocument().LastCompiledShaderPath != ExpectedShaderPath)
        {
            FString CompileError;
            CompileMaterialGraphRuntime(Material, Material->GetGraphDocument().Graph, false, &CompileError);
        }
    }

	SanitizeLoadedImportMaterial(Material, *this);

	return Material;
}

// 임포터용 — JSON 없이 머티리얼을 직접 만들고 .uasset 으로 저장한다.
UMaterial* FMaterialManager::CreateImportedMaterialAsset(const FString& UassetPath, const FVector4& SectionColor,
	const FString& DiffuseTexturePath, const FString& NormalTexturePath,
	bool bEmissive, float EmissiveIntensity, bool bTransparent, bool bMasked, float Opacity,
	bool bTwoSided, float SpecularIntensity, float Shininess, float Metallic, float AlphaClip)
{
	MaterialCache.erase(UassetPath);

	FMaterialTemplate* Template = GetOrCreateTemplate(DefaultShaderPath);
	if (!Template) return nullptr;
	auto Buffers = CreateConstantBuffers(Template);

	UMaterial* Material = UObjectManager::Get().CreateObject<UMaterial>();
	const EBlendMode BlendMode = bMasked ? EBlendMode::Masked
		: (bTransparent ? EBlendMode::Transparent : EBlendMode::Opaque);
	Material->Create(UassetPath, Template, EMaterialDomain::Surface, BlendMode, std::move(Buffers));
	Material->SetShaderPathForSerialize(DefaultShaderPath);
	Material->SetVector4Parameter("SectionColor", SectionColor);
	Material->SetScalarParameter("HasNormalMap", 0.0f);
	Material->SetScalarParameter("Opacity", std::clamp(Opacity, 0.0f, 1.0f)); // CB zero-init=0(투명) 방지
	Material->SetScalarParameter("bEmissive", bEmissive ? 1.0f : 0.0f);
	Material->SetScalarParameter("EmissiveIntensity", bEmissive ? EmissiveIntensity : 1.0f);
	Material->SetScalarParameter("SpecularIntensity", std::clamp(SpecularIntensity, 0.0f, 4.0f));
	Material->SetScalarParameter("Shininess", std::clamp(Shininess, 2.0f, 256.0f));
	Material->SetScalarParameter("Metallic", std::clamp(Metallic, 0.0f, 1.0f));
	Material->SetScalarParameter("AlphaClip", std::clamp(AlphaClip, 0.0f, 1.0f));
	Material->SetTwoSided(bTwoSided);

	if (!DiffuseTexturePath.empty())
		if (UTexture2D* Tex = UTexture2D::LoadFromFile(DiffuseTexturePath, Device, ETextureColorSpace::SRGB))
			Material->SetTextureParameter("DiffuseTexture", Tex);
	if (!NormalTexturePath.empty())
		if (UTexture2D* Tex = UTexture2D::LoadFromFile(NormalTexturePath, Device, ETextureColorSpace::Linear))
		{
			Material->SetTextureParameter("NormalTexture", Tex);
			Material->SetScalarParameter("HasNormalMap", 1.0f);
		}

	Material->RebuildCachedSRVs();
	SaveMaterial(Material, UassetPath);
	MaterialCache[UassetPath] = Material;
	return Material;
}

// 에디터 Create Material 팩토리용 — 흰색 기본 머티리얼(텍스처 없음)을 생성·저장·캐시.
UMaterial* FMaterialManager::CreateMaterialAsset(const FString& UassetPath)
{
	return CreateImportedMaterialAsset(UassetPath, FVector4(1.0f, 1.0f, 1.0f, 1.0f), FString(), FString());
}

// 머티리얼의 셰이더(레이아웃 소스 & custom 대상)를 교체한다. 템플릿/CB 를 재구성하므로
// 레이아웃이 달라지면 파라미터 값은 초기화되고 텍스처 슬롯은 유지(RebuildCachedSRVs).
// 컴파일 실패(부적합 셰이더)거나 인스턴스면 변경을 거부한다.
bool FMaterialManager::SetMaterialShader(UMaterial* Material, const FString& ShaderPath, uint64 SourceHash, uint32 CompileRevision)
{
	if (!Material || Material->IsMaterialInstance())
		return false;

	FMaterialTemplate* Template = GetOrCreateTemplate(ShaderPath, SourceHash, CompileRevision);
	if (!Template)
		return false; // FindOrCreate 실패(엔트리포인트 없음/컴파일 오류 등) → 변경 거부

	auto Buffers = CreateConstantBuffers(Template);
	Material->Create(Material->GetAssetPathFileName(), Template,
		Material->GetDomain(), Material->GetBlendMode(), std::move(Buffers));
	Material->SetShaderPathForSerialize(ShaderPath);

	// 기본 UberLit 은 엔진이 (ViewMode×VertexFactory) 퍼뮤테이션으로 도출 가능 → custom 해제.
	// 그 외(非UberLit) 셰이더는 도출 불가 → custom 강제(인스펙터 레이아웃 = 실제 렌더 셰이더 일치 보장).
	Material->SetUseCustomShader(ShaderPath != DefaultShaderPath);

	Material->RebuildCachedSRVs();
	return true;
}

TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> FMaterialManager::CreateConstantBuffers(FMaterialTemplate* Template)
{

	TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> InjectedBuffers;

	const auto& RequiredBuffers = Template->GetParameterInfo();
	std::vector<FString> CreatedBuffers;

	for (const auto& BufferInfo : RequiredBuffers)
	{
		const FMaterialParameterInfo* ParamInfo = BufferInfo.second.get();

		if (std::find(CreatedBuffers.begin(), CreatedBuffers.end(), ParamInfo->BufferName) != CreatedBuffers.end())
			continue;

		auto MatCB = std::make_unique<FMaterialConstantBuffer>();
		MatCB->Init(Device, ParamInfo->BufferSize, ParamInfo->SlotIndex);

		InjectedBuffers.emplace(ParamInfo->BufferName, std::move(MatCB));
		CreatedBuffers.push_back(ParamInfo->BufferName);
	}

	return InjectedBuffers;
}

FMaterialTemplate* FMaterialManager::GetOrCreateTemplate(const FString& ShaderPath, uint64 SourceHash, uint32 CompileRevision)
{
    const FString CacheKey = (SourceHash == 0 && CompileRevision == 0)
            ? ShaderPath
            : ShaderPath + "#" + std::to_string(SourceHash) + "#" + std::to_string(CompileRevision);

	// 1. 템플릿이 캐시에 있는지 확인 (셰이더 경로 + revision 을 키값으로 사용)
	auto It = TemplateCache.find(CacheKey);
	if (It != TemplateCache.end())
	{
		FMaterialTemplate* CachedTemplate = It->second;
		if (CachedTemplate && CachedTemplate->GetShader() && CachedTemplate->GetShader()->IsValid())
		{
			return CachedTemplate;
		}

		delete CachedTemplate;
		TemplateCache.erase(It);
	}

	// 2. 템플릿이 기존에 없다면 새로 제작
	//    캐시에 있으면 반환, 없으면 컴파일 후 캐싱
	FShader* Shader = FShaderManager::Get().FindOrCreate(FShaderKey(ShaderPath, EShaderVertexFactory::Auto, SourceHash, CompileRevision));
	if (!Shader)
	{
		return nullptr;
	}

	FMaterialTemplate* NewTemplate = new FMaterialTemplate();
	NewTemplate->Create(Shader);
	TemplateCache.emplace(CacheKey, NewTemplate);
	return NewTemplate;
}

UMaterial* FMaterialManager::CreateGraphMaterialAsset(const FString& UassetPath)
{
    UMaterial* Material = CreateMaterialAsset(UassetPath);
    if (!Material)
    {
        return nullptr;
    }

    Material->EnableGraphMaterial();
    Material->GetGraphDocument().Target = EMaterialGraphTarget::Surface;
    Material->GetGraphDocument().Graph.InitializeDefault(EMaterialGraphTarget::Surface);
    Material->GetGraphDocument().LastSavedGraphHash = ComputeMaterialGraphStructuralHash(Material->GetGraphDocument().Graph);
    Material->GetMaterialSettings().Domain          = Material->GetDomain();
    Material->GetMaterialSettings().BlendMode       = Material->GetBlendMode();
    Material->GetMaterialSettings().ShadingModel    = EMaterialShadingModel::Unlit;
    Material->GetMaterialSettings().bReceiveLighting = false;
    SaveMaterial(Material, UassetPath);
    return Material;
}

UMaterial* FMaterialManager::CreatePreviewMaterialClone(UMaterial* SourceMaterial)
{
    if (!SourceMaterial)
    {
        return nullptr;
    }

    FMaterialTemplate* SourceTemplate = GetOrCreateTemplate(SourceMaterial->GetShaderPathForSerialize());
    if (!SourceTemplate)
    {
        return nullptr;
    }

    UMaterial* PreviewMaterial = UObjectManager::Get().CreateObject<UMaterial>();
    auto       Buffers         = CreateConstantBuffers(SourceTemplate);
    PreviewMaterial->Create("__material_graph_preview__", SourceTemplate, SourceMaterial->GetDomain(), SourceMaterial->GetBlendMode(), std::move(Buffers));
    PreviewMaterial->SetShaderPathForSerialize(SourceMaterial->GetShaderPathForSerialize());
    PreviewMaterial->SetSourceKind(SourceMaterial->GetSourceKind());
    PreviewMaterial->GetMaterialSettings() = SourceMaterial->GetMaterialSettings();

    for (const auto& Pair : *SourceMaterial->GetTexture())
    {
        PreviewMaterial->SetTextureParameter(Pair.first, Pair.second);
    }

    for (const FMaterialParameterValue& Value : SourceMaterial->GetRuntimeParameterStore().Values)
    {
        switch (Value.Type)
        {
        case EMaterialValueType::Float:
            PreviewMaterial->SetScalarParameter(Value.FallbackName, Value.Value.X);
            break;
        case EMaterialValueType::Float3:
        case EMaterialValueType::Color:
            PreviewMaterial->SetVector3Parameter(Value.FallbackName, FVector(Value.Value.X, Value.Value.Y, Value.Value.Z));
            break;
        case EMaterialValueType::Float4:
        default:
            PreviewMaterial->SetVector4Parameter(Value.FallbackName, Value.Value);
            break;
        }
    }
    PreviewMaterial->RebuildCachedSRVs();
    return PreviewMaterial;
}

static FString BuildMaterialGraphCompileHash(const FMaterialGraph& Graph, const FMaterialSettings& Settings, EMaterialGraphTarget Target)
{
    FString Hash = ComputeMaterialGraphStructuralHash(Graph);
    Hash += "#target=" + FString(ToString(Target));
    Hash += "#domain=" + std::to_string(static_cast<int>(Settings.Domain));
    Hash += "#blend=" + std::to_string(static_cast<int>(Settings.BlendMode));
    Hash += "#shading=" + std::to_string(static_cast<int>(Settings.ShadingModel));
    Hash += Settings.bTwoSided ? "#twosided=1" : "#twosided=0";
    Hash += Settings.bReceiveLighting ? "#lighting=1" : "#lighting=0";
    Hash += Settings.bCastShadow ? "#shadow=1" : "#shadow=0";
    Hash += "#mask=" + std::to_string(Settings.OpacityMaskClipValue);
    return Hash;
}

static uint64 MaterialGraphHashToKey(const FString& Hash)
{
    if (Hash.empty()) return 0;
    return std::hash<FString>{}(Hash + "#" + MaterialGraphGeneratorVersion);
}

static FString BuildGeneratedMaterialShaderPath(const FString& MaterialPath, bool bPreview, EMaterialGraphTarget Target)
{
    std::filesystem::path SourcePath(FPaths::ToWide(MaterialPath));
    FString               Stem = SourcePath.stem().string();
    if (Stem.empty())
    {
        Stem = "Material";
    }

    const FString TargetName = ToString(Target);
    return FString(bPreview ? "Shaders/Generated/Preview/Materials/" : "Shaders/Generated/Materials/")
            + Stem + "_" + TargetName + ".hlsl";
}

static EMaterialGraphTarget ResolveMaterialGraphCompileTarget(const UMaterial* Material)
{
    if (!Material)
    {
        return EMaterialGraphTarget::Surface;
    }

    switch (Material->GetMaterialSettings().Domain)
    {
    case EMaterialDomain::Decal:
        return EMaterialGraphTarget::Decal;
    case EMaterialDomain::PostProcess:
        return EMaterialGraphTarget::PostProcess;
    case EMaterialDomain::UI:
    case EMaterialDomain::Surface:
    default:
        return Material->GetGraphDocument().Target;
    }
}

// Editor session boot helper — clears the preview directory once so old preview .hlsl files
// from previous runs (different materials, renamed assets) don't accumulate on disk.
static void EnsurePreviewShaderDirCleared()
{
    static bool bCleared = false;
    if (bCleared) return;
    bCleared = true;

    const std::filesystem::path Dir = std::filesystem::path(FPaths::RootDir()) / L"Shaders" / L"Generated" / L"Preview" / L"Materials";
    std::error_code Ec;
    if (!std::filesystem::exists(Dir, Ec)) return;
    for (const auto& Entry : std::filesystem::directory_iterator(Dir, Ec))
    {
        if (!Entry.is_regular_file()) continue;
        if (Entry.path().extension() == L".hlsl")
        {
            std::filesystem::remove(Entry.path(), Ec);
        }
    }
}

static bool WriteGeneratedMaterialShaderFile(const FString& ProjectRelativePath, const FString& Hlsl, FString* OutError)
{
    const std::filesystem::path AbsolutePath = std::filesystem::path(FPaths::RootDir()) / FPaths::ToWide(ProjectRelativePath);
    std::filesystem::create_directories(AbsolutePath.parent_path());

    const std::filesystem::path TempPath = AbsolutePath.wstring() + L".tmp";
    {
        std::ofstream File(TempPath, std::ios::binary | std::ios::trunc);
        if (!File.is_open())
        {
            if (OutError) *OutError = "Failed to write generated material shader: " + ProjectRelativePath;
            return false;
        }
        File << Hlsl;
        if (!File.good())
        {
            if (OutError) *OutError = "Failed while writing generated material shader: " + ProjectRelativePath;
            return false;
        }
    }

    std::error_code Ec;
    std::filesystem::rename(TempPath, AbsolutePath, Ec);
    if (Ec)
    {
        std::filesystem::remove(AbsolutePath, Ec);
        Ec.clear();
        std::filesystem::rename(TempPath, AbsolutePath, Ec);
    }
    if (Ec)
    {
        if (OutError) *OutError = "Failed to replace generated material shader: " + ProjectRelativePath;
        return false;
    }
    return true;
}

static void SyncPreviewMaterialRuntimeSettings(UMaterial* PreviewMaterial, const UMaterial* SourceMaterial)
{
    if (!PreviewMaterial || !SourceMaterial)
    {
        return;
    }

    PreviewMaterial->SetDomainBlend(SourceMaterial->GetDomain(), SourceMaterial->GetBlendMode());
    PreviewMaterial->SetTwoSided(SourceMaterial->IsTwoSided());
    PreviewMaterial->GetMaterialSettings() = SourceMaterial->GetMaterialSettings();
}

static void ApplyGraphCompileParametersToMaterial(UMaterial* Material, const FMaterialCompileResult& Result, ID3D11Device* Device)
{
    if (!Material)
    {
        return;
    }

    for (const auto& Pair : Result.Parameters)
    {
        const FString&                    Name  = Pair.first;
        const FMaterialCompiledParameter& Param = Pair.second;
        switch (Param.Type)
        {
        case EMaterialGraphPinType::Float:
            Material->SetScalarParameter(Name, Param.Value.X);
            break;
        case EMaterialGraphPinType::Float2:
        case EMaterialGraphPinType::Float3:
        case EMaterialGraphPinType::Color:
            Material->SetVector3Parameter(Name, FVector(Param.Value.X, Param.Value.Y, Param.Value.Z));
            break;
        case EMaterialGraphPinType::Float4:
        default:
            Material->SetVector4Parameter(Name, Param.Value);
            break;
        }
    }

    for (const auto& Pair : Result.Textures)
    {
        const FString&                  Name = Pair.first;
        const FMaterialCompiledTexture& Tex  = Pair.second;
        if (Tex.Path.empty())
        {
            continue;
        }
        const bool bSRGB = MaterialTextureSlot::IsSRGBTextureSlot(ToString(Tex.Slot));
        if (UTexture2D* Texture = UTexture2D::LoadFromFile(Tex.Path, Device, bSRGB ? ETextureColorSpace::SRGB : ETextureColorSpace::Linear))
        {
            Material->SetTextureParameter(Name, Texture);
        }
    }
    Material->RebuildCachedSRVs();
}

static bool CompileGraphToShader(UMaterial* Material, const FMaterialGraph& WorkingGraph, bool bPreview, FMaterialCompileResult& OutResult, FString* OutError)
{
    if (!Material)
    {
        if (OutError) *OutError = "Invalid material.";
        return false;
    }

    if (bPreview) EnsurePreviewShaderDirCleared();
    const EMaterialGraphTarget CompileTarget = ResolveMaterialGraphCompileTarget(Material);
    FMaterialGraph GraphForCompile = WorkingGraph;
    GraphForCompile.RepairPinsForDomain(CompileTarget);
    const FString           Hash = BuildMaterialGraphCompileHash(GraphForCompile, Material->GetMaterialSettings(), CompileTarget);
    FMaterialCompileOptions Options;
    Options.MaterialPath      = Material->GetAssetPathFileName();
    Options.MaterialGuid      = Material->GetAssetPathFileName();
    Options.Domain            = CompileTarget;
    Options.RenderPass        = Material->GetRenderPass();
    Options.BlendState        = Material->GetBlendState();
    Options.BlendMode         = Material->GetBlendMode();
    Options.DepthStencilState = Material->GetDepthStencilState();
    Options.RasterizerState   = Material->GetRasterizerState();
    Options.bReceiveLighting     = Material->GetMaterialSettings().bReceiveLighting;
    Options.ShadingModel         = Material->GetMaterialSettings().ShadingModel;
    Options.OpacityMaskClipValue = Material->GetMaterialSettings().OpacityMaskClipValue;

    if (!FMaterialGraphCompiler::Compile(GraphForCompile, Options, OutResult))
    {
        if (OutError)
        {
            *OutError = OutResult.Errors.empty() ? FString("Material graph compile failed.") : OutResult.Errors.front();
        }
        return false;
    }

    OutResult.SourceHashString    = Hash;
    OutResult.SourceHash          = MaterialGraphHashToKey(Hash);
    OutResult.CompileRevision     = 0;
    OutResult.GeneratedShaderPath = BuildGeneratedMaterialShaderPath(Material->GetAssetPathFileName(), bPreview, Options.Domain);
    if (!WriteGeneratedMaterialShaderFile(OutResult.GeneratedShaderPath, OutResult.GeneratedHlsl, OutError))
    {
        return false;
    }

    return true;
}

FMaterialManager::~FMaterialManager()
{
    Release();
}

bool FMaterialManager::CompileMaterialGraphPreview(UMaterial* SourceMaterial, const FMaterialGraph& WorkingGraph, UMaterial*& InOutPreviewMaterial, FString* OutError)
{
    if (!SourceMaterial)
    {
        if (OutError) *OutError = "Invalid material.";
        return false;
    }

    if (!InOutPreviewMaterial)
    {
        InOutPreviewMaterial = CreatePreviewMaterialClone(SourceMaterial);
    }
    if (!InOutPreviewMaterial)
    {
        if (OutError) *OutError = "Failed to create preview material.";
        return false;
    }

    FMaterialCompileResult Result;
    if (!CompileGraphToShader(SourceMaterial, WorkingGraph, true, Result, OutError))
    {
        return false;
    }

    SyncPreviewMaterialRuntimeSettings(InOutPreviewMaterial, SourceMaterial);

    if (!SetMaterialShader(InOutPreviewMaterial, Result.GeneratedShaderPath, Result.SourceHash, Result.CompileRevision))
    {
        if (OutError) *OutError = "Generated preview shader failed to bind to material.";
        return false;
    }

    SyncPreviewMaterialRuntimeSettings(InOutPreviewMaterial, SourceMaterial);
    InOutPreviewMaterial->SetSourceKind(EMaterialSourceKind::Graph);
    ApplyGraphCompileParametersToMaterial(InOutPreviewMaterial, Result, Device);
    return true;
}

bool FMaterialManager::CompileMaterialGraphRuntime(UMaterial* Material, const FMaterialGraph& WorkingGraph, bool bPersistCompiledState, FString* OutError)
{
    if (!Material || Material->IsMaterialInstance())
    {
        if (OutError) *OutError = "Graph runtime compile requires a non-instance material.";
        return false;
    }

    FMaterialGraph GraphForPersist = WorkingGraph;
    GraphForPersist.RepairPinsForDomain(ResolveMaterialGraphCompileTarget(Material));

    FMaterialCompileResult Result;
    if (!CompileGraphToShader(Material, GraphForPersist, false, Result, OutError))
    {
        Material->GetLastCompileRecord().bSucceeded   = false;
        Material->GetLastCompileRecord().ErrorSummary = OutError ? *OutError : FString("Material graph compile failed.");
        return false;
    }

    if (!SetMaterialShader(Material, Result.GeneratedShaderPath, Result.SourceHash, Result.CompileRevision))
    {
        if (OutError) *OutError = "Generated runtime shader failed to bind to material.";
        Material->GetLastCompileRecord().bSucceeded   = false;
        Material->GetLastCompileRecord().ErrorSummary = OutError ? *OutError : FString("Generated runtime shader failed to bind to material.");
        return false;
    }

    Material->SetSourceKind(EMaterialSourceKind::Graph);
    Material->GetGraphDocument().bEnabled               = true;
    Material->GetGraphDocument().Graph                  = GraphForPersist;
    Material->GetGraphDocument().LastCompiledGraphHash  = Result.SourceHashString;
    Material->GetGraphDocument().LastCompiledShaderPath = Result.GeneratedShaderPath;
    Material->GetGraphDocument().LastCompileError.clear();
    Material->GetLastCompileRecord().SourceHash          = Material->GetGraphDocument().LastCompiledGraphHash;
    Material->GetLastCompileRecord().CompilerVersion     = MaterialGraphGeneratorVersion;
    Material->GetLastCompileRecord().GeneratedShaderPath = Result.GeneratedShaderPath;
    Material->GetLastCompileRecord().ErrorSummary.clear();
    Material->GetLastCompileRecord().bSucceeded = true;

    ApplyGraphCompileParametersToMaterial(Material, Result, Device);

    if (bPersistCompiledState)
    {
        SaveMaterial(Material, Material->GetAssetPathFileName());
    }
    return true;
}

void FMaterialManager::Release()
{
    if (bReleased)
    {
        return;
    }
    bReleased = true;

    // MaterialCache 는 UObject raw pointer 캐시다. GC 종료/씬 전환 중 캐시된
    // 머티리얼이 이미 PendingKill/Garbage 또는 완전 삭제된 상태일 수 있으므로
    // 절대 바로 Mat->ReleaseGPUBuffers() 로 역참조하지 않는다.
    for (auto& Pair : MaterialCache)
    {
        UMaterial* Mat = Pair.second;
        if (IsAliveObject(Mat))
        {
            Mat->ReleaseGPUBuffers();
        }
    }
    MaterialCache.clear();

    // TemplateCache 는 UMaterial::Template 이 non-owning 으로 가리킨다.
    // 살아 있는 UMaterial GPU buffer 해제를 먼저 끝낸 뒤 template 을 해제해야 한다.
    for (auto& Pair : TemplateCache)
    {
        delete Pair.second;
        Pair.second = nullptr;
    }
    TemplateCache.clear();

    // 외부에서 주입받은 리소스이므로 포인터만 초기화합니다.
    Device = nullptr;
}

void FMaterialManager::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (auto& Pair : MaterialCache)
    {
        if (IsValid(Pair.second))
        {
            Collector.AddReferencedObject(Pair.second, "FMaterialManager.MaterialCache");
        }
    }
}

void FMaterialManager::PurgeInvalidMaterialCacheEntries()
{
    for (auto It = MaterialCache.begin(); It != MaterialCache.end();)
    {
        UMaterial* Material = It->second;
        if (!IsValid(Material))
        {
            It = MaterialCache.erase(It);
            continue;
        }
        ++It;
    }
}
