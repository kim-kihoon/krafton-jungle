#include "Materials/Material.h"
#include "Serialization/Archive.h"
#include "Render/Shader/Shader.h"
#include "Texture/Texture2D.h"
#include "Object/GarbageCollection.h"
#include "Engine/Runtime/Engine.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Types/MaterialTextureSlot.h"
#include "Asset/AssetPackage.h"
#include <cstring>

// ─── FMaterialTemplate ───

void FMaterialTemplate::Create(FShader* InShader)
{
	ParameterLayout = InShader->GetParameterLayout(); // 셰이더에서 리플렉션된 파라미터 레이아웃 정보 확보
	TextureBindings = InShader->GetTextureBindings(); // t0~t7 텍스처 바인딩도 확보
	Shader = InShader;
}

bool FMaterialTemplate::GetParameterInfo(const FString& Name, FMaterialParameterInfo& OutInfo) const
{
	auto it = ParameterLayout.find(Name);
	if (it != ParameterLayout.end())
	{
		OutInfo = *(it->second);
		return true;
	}
	else
	{
		return false;
	}
}

// ─── FMaterialConstantBuffer ───

FMaterialConstantBuffer::~FMaterialConstantBuffer()
{
	Release();
}

void FMaterialConstantBuffer::Init(ID3D11Device* InDevice, uint32 InSize, uint32 InSlot)
{
	Release();

	uint32 AlignedSize = (InSize + 15) & ~15;
	GPUBuffer.Create(InDevice, AlignedSize, "MaterialGPUBuffer");
	CPUData = new uint8[AlignedSize]();
	Size = AlignedSize;
	SlotIndex = InSlot;
	bDirty = true;
}

void FMaterialConstantBuffer::SetData(const void* Data, uint32 InSize, uint32 Offset)
{
	if (!CPUData || Offset + InSize > Size)
	{
		return;
	}
	memcpy(CPUData + Offset, Data, InSize);
	bDirty = true;
}

void FMaterialConstantBuffer::Upload(ID3D11DeviceContext* DeviceContext)
{
	if (!bDirty)
		return;

	GPUBuffer.Update(DeviceContext, CPUData, Size);
	bDirty = false;
}

void FMaterialConstantBuffer::Release()
{
	GPUBuffer.Release();
	delete[] CPUData;
	CPUData = nullptr;
	Size = 0;
	bDirty = false;
}

// ─── UMaterial ───

UMaterial::~UMaterial()
{
	ReleaseGPUBuffers();
	ConstantBufferMap.clear();

	for (auto& Pair : TextureParameters)
	{
		Pair.second = nullptr;
	}
	TextureParameters.clear();
}

void UMaterial::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);
	for (auto& Pair : TextureParameters)
	{
		Collector.AddReferencedObject(Pair.second, "UMaterial::TextureParameters");
	}
}

void UMaterial::Create(const FString& InPathFileName, FMaterialTemplate* InTemplate,
	EMaterialDomain InDomain,
	EBlendMode InBlendMode,
	TMap<FString, std::unique_ptr<FMaterialConstantBuffer>>&& InBuffers)
{
	PathFileName = InPathFileName;
	Template = InTemplate;
	Domain = InDomain;
	BlendMode = InBlendMode;
	RecomputeRenderState();  // 저수준 렌더상태 도출
    MaterialSettings.Domain    = InDomain;
    MaterialSettings.BlendMode = InBlendMode;

	ConstantBufferMap = std::move(InBuffers);
}

bool UMaterial::SetParameter(const FString& Name, const void* Data, uint32 Size)
{
	FMaterialParameterInfo Info;
    if (!Template || !Template->GetParameterInfo(Name, Info))
    {
		return false;
	}
	auto It = ConstantBufferMap.find(Info.BufferName);
	if (It == ConstantBufferMap.end()) return false;

	It->second->SetData(Data, Size, Info.Offset); // SetData 가 bDirty 설정. 업로드는 draw-build 의 FlushDirtyBuffers 가 dirty CB 만 일괄 처리.
	return true;
}


bool UMaterial::SetScalarParameter(const FString& ParamName, float Value)
{
    RuntimeParameterStore.SetValue(ParamName, EMaterialValueType::Float, FVector4(Value, 0.0f, 0.0f, 0.0f));
	return SetParameter(ParamName, &Value, sizeof(float));
}

