#pragma once

#include "Object/Reflection/ObjectFactory.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Types/RenderStateTypes.h"
#include "Materials/MaterialDomain.h"
#include "Materials/MaterialSourceTypes.h"
#include "Render/Resource/Buffer.h"
#include "Render/Types/MaterialTextureSlot.h"
#include "Render/Types/RenderConstants.h"
#include "Source/Engine/Materials/Material.generated.h"
#include <memory>

class UTexture2D;
class FArchive;
class FShader;
class FReferenceCollector;
class UMaterialInstance;

// 파라미터 이름 → 상수 버퍼 내 위치 매핑
struct FMaterialParameterInfo
{
	FString BufferName;  // ConstantBuffers 이름 "PerMaterial""PerFrame"
	uint32 SlotIndex;    // ConstantBuffers 슬롯 인덱스 

	uint32 Offset;      // 버퍼 내 바이트 오프셋
	uint32 Size;        // 바이트 크기

	uint32 BufferSize;   //이 변수가 속한 상수 버퍼의 전체 크기 (16의 배수)
};


//셰이더 + 레이아웃 (불변, 공유)
//Template은 셰이더 파일이 있으면 언제든 재생성 가능
class FMaterialTemplate
{
private:
	uint32 MaterialTemplateID; // 고유 ID
	FShader* Shader; // 어떤 셰이더를 사용하는지
	TMap<FString, std::shared_ptr<FMaterialParameterInfo>> ParameterLayout; // 리플렉션 결과 : cbuffer 레이아웃(셰이더와 수명 공유)
	TArray<FShaderTextureBinding> TextureBindings;          // 리플렉션 결과 : t0~t7 텍스처 바인딩

public:
	const TMap<FString, std::shared_ptr<FMaterialParameterInfo>>& GetParameterInfo() const { return ParameterLayout; }
	const TArray<FShaderTextureBinding>& GetTextureBindings() const { return TextureBindings; }
	void Create(FShader* InShader);

	FShader* GetShader() const { return Shader; }
	bool GetParameterInfo(const FString& Name, FMaterialParameterInfo& OutInfo) const;
};


// 실제 데이터가 올라가는 버퍼
struct FMaterialConstantBuffer
{
	uint8* CPUData;   // CPU 메모리의 실제 값
	FConstantBuffer GPUBuffer;
	uint32 Size = 0;
	UINT SlotIndex = 0;	//cbuffer 바인딩 슬롯 (b0, b1 등)
	bool bDirty = false;

	FMaterialConstantBuffer() = default;
	~FMaterialConstantBuffer();

	FMaterialConstantBuffer(const FMaterialConstantBuffer&) = delete;
	FMaterialConstantBuffer& operator=(const FMaterialConstantBuffer&) = delete;

	void Init(ID3D11Device* InDevice, uint32 InSize, uint32 InSlot);
	void SetData(const void* Data, uint32 InSize, uint32 Offset = 0);
	void Upload(ID3D11DeviceContext* DeviceContext);
	void Release();

	FConstantBuffer* GetConstantBuffer() { return &GPUBuffer; }
};

//파라미터 값 + 텍스처 (런타임 데이터)
// 바이너리(.uasset)로 직렬화 — Manager 가 Serialize 를 위임
UCLASS()
class UMaterial : public UObject
{
	// UMaterialInstance가 Parent의 Template/CBMap을 깊은 복사할 때 직접 접근.
	friend class UMaterialInstance;
    friend class UMaterialInstanceDynamic;

protected:
	FString PathFileName;// 어떤 Material인지 판별하는 고유 이름
	uint32 MaterialInstanceID; // 고유 ID
	FMaterialTemplate* Template; // 공유
	FString ShaderPathForSerialize; // 파라미터 레이아웃 소스 셰이더 경로 (바이너리 직렬화/Template 재구성용)

