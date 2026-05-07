#pragma once

#include "Object/Object.h"
#include "Texture.h"
#include "Shader.h"
#include "RenderResources.h"
#include <variant>

/**
 * @brief MTL 파일의 머테리얼 데이터를 표현하는 구조체.
 * Obj .mtl 포맷 기준으로 정의했습니다.
 */

struct FMaterial
{
    FString Name;

    FVector BaseColor      = { 0.8f, 0.8f, 0.8f }; // Kd
    FVector SpecularColor  = { 0.0f, 0.0f, 0.0f }; // Ks
    FVector EmissiveColor  = { 0.0f, 0.0f, 0.0f }; // Ke

    float Shininess  = 30.0f; 
    float Opacity    = 1.0f; 
    int   IllumModel = 2;    

	// Texture 정보
    FString DiffuseTexPath;   // map_Kd
	bool	bHasDiffuseTexture = { false };

    FString SpecularTexPath;  // map_Ks
	bool	bHasSpecularTexture = { false };

	FString NormalTexPath;     // norm / map_norm / map_Kn
	bool	bHasNormalTexture = { false };

	FString BumpTexPath;      // map_bump
	bool	bHasBumpTexture = { false };
};

enum class EMaterialParamType
{
	Bool,
	Int,
	UInt,
	Float,
	Vector2,
	Vector3,
	Vector4,
	Matrix4,
	Texture,
};

struct FMaterialParamValue
{
	FMaterialParamValue() : Type(EMaterialParamType::Float), Value(0.0f) {}
	FMaterialParamValue(bool InBool) : Type(EMaterialParamType::Bool), Value(InBool) {}
	FMaterialParamValue(int32 InInt) : Type(EMaterialParamType::Int), Value(InInt) {}
	FMaterialParamValue(uint32 InUInt) : Type(EMaterialParamType::UInt), Value(InUInt) {}
	FMaterialParamValue(float InScalar) : Type(EMaterialParamType::Float), Value(InScalar) {}
	FMaterialParamValue(const FVector2& InVector2) : Type(EMaterialParamType::Vector2), Value(InVector2) {}
	FMaterialParamValue(const FVector& InVector3) : Type(EMaterialParamType::Vector3), Value(InVector3) {}
	FMaterialParamValue(const FVector4& InVector4) : Type(EMaterialParamType::Vector4), Value(InVector4) {}
	FMaterialParamValue(const FMatrix& InMatrix4) : Type(EMaterialParamType::Matrix4), Value(InMatrix4) {}
	FMaterialParamValue(UTexture* InTexture) : Type(EMaterialParamType::Texture), Value(InTexture) {}

	EMaterialParamType Type;
	std::variant<bool, int32, uint32, float, FVector2, FVector, FVector4, FMatrix, UTexture*> Value;
};

enum class EMaterialInstanceOwnership : uint8
{
	ResourceManaged,
	ComponentTransient,
};

enum class ELightingModel : uint8
{
	Gouraud = 0,
	Lambert,
	Phong,
	Toon,
};

enum class EMaterialDomain : uint8
{
	Surface = 0,
	Decal,
};

const char* ToMaterialDomainString(EMaterialDomain MaterialDomain);
bool TryParseMaterialDomain(const FString& Value, EMaterialDomain& OutMaterialDomain);
const char* ToLightingModelString(ELightingModel LightingModel);
bool TryParseLightingModel(const FString& Value, ELightingModel& OutLightingModel);
FShaderCompileKey MakeUberLitShaderCompileKey(EMaterialDomain MaterialDomain, ELightingModel LightingModel = ELightingModel::Phong);
inline FShaderCompileKey MakeUberLitShaderCompileKey(ELightingModel LightingModel)
{
	return MakeUberLitShaderCompileKey(EMaterialDomain::Surface, LightingModel);
}
struct FRenderPassContext;

class UMaterialInterface : public UObject
{
public:
	DECLARE_CLASS(UMaterialInterface, UObject)