bool UMaterial::SetVector3Parameter(const FString& ParamName, const FVector& Value)
{
	float Data[3] = { Value.X, Value.Y, Value.Z };
    RuntimeParameterStore.SetValue(ParamName, EMaterialValueType::Float3, FVector4(Value.X, Value.Y, Value.Z, 0.0f));
	return SetParameter(ParamName, Data, sizeof(Data));
}

bool UMaterial::SetVector4Parameter(const FString& ParamName, const FVector4& Value)
{
	float Data[4] = { Value.X, Value.Y, Value.Z, Value.W };
    RuntimeParameterStore.SetValue(ParamName, EMaterialValueType::Float4, Value);
	return SetParameter(ParamName, Data, sizeof(Data));
}

bool UMaterial::SetTextureParameter(const FString& ParamName, UTexture2D* Texture)
{
	TextureParameters[ParamName] = Texture;
    RuntimeParameterStore.SetValue(ParamName, EMaterialValueType::Texture2D, FVector4(0.0f, 0.0f, 0.0f, 0.0f), Texture ? Texture->GetSourcePath() : FString());

	// 리플렉션 텍스처 바인딩(이름→register)으로 CachedSRV 즉시 갱신 — RebuildCachedSRVs 와 동일 규칙.
	for (const FShaderTextureBinding& B : GetTextureBindings())
	{
		if (B.Name == ParamName && B.BindPoint < (uint32)EMaterialTextureSlot::Max)
		{
			CachedSRVs[B.BindPoint] = (Texture && Texture->GetSRV()) ? Texture->GetSRV() : nullptr;
			break;
		}
	}

	return true;
}

bool UMaterial::SetMatrixParameter(const FString& ParamName, const FMatrix& Value)
{
	return SetParameter(ParamName, Value.Data, sizeof(float) * 16);
}

bool UMaterial::GetScalarParameter(const FString& ParamName, float& OutValue) const
{
	FMaterialParameterInfo Info;
    if (!Template || !Template->GetParameterInfo(ParamName, Info)) return false;

	auto It = ConstantBufferMap.find(Info.BufferName);
	if (It == ConstantBufferMap.end()) return false;

	const uint8* Ptr = It->second->CPUData + Info.Offset;
	OutValue = *reinterpret_cast<const float*>(Ptr);
	return true;
}

bool UMaterial::GetVector3Parameter(const FString& ParamName, FVector& OutValue) const
{
	FMaterialParameterInfo Info;
    if (!Template || !Template->GetParameterInfo(ParamName, Info)) return false;

	auto It = ConstantBufferMap.find(Info.BufferName);
	if (It == ConstantBufferMap.end()) return false;

	const uint8* Ptr = It->second->CPUData + Info.Offset;
	OutValue = *reinterpret_cast<const FVector*>(Ptr);
	return true;
}

bool UMaterial::GetVector4Parameter(const FString& ParamName, FVector4& OutValue) const
{
	FMaterialParameterInfo Info;
    if (!Template || !Template->GetParameterInfo(ParamName, Info)) return false;

	auto It = ConstantBufferMap.find(Info.BufferName);
	if (It == ConstantBufferMap.end()) return false;

	const uint8* Ptr = It->second->CPUData + Info.Offset;
	OutValue = *reinterpret_cast<const FVector4*>(Ptr);
	return true;
}

bool UMaterial::GetTextureParameter(const FString& ParamName, UTexture2D*& OutTexture) const
{
	auto It = TextureParameters.find(ParamName);
	if (It == TextureParameters.end()) return false;

	OutTexture = It->second;
	return true;
}

bool UMaterial::GetMatrixParameter(const FString& ParamName, FMatrix& Value) const
{
	FMaterialParameterInfo Info;
    if (!Template || !Template->GetParameterInfo(ParamName, Info)) return false;

	auto It = ConstantBufferMap.find(Info.BufferName);
	if (It == ConstantBufferMap.end()) return false;

	const uint8* Ptr = It->second->CPUData + Info.Offset;
	memcpy(Value.Data, Ptr, sizeof(float) * 16);
	return true;
}

const FString& UMaterial::GetTexturePathFileName(const FString& TextureName)const
{
	auto it = TextureParameters.find(TextureName);
	if (it != TextureParameters.end())
	{
		UTexture2D* Texture = it->second;
		if(Texture)
		{
			return Texture->GetSourcePath();
		}
	}
	static const FString EmptyString;
	return EmptyString;
}