    // 언리얼식 authoring source. Legacy runtime payload 와 분리하여 graph/source 데이터만 안전하게 저장한다.
    EMaterialSourceKind                  SourceKind = EMaterialSourceKind::EngineDefault;
    FMaterialSettings                    MaterialSettings;
    FMaterialGraphDocument               GraphDocument;
    TArray<FMaterialParameterDefinition> ParameterDefinitions;
    FMaterialRuntimeParameterStore       RuntimeParameterStore;
    FMaterialCompileRecord               LastCompileRecord;

	// 고수준 의도 (단일 소스) — 저수준 렌더상태는 ResolveMaterialRenderState 로 도출.
	EMaterialDomain Domain    = EMaterialDomain::Surface;
	EBlendMode      BlendMode = EBlendMode::Opaque;
	FMaterialRenderState CachedRenderState;  // = ResolveMaterialRenderState(Domain, BlendMode)

	// 도출로 표현 불가한 특수 케이스 override.
	//   .mat 일반 경로: raster 만 (스프라이트 NoCull 등). CreateTransient(Gizmo/Decal/Text): 4개 모두.
	bool bHasPassOverride   = false; ERenderPass        PassOverride   = ERenderPass::Opaque;
	bool bHasBlendOverride  = false; EBlendState        BlendOverride  = EBlendState::Opaque;
	bool bHasDepthOverride  = false; EDepthStencilState DepthOverride  = EDepthStencilState::Default;
	bool bHasRasterOverride = false; ERasterizerState   RasterOverride = ERasterizerState::SolidBackCull;

	void RecomputeRenderState() { CachedRenderState = ResolveMaterialRenderState(Domain, BlendMode); }

	TMap<FString, std::unique_ptr<FMaterialConstantBuffer>> ConstantBufferMap; // 인스턴스 고유
	TMap<FString, UTexture2D*> TextureParameters;  //텍스처는 슬롯 이름으로 관리

	FShader* TransientShader = nullptr; // CreateTransient / custom override 로 지정된 셰이더 (Template 없는 경우)
	bool     bUseCustomShader = false;  // true면 ResolveSectionShader 가 TransientShader 를 강제 (도출 우회)

	// Per-shader CB 오버라이드 — transient Material에서 프록시가 관리하는 외부 CB
	FConstantBufferBinding PerShaderOverride;

	// SRV 캐시 — SetTextureParameter 시 갱신, BuildCommandForProxy에서 map lookup 회피
	ID3D11ShaderResourceView* CachedSRVs[(int)EMaterialTextureSlot::Max] = {};

	virtual bool SetParameter(const FString& Name, const void* Data, uint32 Size);

public:
	GENERATED_BODY()
	~UMaterial() override;

	void Create(const FString& InPathFileName, FMaterialTemplate* InTemplate,
		EMaterialDomain InDomain,
		EBlendMode InBlendMode,
		TMap<FString, std::unique_ptr<FMaterialConstantBuffer>>&& InBuffers);

	const uint8* GetRawPtr(const FString& BufferName, uint32 Offset) const;

	const TMap<FString, std::shared_ptr<FMaterialParameterInfo>>& GetParameterInfo() const { return Template->GetParameterInfo(); }

	UFUNCTION(Callable, Category="Material|Parameters")
	virtual bool SetScalarParameter(const FString& ParamName, float Value);
	UFUNCTION(Callable, Category="Material|Parameters")
	virtual bool SetVector3Parameter(const FString& ParamName, const FVector& Value);
	UFUNCTION(Callable, Category="Material|Parameters")
	virtual bool SetVector4Parameter(const FString& ParamName, const FVector4& Value);
	UFUNCTION(Callable, Category="Material|Parameters")
	virtual bool SetTextureParameter(const FString& ParamName, UTexture2D* Texture);
	UFUNCTION(Callable, Category="Material|Parameters")
	virtual bool SetMatrixParameter(const FString& ParamName, const FMatrix& Value);