	virtual const FString& GetName() const = 0;
	virtual FString& GetNameRef() = 0;
	virtual const FString& GetFilePath() const = 0;
	virtual FString& GetFilePathRef() = 0;
	
	void Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus = nullptr, const FPerObjectConstants* PerObject = nullptr) const
	{
		Bind(Context, RenderBus, PerObject, nullptr, nullptr, nullptr);
	}

	virtual void Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus, const FPerObjectConstants* PerObject, UShader* ShaderOverride, const FRenderPassContext* PassContext = nullptr, const FDecalConstants* DecalConstants = nullptr) const = 0;
	virtual bool GetParam(const FString& Name, FMaterialParamValue& OutValue) const = 0;
	virtual EMaterialDomain GetEffectiveMaterialDomain() const = 0;
	virtual ELightingModel GetEffectiveLightingModel() const = 0;

	virtual void SetParam(const FString& Name, const FMaterialParamValue& Value) = 0;

	void SetBool(const FString& Name, bool Value) { SetParam(Name, FMaterialParamValue(Value)); }
	void SetInt(const FString& Name, int32 Value) { SetParam(Name, FMaterialParamValue(Value)); }
	void SetUInt(const FString& Name, uint32 Value) { SetParam(Name, FMaterialParamValue(Value)); }
	void SetFloat(const FString& Name, float Value) { SetParam(Name, FMaterialParamValue(Value)); }
	void SetVector2(const FString& Name, const FVector2& Value) { SetParam(Name, FMaterialParamValue(Value)); }
	void SetVector3(const FString& Name, const FVector& Value) { SetParam(Name, FMaterialParamValue(Value)); }
	void SetVector4(const FString& Name, const FVector4& Value) { SetParam(Name, FMaterialParamValue(Value)); }
	void SetMatrix4(const FString& Name, const FMatrix& Value) { SetParam(Name, FMaterialParamValue(Value)); }
	void SetTexture(const FString& Name, UTexture* Value) { SetParam(Name, FMaterialParamValue(Value)); }

	virtual void GatherAllParams(TMap<FString, FMaterialParamValue>& OutParams) const = 0;
};

class UMaterial : public UMaterialInterface
{
public:
	DECLARE_CLASS(UMaterial, UMaterialInterface)

	FString Name;
	FString FilePath;

	FMaterial MaterialData;
	TMap<FString, FMaterialParamValue> MaterialParams;

	UShader* Shader = nullptr;
	mutable std::shared_ptr<FShaderBindingInstance> ShaderBinding;

	ESamplerType SamplerType = ESamplerType::EST_Linear;
	EDepthStencilType DepthStencilType = EDepthStencilType::Default;
	EBlendType BlendType = EBlendType::Opaque;
	ERasterizerType RasterizerType = ERasterizerType::SolidBackCull;
	D3D11_PRIMITIVE_TOPOLOGY PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	EMaterialDomain MaterialDomain = EMaterialDomain::Surface;
	ELightingModel LightingModel = ELightingModel::Phong;

	const FString& GetName() const override { return Name; }
	FString& GetNameRef() override { return Name; }
	const FString& GetFilePath() const override { return FilePath; }
	FString& GetFilePathRef() override { return FilePath; }
	EMaterialDomain GetEffectiveMaterialDomain() const override { return MaterialDomain; }
	ELightingModel GetEffectiveLightingModel() const override { return LightingModel; }

	void SetShader(UShader* InShader)
	{
		Shader = InShader;
		ShaderBinding.reset();
	}

	void SetLightingModel(ELightingModel InLightingModel)
	{
		LightingModel = InLightingModel;
	}

	void SetMaterialDomain(EMaterialDomain InMaterialDomain)
	{
		MaterialDomain = InMaterialDomain;
	}

	void SetParam(const FString& Name, const FMaterialParamValue& Value)
	{
		MaterialParams[Name] = Value;
	}
	virtual bool GetParam(const FString& Name, FMaterialParamValue& OutValue) const
	{
		auto It = MaterialParams.find(Name);
		if (It != MaterialParams.end())
		{
			OutValue = It->second;
			return true;
		}
		return false;
	}