const TArray<FShaderTextureBinding>& UMaterial::GetTextureBindings() const
{
	static const TArray<FShaderTextureBinding> Empty;
	return Template ? Template->GetTextureBindings() : Empty;
}

void UMaterial::RebuildCachedSRVs()
{
	for (int s = 0; s < (int)EMaterialTextureSlot::Max; s++)
		CachedSRVs[s] = nullptr;

	const TArray<FShaderTextureBinding>& Bindings = GetTextureBindings();
	if (!Bindings.empty())
	{
		// 리플렉션된 바인딩: 텍스처 변수명 → register(t#) 로 SRV 배치 (셰이더 선언 그대로).
		for (const FShaderTextureBinding& B : Bindings)
		{
			if (B.BindPoint >= (uint32)EMaterialTextureSlot::Max) continue;
			UTexture2D* Tex = nullptr;
			if (GetTextureParameter(B.Name, Tex) && Tex && Tex->GetSRV())
			{
				CachedSRVs[B.BindPoint] = Tex->GetSRV();
				continue;
			}
			constexpr const char* GraphTexturePrefix = "Tex_";
			if (B.Name.rfind(GraphTexturePrefix, 0) == 0)
			{
				FString SlotAlias = B.Name.substr(strlen(GraphTexturePrefix)) + "Texture";
				if (GetTextureParameter(SlotAlias, Tex) && Tex && Tex->GetSRV())
				{
					CachedSRVs[B.BindPoint] = Tex->GetSRV();
				}
			}
		}
	}
	else
	{
		// 폴백(Template 없는 TransientShader 등): 고정 enum 규칙.
		for (int s = 0; s < (int)EMaterialTextureSlot::Max; s++)
		{
			UTexture2D* Tex = nullptr;
			FString SlotName = MaterialTextureSlot::ToString(s) + "Texture";
			if (GetTextureParameter(SlotName, Tex) && Tex && Tex->GetSRV())
				CachedSRVs[s] = Tex->GetSRV();
		}
	}
}

void UMaterial::Serialize(FArchive& Ar)
{
    Serialize(Ar, FAssetPackageHeader::CurrentVersion);
}