	UFUNCTION(Pure, Category="Material|Parameters")
	float GetScalarParameterValue(const FString& ParamName) const { float Value = 0.0f; GetScalarParameter(ParamName, Value); return Value; }
	UFUNCTION(Pure, Category="Material|Parameters")
	FVector GetVector3ParameterValue(const FString& ParamName) const { FVector Value(0, 0, 0); GetVector3Parameter(ParamName, Value); return Value; }
	UFUNCTION(Pure, Category="Material|Parameters")
	FVector4 GetVector4ParameterValue(const FString& ParamName) const { FVector4 Value{}; GetVector4Parameter(ParamName, Value); return Value; }
	UFUNCTION(Pure, Category="Material|Parameters")
	UTexture2D* GetTextureParameterValue(const FString& ParamName) const { UTexture2D* Value = nullptr; GetTextureParameter(ParamName, Value); return Value; }
	UFUNCTION(Pure, Category="Material|Parameters")
	FMatrix GetMatrixParameterValue(const FString& ParamName) const { FMatrix Value; GetMatrixParameter(ParamName, Value); return Value; }

	virtual bool GetScalarParameter(const FString& ParamName, float& OutValue) const;
	virtual bool GetVector3Parameter(const FString& ParamName, FVector& OutValue) const;
	virtual bool GetVector4Parameter(const FString& ParamName, FVector4& OutValue) const;
	virtual bool GetTextureParameter(const FString& ParamName, UTexture2D*& OutTexture) const;
	virtual bool GetMatrixParameter(const FString& ParamName, FMatrix& Value) const;

	void AddReferencedObjects(FReferenceCollector& Collector) override;

	TMap<FString, UTexture2D*>* GetTexture() { return &TextureParameters; }

	// 셰이더 리플렉션된 텍스처 슬롯(t0~t7) — 에디터 텍스처 편집 UI 용. Template 없으면 빈 목록.
	const TArray<FShaderTextureBinding>& GetTextureBindings() const;

	virtual FShader* GetShader() const { return Template ? Template->GetShader() : TransientShader; }

	// custom shader override — 머티리얼이 셰이더를 강제할 때(CreateTransient: Gizmo/Decal/Text,
	// 또는 비표준 셰이더 .mat). 없으면 엔진이 (Domain × VertexFactory × Pass × ViewMode)로 도출.
	bool     HasCustomShader() const { return bUseCustomShader && TransientShader != nullptr; }
	FShader* GetCustomShader() const { return TransientShader; }
	void     SetCustomShader(FShader* InShader) { TransientShader = InShader; bUseCustomShader = (InShader != nullptr); }
	// 직렬화에서 custom-shader '의도' 판정 (로드 직후 TransientShader 가 아직 null 이므로 플래그만 본다)
	bool     WasCustomShaderRequested() const { return bUseCustomShader; }
	// 에디터 토글 — ON 이면 머티리얼 자신의(레이아웃 소스) 셰이더를 강제 바인딩, OFF면 도출 복귀.
	void     SetUseCustomShader(bool bEnable) { SetCustomShader(bEnable ? GetShader() : nullptr); }

	// MaterialInstance 판별 (에디터: 인스턴스는 셰이더를 부모에서 상속하므로 셰이더/custom 변경 비활성).
	virtual bool IsMaterialInstance() const { return false; }

    virtual bool IsDynamicMaterialInstance() const
    {
        return false;
    }

    // Source/graph material authoring API — 저장 원본과 컴파일 결과를 분리하기 위한 진입점.
    EMaterialSourceKind GetSourceKind() const
    {
        return SourceKind;
    }

    void SetSourceKind(EMaterialSourceKind InSourceKind)
    {
        SourceKind = InSourceKind;
    }

    bool IsGraphMaterial() const
    {
        return SourceKind == EMaterialSourceKind::Graph && GraphDocument.bEnabled;
    }

    void EnableGraphMaterial()
    {
        SourceKind             = EMaterialSourceKind::Graph;
        GraphDocument.bEnabled = true;
        if (!GraphDocument.Graph.HasOutputNode())
        {
            GraphDocument.Graph.InitializeDefault(GraphDocument.Target);
        }
    }

