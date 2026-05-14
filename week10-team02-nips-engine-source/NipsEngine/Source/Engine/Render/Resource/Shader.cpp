#include "Shader.h"

#include "Texture.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Scene/RenderCommand.h"
#include "Core/Logger.h"

#include <vector>

DEFINE_CLASS(UShader, UObject)

namespace
{
	uint32 AlignConstantBufferSize(uint32 Size)
	{
		return (Size + 0x0F) & ~0x0Fu;
	}

	DXGI_FORMAT GetFormatFromSignature(const D3D11_SIGNATURE_PARAMETER_DESC& ParamDesc)
	{
		switch (ParamDesc.Mask)
		{
		case 1:
			if (ParamDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) return DXGI_FORMAT_R32_UINT;
			if (ParamDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) return DXGI_FORMAT_R32_SINT;
			return DXGI_FORMAT_R32_FLOAT;
		case 3:
			if (ParamDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) return DXGI_FORMAT_R32G32_UINT;
			if (ParamDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) return DXGI_FORMAT_R32G32_SINT;
			return DXGI_FORMAT_R32G32_FLOAT;
		case 7:
			if (ParamDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) return DXGI_FORMAT_R32G32B32_UINT;
			if (ParamDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) return DXGI_FORMAT_R32G32B32_SINT;
			return DXGI_FORMAT_R32G32B32_FLOAT;
		case 15:
			if (ParamDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32) return DXGI_FORMAT_R32G32B32A32_UINT;
			if (ParamDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32) return DXGI_FORMAT_R32G32B32A32_SINT;
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	void BindStageConstantBuffer(ID3D11DeviceContext* Context, EShaderStage Stage, uint32 BindPoint, ID3D11Buffer* Buffer)
	{
		switch (Stage)
		{
		case EShaderStage::Vertex:
			Context->VSSetConstantBuffers(BindPoint, 1, &Buffer);
			break;
		case EShaderStage::Pixel:
			Context->PSSetConstantBuffers(BindPoint, 1, &Buffer);
			break;
		default:
			break;
		}
	}

	void BindStageShaderResource(ID3D11DeviceContext* Context, EShaderStage Stage, uint32 BindPoint, ID3D11ShaderResourceView* SRV)
	{
		switch (Stage)
		{
		case EShaderStage::Vertex:
			Context->VSSetShaderResources(BindPoint, 1, &SRV);
			break;
		case EShaderStage::Pixel:
			Context->PSSetShaderResources(BindPoint, 1, &SRV);
			break;
		default:
			break;
		}
	}

	void BindStageSampler(ID3D11DeviceContext* Context, EShaderStage Stage, uint32 BindPoint, ID3D11SamplerState* Sampler)
	{
		switch (Stage)
		{
		case EShaderStage::Vertex:
			Context->VSSetSamplers(BindPoint, 1, &Sampler);
			break;
		case EShaderStage::Pixel:
			Context->PSSetSamplers(BindPoint, 1, &Sampler);
			break;
		default:
			break;
		}
	}
}

bool UShader::CreateInputLayoutFromReflection(ID3DBlob* ShaderBlob, ID3D11Device* Device, ID3D11ShaderReflection* Reflector)
{
	if (!ShaderBlob || !Device || !Reflector)
	{
		return false;
	}

	D3D11_SHADER_DESC ShaderDesc = {};
	if (FAILED(Reflector->GetDesc(&ShaderDesc)))
	{
		return false;
	}

	std::vector<std::string> SemanticNames;
	std::vector<D3D11_INPUT_ELEMENT_DESC> InputLayoutDesc;
	SemanticNames.reserve(ShaderDesc.InputParameters);
	InputLayoutDesc.reserve(ShaderDesc.InputParameters);

	for (UINT InputIndex = 0; InputIndex < ShaderDesc.InputParameters; ++InputIndex)
	{
		D3D11_SIGNATURE_PARAMETER_DESC ParamDesc = {};
		if (FAILED(Reflector->GetInputParameterDesc(InputIndex, &ParamDesc)))
		{
			continue;
		}

		if (ParamDesc.SystemValueType != D3D_NAME_UNDEFINED)
		{
			continue;
		}

		DXGI_FORMAT Format = GetFormatFromSignature(ParamDesc);
		if (Format == DXGI_FORMAT_UNKNOWN)
		{
			continue;
		}

		SemanticNames.emplace_back(ParamDesc.SemanticName);

		D3D11_INPUT_ELEMENT_DESC ElementDesc = {};
		ElementDesc.SemanticName = SemanticNames.back().c_str();
		ElementDesc.SemanticIndex = ParamDesc.SemanticIndex;
		ElementDesc.Format = Format;
		ElementDesc.InputSlot = 0;
		ElementDesc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		ElementDesc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		ElementDesc.InstanceDataStepRate = 0;
		InputLayoutDesc.push_back(ElementDesc);
	}

	if (InputLayoutDesc.empty())
	{
		if (ShaderData.InputLayout)
		{
			ShaderData.InputLayout->Release();
			ShaderData.InputLayout = nullptr;
		}
		return true;
	}

	if (ShaderData.InputLayout)
	{
		ShaderData.InputLayout->Release();
		ShaderData.InputLayout = nullptr;
	}

	const HRESULT Hr = Device->CreateInputLayout(
		InputLayoutDesc.data(),
		static_cast<UINT>(InputLayoutDesc.size()),
		ShaderBlob->GetBufferPointer(),
		ShaderBlob->GetBufferSize(),
		&ShaderData.InputLayout);

	return SUCCEEDED(Hr);
}

bool UShader::ReflectShader(ID3DBlob* ShaderBlob, ID3D11Device* Device, EShaderStage Stage)
{
	if (!ShaderBlob || !Device)
	{
		return false;
	}

	ID3D11ShaderReflection* Reflector = nullptr;
	const HRESULT Hr = D3DReflect(
		ShaderBlob->GetBufferPointer(),
		ShaderBlob->GetBufferSize(),
		IID_ID3D11ShaderReflection,
		reinterpret_cast<void**>(&Reflector));

	if (FAILED(Hr) || !Reflector)
	{
		return false;
	}

	FReflectResult& ReflectResult = ReflectResults[static_cast<uint32>(Stage)];
	ReflectResult.CBuffers.clear();
	ReflectResult.Textures.clear();
	ReflectResult.Samplers.clear();

	D3D11_SHADER_DESC ShaderDesc = {};
	Reflector->GetDesc(&ShaderDesc);

	for (UINT ResourceIndex = 0; ResourceIndex < ShaderDesc.BoundResources; ++ResourceIndex)
	{
		D3D11_SHADER_INPUT_BIND_DESC BindDesc = {};
		Reflector->GetResourceBindingDesc(ResourceIndex, &BindDesc);

		if (BindDesc.Type == D3D_SIT_CBUFFER)
		{
			FConstantBufferDesc BufferDesc = {};
			BufferDesc.Name = BindDesc.Name;
			BufferDesc.BindPoint = BindDesc.BindPoint;

			ID3D11ShaderReflectionConstantBuffer* ConstantBuffer = Reflector->GetConstantBufferByName(BindDesc.Name);
			if (ConstantBuffer)
			{
				D3D11_SHADER_BUFFER_DESC ShaderBufferDesc = {};
				ConstantBuffer->GetDesc(&ShaderBufferDesc);
				BufferDesc.BufferSize = ShaderBufferDesc.Size;

				for (UINT VarIndex = 0; VarIndex < ShaderBufferDesc.Variables; ++VarIndex)
				{
					ID3D11ShaderReflectionVariable* Variable = ConstantBuffer->GetVariableByIndex(VarIndex);
					D3D11_SHADER_VARIABLE_DESC VarDesc = {};
					Variable->GetDesc(&VarDesc);

					FShaderVariableDesc VariableDesc = {};
					VariableDesc.StartOffset = VarDesc.StartOffset;
					VariableDesc.Size = VarDesc.Size;
					BufferDesc.Variables[VarDesc.Name] = VariableDesc;
				}
			}

			ReflectResult.CBuffers[BufferDesc.Name] = BufferDesc;
		}
		else if (BindDesc.Type == D3D_SIT_TEXTURE || BindDesc.Type == D3D_SIT_STRUCTURED || BindDesc.Type == D3D_SIT_BYTEADDRESS)
		{
			FTextureBindDesc TextureDesc = {};
			TextureDesc.Name = BindDesc.Name;
			TextureDesc.BindPoint = BindDesc.BindPoint;
			ReflectResult.Textures[TextureDesc.Name] = TextureDesc;
		}
		else if (BindDesc.Type == D3D_SIT_SAMPLER)
		{
			FSamplerBindDesc SamplerDesc = {};
			SamplerDesc.Name = BindDesc.Name;
			SamplerDesc.BindPoint = BindDesc.BindPoint;
			ReflectResult.Samplers[SamplerDesc.Name] = SamplerDesc;
		}
	}

	bool bSuccess = true;
	if (Stage == EShaderStage::Vertex)
	{
		bSuccess = CreateInputLayoutFromReflection(ShaderBlob, Device, Reflector);
	}

	Reflector->Release();
	return bSuccess;
}

std::shared_ptr<FShaderBindingInstance> UShader::CreateBindingInstance(ID3D11Device* Device) const
{
	if (!Device)
	{
		return nullptr;
	}

	std::shared_ptr<FShaderBindingInstance> Binding = std::make_shared<FShaderBindingInstance>();
	if (!Binding->Initialize(Device, this))
	{
		return nullptr;
	}

	return Binding;
}

bool FShaderBindingInstance::Initialize(ID3D11Device* Device, const UShader* InShader)
{
	if (!Device || !InShader)
	{
		return false;
	}

	ShaderAsset = InShader;

	for (uint32 StageIndex = 0; StageIndex < static_cast<uint32>(EShaderStage::Count); ++StageIndex)
	{
		ConstantBuffers[StageIndex].clear();
		Textures[StageIndex].clear();
		Samplers[StageIndex].clear();

		const FReflectResult& ReflectResult = ShaderAsset->GetReflectResult(static_cast<EShaderStage>(StageIndex));

		for (const auto& [BufferName, BufferDesc] : ReflectResult.CBuffers)
		{
			FConstantBufferRuntime Runtime = {};
			Runtime.BindPoint = BufferDesc.BindPoint;
			Runtime.Size = BufferDesc.BufferSize;
			Runtime.LocalData.resize(BufferDesc.BufferSize, 0);
			Runtime.bDirty = true;

			if (BufferDesc.BufferSize > 0)
			{
				D3D11_BUFFER_DESC Desc = {};
				Desc.ByteWidth = AlignConstantBufferSize(BufferDesc.BufferSize);
				Desc.Usage = D3D11_USAGE_DYNAMIC;
				Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				if (FAILED(Device->CreateBuffer(&Desc, nullptr, Runtime.Buffer.ReleaseAndGetAddressOf())))
				{
					return false;
				}
			}

			ConstantBuffers[StageIndex][BufferName] = Runtime;
		}

		for (const auto& [TextureName, TextureDesc] : ReflectResult.Textures)
		{
			FTextureRuntime Runtime = {};
			Runtime.BindPoint = TextureDesc.BindPoint;
			Textures[StageIndex][TextureName] = Runtime;
		}

		for (const auto& [SamplerName, SamplerDesc] : ReflectResult.Samplers)
		{
			FSamplerRuntime Runtime = {};
			Runtime.BindPoint = SamplerDesc.BindPoint;
			Samplers[StageIndex][SamplerName] = Runtime;
		}
	}

	return true;
}

bool FShaderBindingInstance::SetValueInternal(const FString& Name, const void* Data, uint32 Size, bool bRequireExactSize)
{
	if (!ShaderAsset || !Data || Size == 0)
	{
		return false;
	}

	bool bFound = false;

	for (uint32 StageIndex = 0; StageIndex < static_cast<uint32>(EShaderStage::Count); ++StageIndex)
	{
		const FReflectResult& ReflectResult = ShaderAsset->GetReflectResult(static_cast<EShaderStage>(StageIndex));
		for (const auto& [BufferName, BufferDesc] : ReflectResult.CBuffers)
		{
			auto VarIt = BufferDesc.Variables.find(Name);
			if (VarIt == BufferDesc.Variables.end())
			{
				continue;
			}

			bFound = true;

			const FShaderVariableDesc& VariableDesc = VarIt->second;
			if ((bRequireExactSize && VariableDesc.Size != Size) || (!bRequireExactSize && VariableDesc.Size < Size))
			{
				UE_LOG("[ShaderBinding] Type/size mismatch for variable %s (expected %u, got %u)",
					Name.c_str(), VariableDesc.Size, Size);
				return false;
			}

			auto RuntimeIt = ConstantBuffers[StageIndex].find(BufferName);
			if (RuntimeIt == ConstantBuffers[StageIndex].end())
			{
				continue;
			}

			FConstantBufferRuntime& Runtime = RuntimeIt->second;
			if (Runtime.LocalData.size() < BufferDesc.BufferSize)
			{
				Runtime.LocalData.resize(BufferDesc.BufferSize, 0);
			}

			std::memcpy(Runtime.LocalData.data() + VariableDesc.StartOffset, Data, Size);
			Runtime.bDirty = true;
		}
	}

	return bFound;
}

bool FShaderBindingInstance::SetBytes(const FString& Name, const void* Data, uint32 Size)
{
	return SetValueInternal(Name, Data, Size, false);
}

bool FShaderBindingInstance::SetBool(const FString& Name, bool Value)
{
	const uint32 BoolValue = Value ? 1u : 0u;
	return SetValueInternal(Name, &BoolValue, sizeof(BoolValue), true);
}

bool FShaderBindingInstance::SetInt(const FString& Name, int32 Value)
{
	return SetValueInternal(Name, &Value, sizeof(Value), true);
}

bool FShaderBindingInstance::SetUInt(const FString& Name, uint32 Value)
{
	return SetValueInternal(Name, &Value, sizeof(Value), true);
}

bool FShaderBindingInstance::SetFloat(const FString& Name, float Value)
{
	return SetValueInternal(Name, &Value, sizeof(Value), true);
}

bool FShaderBindingInstance::SetVector2(const FString& Name, const FVector2& Value)
{
	return SetValueInternal(Name, &Value, sizeof(Value), true);
}

bool FShaderBindingInstance::SetVector3(const FString& Name, const FVector& Value)
{
	return SetValueInternal(Name, &Value, sizeof(Value), true);
}

bool FShaderBindingInstance::SetVector4(const FString& Name, const FVector4& Value)
{
	return SetValueInternal(Name, &Value, sizeof(Value), true);
}

bool FShaderBindingInstance::SetMatrix4(const FString& Name, const FMatrix& Value)
{
	return SetValueInternal(Name, &Value, sizeof(Value), true);
}

bool FShaderBindingInstance::SetTexture(const FString& Name, UTexture* Texture)
{
	return SetSRV(Name, Texture ? Texture->GetSRV() : nullptr);
}

bool FShaderBindingInstance::SetSRV(const FString& Name, ID3D11ShaderResourceView* SRV)
{
	if (!ShaderAsset)
	{
		return false;
	}

	bool bFound = false;
	for (uint32 StageIndex = 0; StageIndex < static_cast<uint32>(EShaderStage::Count); ++StageIndex)
	{
		const FReflectResult& ReflectResult = ShaderAsset->GetReflectResult(static_cast<EShaderStage>(StageIndex));
		if (ReflectResult.Textures.find(Name) == ReflectResult.Textures.end())
		{
			continue;
		}

		auto RuntimeIt = Textures[StageIndex].find(Name);
		if (RuntimeIt == Textures[StageIndex].end())
		{
			continue;
		}

		RuntimeIt->second.SRV = SRV;
		bFound = true;
	}

	return bFound;
}

void FShaderBindingInstance::SetSampler(const FString& Name, ID3D11SamplerState* Sampler)
{
	if (!ShaderAsset)
	{
		return;
	}

	for (uint32 StageIndex = 0; StageIndex < static_cast<uint32>(EShaderStage::Count); ++StageIndex)
	{
		const FReflectResult& ReflectResult = ShaderAsset->GetReflectResult(static_cast<EShaderStage>(StageIndex));
		if (ReflectResult.Samplers.find(Name) == ReflectResult.Samplers.end())
		{
			continue;
		}

		auto RuntimeIt = Samplers[StageIndex].find(Name);
		if (RuntimeIt == Samplers[StageIndex].end())
		{
			continue;
		}

		RuntimeIt->second.Sampler = Sampler;
	}
}

void FShaderBindingInstance::SetAllSamplers(ID3D11SamplerState* Sampler)
{
	for (uint32 StageIndex = 0; StageIndex < static_cast<uint32>(EShaderStage::Count); ++StageIndex)
	{
		for (auto& [Name, Runtime] : Samplers[StageIndex])
		{
			Runtime.Sampler = Sampler;
		}
	}
}

void FShaderBindingInstance::ApplyFrameParameters(const FRenderBus& RenderBus, ID3D11ShaderResourceView* SceneGlobalLightBufferSRV, uint32 SceneGlobalLightCount)
{
	const FMatrix ViewProjection = RenderBus.GetView() * RenderBus.GetProj();

	SetMatrix4("View", RenderBus.GetView());
	SetMatrix4("Projection", RenderBus.GetProj());
	SetMatrix4("InverseViewProjection", ViewProjection.GetInverse());
	SetVector3("CameraPosition", RenderBus.GetCameraPosition());
	SetFloat("bIsWireframe", RenderBus.GetViewMode() == EViewMode::Wireframe ? 1.0f : 0.0f);
	SetFloat("bLightingEnabled",
		RenderBus.GetViewMode() == EViewMode::Lit || RenderBus.GetViewMode() == EViewMode::CascadeShadow ? 1.0f : 0.0f);
	SetFloat("UberDebugViewMode", static_cast<float>(RenderBus.GetViewMode()));
	SetVector3("WireframeRGB", RenderBus.GetWireframeColor());
	SetVector2("ViewportSize", RenderBus.GetViewportSize());
	SetUInt("SceneGlobalLightCount", SceneGlobalLightCount);
	SetSRV("GlobalLights", SceneGlobalLightBufferSRV);
}

void FShaderBindingInstance::ApplyPerObjectParameters(const FPerObjectConstants& Constants)
{
	SetMatrix4("Model", Constants.Model);
	SetMatrix4("WorldInvTrans", Constants.WorldInvTrans);
	SetVector4("PrimitiveColor", Constants.Color);

	// 추후 UberLit 등 다른 naming convention과의 호환을 위한 alias
	SetMatrix4("World", Constants.Model);
	SetMatrix4("WorldInvTans", Constants.WorldInvTrans);
}

void FShaderBindingInstance::ApplyUberPerObjectParameters(const FPerObjectConstants& Constants)
{
	SetMatrix4("World", Constants.Model);
	SetMatrix4("WorldInverseTranspose", Constants.WorldInvTrans);
	SetVector4("PrimitiveColor", Constants.Color);
}

void FShaderBindingInstance::ApplyDecalParameters(const FDecalConstants& Constants)
{
	SetMatrix4("InvDecalWorld", Constants.InvDecalWorld);
	SetFloat("DecalMaskInfluence", Constants.MaskInfluence);
	SetFloat("DecalWhiteOverride", Constants.WhiteOverride);
}

void FShaderBindingInstance::UploadConstantBuffers(ID3D11DeviceContext* Context)
{
	for (uint32 StageIndex = 0; StageIndex < static_cast<uint32>(EShaderStage::Count); ++StageIndex)
	{
		for (auto& [Name, Runtime] : ConstantBuffers[StageIndex])
		{
			if (!Runtime.Buffer || !Runtime.bDirty)
			{
				continue;
			}

			D3D11_MAPPED_SUBRESOURCE MappedResource = {};
			if (FAILED(Context->Map(Runtime.Buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource)))
			{
				continue;
			}

			std::memcpy(MappedResource.pData, Runtime.LocalData.data(), Runtime.Size);
			Context->Unmap(Runtime.Buffer.Get(), 0);
			Runtime.bDirty = false;
		}
	}
}

void FShaderBindingInstance::BindConstantBuffers(ID3D11DeviceContext* Context) const
{
	for (uint32 StageIndex = 0; StageIndex < static_cast<uint32>(EShaderStage::Count); ++StageIndex)
	{
		const EShaderStage Stage = static_cast<EShaderStage>(StageIndex);
		for (const auto& [Name, Runtime] : ConstantBuffers[StageIndex])
		{
			ID3D11Buffer* Buffer = Runtime.Buffer.Get();
			if (!Buffer)
			{
				continue;
			}

			BindStageConstantBuffer(Context, Stage, Runtime.BindPoint, Buffer);
		}
	}
}

void FShaderBindingInstance::BindTextures(ID3D11DeviceContext* Context) const
{
	for (uint32 StageIndex = 0; StageIndex < static_cast<uint32>(EShaderStage::Count); ++StageIndex)
	{
		const EShaderStage Stage = static_cast<EShaderStage>(StageIndex);
		for (const auto& [Name, Runtime] : Textures[StageIndex])
		{
			ID3D11ShaderResourceView* SRV = Runtime.SRV.Get();
			BindStageShaderResource(Context, Stage, Runtime.BindPoint, SRV);
		}
	}
}

void FShaderBindingInstance::BindSamplers(ID3D11DeviceContext* Context) const
{
	for (uint32 StageIndex = 0; StageIndex < static_cast<uint32>(EShaderStage::Count); ++StageIndex)
	{
		const EShaderStage Stage = static_cast<EShaderStage>(StageIndex);
		for (const auto& [Name, Runtime] : Samplers[StageIndex])
		{
			ID3D11SamplerState* Sampler = Runtime.Sampler.Get();
			if (!Sampler)
			{
				continue;
			}

			BindStageSampler(Context, Stage, Runtime.BindPoint, Sampler);
		}
	}
}

void FShaderBindingInstance::Bind(ID3D11DeviceContext* Context)
{
	if (!Context || !ShaderAsset)
	{
		return;
	}

	UploadConstantBuffers(Context);
	ShaderAsset->Bind(Context);
	BindConstantBuffers(Context);
	BindTextures(Context);
	BindSamplers(Context);
}