void UMaterial::Serialize(FArchive& Ar, uint32 PackageVersion)
{
	// [Phase 4] 고수준 의도 + custom-shader 플래그 + 저수준 override.
	// PathFileName/ShaderPath 는 Manager 가 헤더 영역에서 처리한다
	// (로드 순서 의존성: ShaderPath→Template→CB 생성 후에야 아래 CPUData 를 기록 가능).
	uint8 DomainRaw = static_cast<uint8>(Domain);
	uint8 BlendRaw  = static_cast<uint8>(BlendMode);
	Ar << DomainRaw;
	Ar << BlendRaw;
	if (Ar.IsLoading())
		SetDomainBlend(static_cast<EMaterialDomain>(DomainRaw), static_cast<EBlendMode>(BlendRaw));

	Ar << bUseCustomShader; // 의도 플래그 (런타임 FShader* 는 Manager 가 Template 에서 재바인딩)

	// 저수준 override 슬롯 (스프라이트 NoCull, CreateTransient 등 도출 불가 케이스)
	{
		uint32 PassV   = static_cast<uint32>(PassOverride);
		uint8  BlendV  = static_cast<uint8>(BlendOverride);
		uint8  DepthV  = static_cast<uint8>(DepthOverride);
		uint8  RasterV = static_cast<uint8>(RasterOverride);
		Ar << bHasPassOverride;   Ar << PassV;
		Ar << bHasBlendOverride;  Ar << BlendV;
		Ar << bHasDepthOverride;  Ar << DepthV;
		Ar << bHasRasterOverride; Ar << RasterV;
		if (Ar.IsLoading())
		{
			PassOverride   = static_cast<ERenderPass>(PassV);
			BlendOverride  = static_cast<EBlendState>(BlendV);
			DepthOverride  = static_cast<EDepthStencilState>(DepthV);
			RasterOverride = static_cast<ERasterizerState>(RasterV);
		}
	}

	uint32 BufferCount = static_cast<uint32>(ConstantBufferMap.size());
	Ar << BufferCount;

	if (Ar.IsSaving())
	{
		for (auto& Pair : ConstantBufferMap)
		{
			FString BufferName = Pair.first;
			uint32 Size = Pair.second->Size;

			Ar << BufferName;
			Ar << Size;
			Ar.Serialize(Pair.second->CPUData, Size);
		}
	}

	if (Ar.IsLoading())
	{
		for (uint32 i = 0; i < BufferCount; ++i)
		{
			FString BufferName;
			uint32 Size = 0;

			Ar << BufferName;
			Ar << Size;

			auto It = ConstantBufferMap.find(BufferName);
			if (It != ConstantBufferMap.end())
			{
				Ar.Serialize(It->second->CPUData, Size);
				It->second->bDirty = true;
				It->second->Upload(GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext());
			}
			else
			{
				TArray<uint8> Dummy(Size);
				Ar.Serialize(Dummy.data(), Size);
			}
		}
	}
	
	uint32 TextureCount = static_cast<uint32>(TextureParameters.size());
	Ar << TextureCount;

	if (Ar.IsSaving())
	{
		for (auto& Pair : TextureParameters)
		{
			FString SlotName = Pair.first;
			FString TexturePath = Pair.second ? Pair.second->GetSourcePath() : FString();

			Ar << SlotName;
			Ar << TexturePath;
		}
	}
	else // IsLoading
	{
		for (uint32 i = 0; i < TextureCount; ++i)
		{
			FString SlotName;
			FString TexturePath;

			Ar << SlotName;
			Ar << TexturePath;

			if (!TexturePath.empty())
			{
				ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
				const bool bIsColorTexture = MaterialTextureSlot::IsSRGBTextureSlot(SlotName);
				UTexture2D* Loaded = UTexture2D::LoadFromFile(
					TexturePath,
					Device,
					bIsColorTexture ? ETextureColorSpace::SRGB : ETextureColorSpace::Linear);
				if (Loaded)
				{
					TextureParameters[SlotName] = Loaded;
				}
			}
		}

		RebuildCachedSRVs();
	}

    if (PackageVersion >= static_cast<uint32>(EAssetPackageSerializationVersion::MaterialGraphSourcePayload))
    {
        SerializeMaterialSourcePayload(Ar);
    }
}

void UMaterial::SerializeMaterialSourcePayload(FArchive& Ar)
{
    Ar << SourceKind;
    Ar << MaterialSettings;
    Ar << GraphDocument.bEnabled;
    Ar << GraphDocument.Target;
    Ar << GraphDocument.Graph;
    Ar << GraphDocument.EditorSettings;
    Ar << GraphDocument.LastSavedGraphHash;
    Ar << GraphDocument.LastCompiledGraphHash;
    Ar << GraphDocument.LastCompiledShaderPath;
    Ar << GraphDocument.LastCompileError;
    Ar << GraphDocument.bAutoPreview;
    Ar << GraphDocument.bPreviewDirty;
    Ar << ParameterDefinitions;
    Ar << RuntimeParameterStore;
    Ar << LastCompileRecord;

    if (Ar.IsLoading())
    {
        Domain    = MaterialSettings.Domain;
        BlendMode = MaterialSettings.BlendMode;
        RecomputeRenderState();
        if (GraphDocument.bEnabled)
        {
            SourceKind = EMaterialSourceKind::Graph;
        }
    }
}

UMaterial* UMaterial::CreateTransient(ERenderPass InPass, EBlendState InBlend,
	EDepthStencilState InDepth, ERasterizerState InRaster, FShader* InShader)
{
	UMaterial* Mat = UObjectManager::Get().CreateObject<UMaterial>();
	TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> EmptyBuffers;
	// Transient(Gizmo/Decal/Text/SubUV)는 Domain 으로 표현되지 않는 고정 패스/상태를 쓰므로
	// 저수준 4개를 모두 override 로 박아 정확히 보존한다 (Domain/BlendMode 는 무의미).
	Mat->Create(FString("__transient__"), nullptr, EMaterialDomain::Surface, EBlendMode::Opaque, std::move(EmptyBuffers));
	Mat->SetPassOverride(InPass);
	Mat->SetBlendOverride(InBlend);
	Mat->SetDepthOverride(InDepth);
	Mat->SetRasterOverride(InRaster);
	Mat->SetCustomShader(InShader);  // InShader!=null 이면 custom override 강제 (Gizmo/Decal/Text/SubUV)
	return Mat;
}