    void DisableGraphMaterial()
    {
        GraphDocument.bEnabled = false;
        if (SourceKind == EMaterialSourceKind::Graph) SourceKind = EMaterialSourceKind::EngineDefault;
    }

    FMaterialSettings& GetMaterialSettings()
    {
        return MaterialSettings;
    }

    const FMaterialSettings& GetMaterialSettings() const
    {
        return MaterialSettings;
    }

    FMaterialGraphDocument& GetGraphDocument()
    {
        return GraphDocument;
    }

    const FMaterialGraphDocument& GetGraphDocument() const
    {
        return GraphDocument;
    }

    TArray<FMaterialParameterDefinition>& GetParameterDefinitions()
    {
        return ParameterDefinitions;
    }

    const TArray<FMaterialParameterDefinition>& GetParameterDefinitions() const
    {
        return ParameterDefinitions;
    }

    FMaterialRuntimeParameterStore& GetRuntimeParameterStore()
    {
        return RuntimeParameterStore;
    }

    const FMaterialRuntimeParameterStore& GetRuntimeParameterStore() const
    {
        return RuntimeParameterStore;
    }

    FMaterialCompileRecord& GetLastCompileRecord()
    {
        return LastCompileRecord;
    }

    const FMaterialCompileRecord& GetLastCompileRecord() const
    {
        return LastCompileRecord;
    }

    void MarkGraphSaved()
    {
        GraphDocument.LastSavedGraphHash = ComputeMaterialGraphStructuralHash(GraphDocument.Graph);
    }


	// 바이너리 직렬화용 셰이더 경로(레이아웃 소스) — Manager 가 Create 후 주입.
	void           SetShaderPathForSerialize(const FString& InPath) { ShaderPathForSerialize = InPath; }
	const FString& GetShaderPathForSerialize() const { return ShaderPathForSerialize; }
	virtual ERenderPass GetRenderPass() const { return bHasPassOverride ? PassOverride : CachedRenderState.Pass; }
	virtual EBlendState GetBlendState() const { return bHasBlendOverride ? BlendOverride : CachedRenderState.Blend; }
	virtual EDepthStencilState GetDepthStencilState() const { return bHasDepthOverride ? DepthOverride : CachedRenderState.DepthStencil; }
	virtual ERasterizerState GetRasterizerState() const { return bHasRasterOverride ? RasterOverride : CachedRenderState.Rasterizer; }

	// 고수준 의도 접근/설정
	EMaterialDomain GetDomain() const { return Domain; }
	EBlendMode GetBlendMode() const { return BlendMode; }

    void SetDomainBlend(EMaterialDomain InDomain, EBlendMode InBlend)
    {
        Domain                     = InDomain;
        BlendMode                  = InBlend;
        MaterialSettings.Domain    = InDomain;
        MaterialSettings.BlendMode = InBlend;
        RecomputeRenderState();
    }

	// 양면 렌더(Two Sided) — Raster override(NoCull) 슬롯 재사용(별도 직렬화 필드 불필요).
	// off면 override 해제 → 도출 Rasterizer(Surface=BackCull, Decal=NoCull 등)로 복귀.
	void SetTwoSided(bool bEnable)
	{
		if (bEnable) { bHasRasterOverride = true; RasterOverride = ERasterizerState::SolidNoCull; }
		else         { bHasRasterOverride = false; }
	}
	bool IsTwoSided() const { return bHasRasterOverride && RasterOverride == ERasterizerState::SolidNoCull; }

	// 저수준 override (도출 불가 케이스): CreateTransient / .mat raster 보존용.
	void SetPassOverride(ERenderPass InPass)      { bHasPassOverride   = true; PassOverride   = InPass; }
	void SetBlendOverride(EBlendState InBlend)    { bHasBlendOverride  = true; BlendOverride  = InBlend; }
	void SetDepthOverride(EDepthStencilState InD) { bHasDepthOverride  = true; DepthOverride  = InD; }
	void SetRasterOverride(ERasterizerState InR)  { bHasRasterOverride = true; RasterOverride = InR; }
	void ClearRenderStateOverrides() { bHasPassOverride = bHasBlendOverride = bHasDepthOverride = bHasRasterOverride = false; }