	void Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus = nullptr, const FPerObjectConstants* PerObject = nullptr) const;
	void Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus, const FPerObjectConstants* PerObject, UShader* ShaderOverride, const FRenderPassContext* PassContext = nullptr, const FDecalConstants* DecalConstants = nullptr) const override;

	void ApplyParams(FShaderBindingInstance& Binding, const TMap<FString, FMaterialParamValue>& Params) const;

	void GatherAllParams(TMap<FString, FMaterialParamValue>& OutParams) const override
	{
		for (const auto& [Key, Param] : MaterialParams)
		{
			OutParams[Key] = Param;
		}
	}

	ID3D11SamplerState* ApplyRenderStates(ID3D11DeviceContext* Context) const;
	void EnsureShaderBinding(ID3D11Device* Device) const;
	void EnsureShaderBinding(ID3D11Device* Device, UShader* ShaderToBind) const;
};

class UMaterialInstance : public UMaterialInterface
{
public:
	DECLARE_CLASS(UMaterialInstance, UMaterialInterface)

	FString Name;
	FString FilePath;

	UMaterial* Parent = nullptr;
	mutable std::shared_ptr<FShaderBindingInstance> ShaderBinding;

	TMap<FString, FMaterialParamValue> OverridedParams;

	const FString& GetName() const override { return Name; }
	FString& GetNameRef() override { return Name; }
	const FString& GetFilePath() const override { return FilePath; }
	FString& GetFilePathRef() override { return FilePath; }
	EMaterialDomain GetEffectiveMaterialDomain() const override
	{
		return Parent ? Parent->GetEffectiveMaterialDomain() : EMaterialDomain::Surface;
	}
	ELightingModel GetEffectiveLightingModel() const override
	{
		if (bOverrideLightingModel)
		{
			return LightingModelOverride;
		}

		return Parent ? Parent->GetEffectiveLightingModel() : ELightingModel::Phong;
	}

	static UMaterialInstance* CreateTransient(UMaterial* Material);

	void SetOwnership(EMaterialInstanceOwnership InOwnership) { Ownership = InOwnership; }
	EMaterialInstanceOwnership GetOwnership() const { return Ownership; }
	bool IsResourceManaged() const { return Ownership == EMaterialInstanceOwnership::ResourceManaged; }
	bool IsComponentTransient() const { return Ownership == EMaterialInstanceOwnership::ComponentTransient; }
	bool HasLightingModelOverride() const { return bOverrideLightingModel; }
	ELightingModel GetLightingModelOverride() const { return LightingModelOverride; }
	void SetLightingModelOverride(ELightingModel InLightingModel)
	{
		bOverrideLightingModel = true;
		LightingModelOverride = InLightingModel;
	}
	void ClearLightingModelOverride()
	{
		bOverrideLightingModel = false;
	}

	void SetParam(const FString& Name, const FMaterialParamValue& Value)
	{
		OverridedParams[Name] = Value;
	}
	bool GetParam(const FString& Name, FMaterialParamValue& OutValue) const override
	{
		auto It = OverridedParams.find(Name);
		if (It != OverridedParams.end())
		{
			OutValue = It->second;
			return true;
		}
		return Parent ? Parent->GetParam(Name, OutValue) : false;
	}

	void Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus = nullptr, const FPerObjectConstants* PerObject = nullptr) const;
	void Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus, const FPerObjectConstants* PerObject, UShader* ShaderOverride, const FRenderPassContext* PassContext = nullptr, const FDecalConstants* DecalConstants = nullptr) const override;

	void GatherAllParams(TMap<FString, FMaterialParamValue>& OutParams) const override
	{
		if (Parent)
		{
			Parent->GatherAllParams(OutParams);
		}

		for (const auto& [Key, Param] : OverridedParams)
		{
			OutParams[Key] = Param;
		}
	}

private:
	EMaterialInstanceOwnership Ownership = EMaterialInstanceOwnership::ResourceManaged;
	bool bOverrideLightingModel = false;
	ELightingModel LightingModelOverride = ELightingModel::Phong;

	friend class FResourceManager;
};
