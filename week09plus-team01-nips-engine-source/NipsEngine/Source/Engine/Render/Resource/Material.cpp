#include "Material.h"
#include "Core/ResourceManager.h"
#include "Render/Renderer/RenderFlow/RenderPassContext.h"

DEFINE_CLASS(UMaterialInterface, UObject)
DEFINE_CLASS(UMaterial, UMaterialInterface)
DEFINE_CLASS(UMaterialInstance, UMaterialInterface)

namespace
{
	bool IsUberSurfaceShader(const UShader* Shader)
	{
		return Shader &&
			(Shader->FilePath == "Shaders/UberLit.hlsl" || Shader->FilePath == "Shaders/UberUnlit.hlsl");
	}

	bool IsUberLitShader(const UShader* Shader)
	{
		return Shader && Shader->FilePath == "Shaders/UberLit.hlsl";
	}

	UShader* ResolveEffectiveMaterialShader(const UMaterialInterface& MaterialInterface, UShader* BaseShader, UShader* ShaderOverride)
	{
		if (ShaderOverride)
		{
			return ShaderOverride;
		}

		if (!IsUberLitShader(BaseShader))
		{
			return BaseShader;
		}

		UShader* VariantShader = FResourceManager::Get().GetShaderVariant(
			MakeUberLitShaderCompileKey(
				MaterialInterface.GetEffectiveMaterialDomain(),
				MaterialInterface.GetEffectiveLightingModel()));
		return VariantShader ? VariantShader : BaseShader;
	}

	void ApplyMaterialParam(FShaderBindingInstance& Binding, const FString& Name, const FMaterialParamValue& ParamValue)
	{
		switch (ParamValue.Type)
		{
		case EMaterialParamType::Bool:
			Binding.SetBool(Name, std::get<bool>(ParamValue.Value));
			break;
		case EMaterialParamType::Int:
			Binding.SetInt(Name, std::get<int32>(ParamValue.Value));
			break;
		case EMaterialParamType::UInt:
			Binding.SetUInt(Name, std::get<uint32>(ParamValue.Value));
			break;
		case EMaterialParamType::Float:
			Binding.SetFloat(Name, std::get<float>(ParamValue.Value));
			break;
		case EMaterialParamType::Vector2:
			Binding.SetVector2(Name, std::get<FVector2>(ParamValue.Value));
			break;
		case EMaterialParamType::Vector3:
			Binding.SetVector3(Name, std::get<FVector>(ParamValue.Value));
			break;
		case EMaterialParamType::Vector4:
			Binding.SetVector4(Name, std::get<FVector4>(ParamValue.Value));
			break;
		case EMaterialParamType::Matrix4:
			Binding.SetMatrix4(Name, std::get<FMatrix>(ParamValue.Value));
			break;
		case EMaterialParamType::Texture:
			Binding.SetTexture(Name, std::get<UTexture*>(ParamValue.Value));
			break;
		default:
			break;
		}
	}
}

const char* ToLightingModelString(ELightingModel LightingModel)
{
	switch (LightingModel)
	{
    case ELightingModel::Toon:
        return "Toon";
	case ELightingModel::Gouraud:
		return "Gouraud";
	case ELightingModel::Lambert:
		return "Lambert";
	case ELightingModel::Phong:
	default:
		return "Phong";
	}
}

const char* ToMaterialDomainString(EMaterialDomain MaterialDomain)
{
	switch (MaterialDomain)
	{
	case EMaterialDomain::Decal:
		return "Decal";
	case EMaterialDomain::Surface:
	default:
		return "Surface";
	}
}

bool TryParseMaterialDomain(const FString& Value, EMaterialDomain& OutMaterialDomain)
{
	if (Value == "Surface")
	{
		OutMaterialDomain = EMaterialDomain::Surface;
		return true;
	}

	if (Value == "Decal")
	{
		OutMaterialDomain = EMaterialDomain::Decal;
		return true;
	}

	return false;
}

bool TryParseLightingModel(const FString& Value, ELightingModel& OutLightingModel)
{
	if (Value == "Gouraud")
	{
		OutLightingModel = ELightingModel::Gouraud;
		return true;
	}

	if (Value == "Lambert")
	{
		OutLightingModel = ELightingModel::Lambert;
		return true;
	}

	if (Value == "Phong")
	{
		OutLightingModel = ELightingModel::Phong;
		return true;
	}

	if (Value == "Toon")
	{
        OutLightingModel = ELightingModel::Toon;
        return true;
	}

	return false;
}

FShaderCompileKey MakeUberLitShaderCompileKey(EMaterialDomain MaterialDomain, ELightingModel LightingModel)
{
	FShaderCompileKey Key;
	Key.FilePath = "Shaders/UberLit.hlsl";
	Key.VSEntryPoint = "mainVS";
	Key.PSEntryPoint = "mainPS";

	if (MaterialDomain == EMaterialDomain::Decal)
	{
		Key.Macros.push_back({ "MATERIAL_DOMAIN_DECAL", "1" });
		return Key;
	}

	switch (LightingModel)
	{
    case ELightingModel::Toon:
        Key.Macros.push_back({ "LIGHTING_MODEL_TOON", "1" });
        break;
	case ELightingModel::Gouraud:
		Key.Macros.push_back({ "LIGHTING_MODEL_GOURAUD", "1" });
		break;
	case ELightingModel::Lambert:
		Key.Macros.push_back({ "LIGHTING_MODEL_LAMBERT", "1" });
		break;
	case ELightingModel::Phong:
	default:
		Key.Macros.push_back({ "LIGHTING_MODEL_PHONG", "1" });
		break;
	}

	return Key;
}