	// Per-shader CB 오버라이드 — transient Material에서 Gizmo/SubUV/Decal 등이 사용
	template<typename T>
	T& BindPerShaderCB(FConstantBuffer* Buffer, uint32 Slot)
	{
		return PerShaderOverride.Bind<T>(Buffer, Slot);
	}

	template<typename T>
	T& GetPerShaderAs() { return PerShaderOverride.As<T>(); }

	template<typename T>
	const T& GetPerShaderAs() const { return PerShaderOverride.As<T>(); }

	const FString& GetTexturePathFileName(const FString& TextureName)const;

	const FString& GetAssetPathFileName() const { return PathFileName; }

    void SetAssetPathFileName(const FString& InPath)
    {
        PathFileName = InPath;
    }

    void Serialize(FArchive& Ar) override;               // legacy wrapper
    void Serialize(FArchive& Ar, uint32 PackageVersion); //>>>>>Manager가 위임
    void SerializeMaterialSourcePayload(FArchive& Ar);

	virtual FConstantBuffer* GetGPUBufferBySlot(uint32 InSlot) const
	{
		// Per-shader override (transient Material의 외부 CB)
		if (PerShaderOverride.Buffer && PerShaderOverride.Slot == InSlot)
			return PerShaderOverride.Buffer;

		for (const auto& Pair : ConstantBufferMap)
		{
			if (Pair.second && Pair.second->SlotIndex == InSlot)
				return Pair.second->GetConstantBuffer();
		}
		return nullptr;
	}

	// dirty CB를 GPU에 업로드 — BuildCommandForProxy 전에 호출
	virtual void FlushDirtyBuffers(ID3D11Device* Device, ID3D11DeviceContext* Ctx)
	{
		for (auto& Pair : ConstantBufferMap)
		{
			if (Pair.second && Pair.second->bDirty)
				Pair.second->Upload(Ctx);
		}
		// Per-shader override CB (Gizmo/SubUV/Decal 등)
		if (PerShaderOverride.Buffer)
		{
			if (!PerShaderOverride.Buffer->GetBuffer())
				PerShaderOverride.Buffer->Create(Device, PerShaderOverride.Size);
			PerShaderOverride.Buffer->Update(Ctx, PerShaderOverride.Data, PerShaderOverride.Size);
		}
	}

	// 캐시된 SRV 배열 직접 접근 (map lookup 회피)
	virtual const ID3D11ShaderResourceView* const* GetCachedSRVs() const { return CachedSRVs; }

	// SRV 캐시 재구축 — Material 생성/텍스처 로드 후 호출
	void RebuildCachedSRVs();

	// CachedSRV 슬롯 직접 설정 — UTexture2D 없이 raw SRV를 바인딩할 때 사용
	void SetCachedSRV(EMaterialTextureSlot Slot, ID3D11ShaderResourceView* SRV) { CachedSRVs[(int)Slot] = SRV; }

	// Device 해제 전 GPU 버퍼만 명시적으로 해제 (UObject 수명은 UObjectManager가 관리)
	void ReleaseGPUBuffers()
	{
		for (auto& Pair : ConstantBufferMap)
		{
			if (Pair.second)
			{
				Pair.second->Release();
			}
		}
		for (int s = 0; s < (int)EMaterialTextureSlot::Max; ++s)
		{
			CachedSRVs[s] = nullptr;
		}
	}

	// Template/CB 없는 경량 머티리얼 생성 — SRV만 래핑할 때 사용
	// InShader를 지정하면 GetShader()가 해당 셰이더를 반환 (DrawCommandBuilder per-section 셰이더 지원)
	static UMaterial* CreateTransient(ERenderPass InPass, EBlendState InBlend,
		EDepthStencilState InDepth = EDepthStencilState::Default,
		ERasterizerState InRaster = ERasterizerState::SolidBackCull,
		FShader* InShader = nullptr);
};