UMaterialInstance* UMaterialInstance::CreateTransient(UMaterial* Material)
{
	UMaterialInstance* Instance = UObjectManager::Get().CreateObject<UMaterialInstance>();
	Instance->Parent = Material;
	Instance->Name = Instance->GetFName().ToString();
	Instance->FilePath.clear();
	Instance->SetOwnership(EMaterialInstanceOwnership::ComponentTransient);
	return Instance;
}

ID3D11SamplerState* UMaterial::ApplyRenderStates(ID3D11DeviceContext* Context) const
{
	if (!Context)
	{
		return nullptr;
	}

	ID3D11DepthStencilState* DSState = FResourceManager::Get().GetOrCreateDepthStencilState(DepthStencilType);
	ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(BlendType);
	ID3D11RasterizerState* RasterizerState = FResourceManager::Get().GetOrCreateRasterizerState(RasterizerType);
	ID3D11SamplerState* Sampler = FResourceManager::Get().GetOrCreateSamplerState(SamplerType);

	Context->OMSetDepthStencilState(DSState, 0);
	Context->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);
	Context->RSSetState(RasterizerState);

	return Sampler;
}

void UMaterial::EnsureShaderBinding(ID3D11Device* Device, UShader* ShaderToBind) const
{
	if (!ShaderToBind)
	{
		ShaderBinding.reset();
		return;
	}

	if (ShaderBinding && ShaderBinding->GetShader() == ShaderToBind)
	{
		return;
	}

	ShaderBinding = ShaderToBind->CreateBindingInstance(Device);
}

void UMaterial::EnsureShaderBinding(ID3D11Device* Device) const
{
	EnsureShaderBinding(Device, Shader);
}

void UMaterial::ApplyParams(FShaderBindingInstance& Binding, const TMap<FString, FMaterialParamValue>& Params) const
{
	for (const auto& [Name, ParamValue] : Params)
	{
		ApplyMaterialParam(Binding, Name, ParamValue);
	}
}

void UMaterial::Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus, const FPerObjectConstants* PerObject) const
{
	Bind(Context, RenderBus, PerObject, nullptr, nullptr, nullptr);
}

void UMaterial::Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus, const FPerObjectConstants* PerObject, UShader* ShaderOverride, const FRenderPassContext* PassContext, const FDecalConstants* DecalConstants) const
{
	UShader* EffectiveShader = ResolveEffectiveMaterialShader(*this, Shader, ShaderOverride);
	if (!Context || !EffectiveShader)
	{
		return;
	}

	EnsureShaderBinding(FResourceManager::Get().GetCachedDevice(), EffectiveShader);
	if (!ShaderBinding)
	{
		return;
	}

	ID3D11SamplerState* Sampler = ApplyRenderStates(Context);
	ShaderBinding->SetAllSamplers(Sampler);

	if (RenderBus)
	{
		ShaderBinding->ApplyFrameParameters(
			*RenderBus,
			PassContext ? PassContext->SceneGlobalLightBufferSRV : nullptr,
			PassContext ? PassContext->SceneGlobalLightCount : 0u);
	}

	if (PerObject)
	{
		if (IsUberSurfaceShader(EffectiveShader))
		{
			ShaderBinding->ApplyUberPerObjectParameters(*PerObject);
		}
		else
		{
			ShaderBinding->ApplyPerObjectParameters(*PerObject);
		}
	}

	ApplyParams(*ShaderBinding, MaterialParams);
	if (DecalConstants)
	{
		ShaderBinding->ApplyDecalParameters(*DecalConstants);
	}
	ShaderBinding->Bind(Context);
}

void UMaterialInstance::Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus, const FPerObjectConstants* PerObject) const
{
	Bind(Context, RenderBus, PerObject, nullptr, nullptr, nullptr);
}

void UMaterialInstance::Bind(ID3D11DeviceContext* Context, const FRenderBus* RenderBus, const FPerObjectConstants* PerObject, UShader* ShaderOverride, const FRenderPassContext* PassContext, const FDecalConstants* DecalConstants) const
{
	UShader* EffectiveShader = ResolveEffectiveMaterialShader(*this, Parent ? Parent->Shader : nullptr, ShaderOverride);
	if (!Context || !Parent || !EffectiveShader)
	{
		return;
	}

	TMap<FString, FMaterialParamValue> CombinedParams;
	Parent->GatherAllParams(CombinedParams);
	for (const auto& [Name, Value] : OverridedParams)
	{
		CombinedParams[Name] = Value;
	}

	if (!ShaderBinding || ShaderBinding->GetShader() != EffectiveShader)
	{
		ShaderBinding = EffectiveShader->CreateBindingInstance(FResourceManager::Get().GetCachedDevice());
	}

	if (!ShaderBinding)
	{
		return;
	}

	ID3D11SamplerState* Sampler = Parent->ApplyRenderStates(Context);
	ShaderBinding->SetAllSamplers(Sampler);

	if (RenderBus)
	{
		ShaderBinding->ApplyFrameParameters(
			*RenderBus,
			PassContext ? PassContext->SceneGlobalLightBufferSRV : nullptr,
			PassContext ? PassContext->SceneGlobalLightCount : 0u);
	}

	if (PerObject)
	{
		if (IsUberSurfaceShader(EffectiveShader))
		{
			ShaderBinding->ApplyUberPerObjectParameters(*PerObject);
		}
		else
		{
			ShaderBinding->ApplyPerObjectParameters(*PerObject);
		}
	}

	Parent->ApplyParams(*ShaderBinding, CombinedParams);
	if (DecalConstants)
	{
		ShaderBinding->ApplyDecalParameters(*DecalConstants);
	}
	ShaderBinding->Bind(Context);
}
