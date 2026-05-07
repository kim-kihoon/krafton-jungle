#pragma once

/*
	Shader들을 관리하는 Class입니다.
	추후에 Geometry Shader, Compute Shader 등 다양한 Shader들을 관리하는 Class로 확장할 수 있습니다.
*/

#include "Render/Common/ComPtr.h"
#include "Render/Common/RenderTypes.h"

#include "Core/CoreTypes.h"
#include "Object/Object.h"

class FRenderBus;
class UTexture;
class FShaderBindingInstance;
struct FPerObjectConstants;
struct FDecalConstants;

enum class EShaderStage : uint8
{
	Vertex = 0,
	Pixel,
	Geometry,
	Hull,
	Domain,
	Compute,
	Count
};

struct FShaderVariableDesc
{
	uint32 StartOffset = 0;
	uint32 Size = 0;
};

struct FConstantBufferDesc
{
	FString Name;
	uint32 BindPoint = 0;
	uint32 BufferSize = 0;

	TMap<FString, FShaderVariableDesc> Variables;
};

struct FTextureBindDesc
{
	FString Name;
	uint32 BindPoint = 0;
};

struct FSamplerBindDesc
{
	FString Name;
	uint32 BindPoint = 0;
};

struct FShaderMacro
{
	FString Name;
	FString Value;

	bool operator==(const FShaderMacro& Other) const = default;
};

struct FShaderCompileKey
{
	FString FilePath;
	FString VSEntryPoint;
	FString PSEntryPoint;
	TArray<FShaderMacro> Macros;

	bool operator==(const FShaderCompileKey& Other) const = default;
};

struct FReflectResult
{
	TMap<FString, FConstantBufferDesc> CBuffers;
	TMap<FString, FTextureBindDesc> Textures;
	TMap<FString, FSamplerBindDesc> Samplers;
};

//	Shader Set
struct FShader
{
	ID3D11VertexShader* VS = nullptr;
	ID3D11PixelShader* PS = nullptr;
	ID3D11InputLayout* InputLayout = nullptr;

	void Release()
	{
		if (VS)
		{
			VS->Release();
			VS = nullptr;
		}
		if (PS)
		{
			PS->Release();
			PS = nullptr;
		}
		if (InputLayout)
		{
			InputLayout->Release();
			InputLayout = nullptr;
		}
	}
};

class UShader : public UObject
{
public:
	DECLARE_CLASS(UShader, UObject)
	~UShader() override
	{
		ShaderData.Release();
	}

	void Bind(ID3D11DeviceContext* Context) const
	{
		Context->IASetInputLayout(ShaderData.InputLayout);
		Context->VSSetShader(ShaderData.VS, nullptr, 0);
		Context->PSSetShader(ShaderData.PS, nullptr, 0);
	}

	bool ReflectShader(ID3DBlob* ShaderBlob, ID3D11Device* Device, EShaderStage Stage);
	std::shared_ptr<FShaderBindingInstance> CreateBindingInstance(ID3D11Device* Device) const;
	const FReflectResult& GetReflectResult(EShaderStage Stage) const { return ReflectResults[static_cast<uint32>(Stage)]; }

	FShader ShaderData;
	FString FilePath;

private:
	bool CreateInputLayoutFromReflection(ID3DBlob* ShaderBlob, ID3D11Device* Device, ID3D11ShaderReflection* Reflector);

	FReflectResult ReflectResults[static_cast<uint32>(EShaderStage::Count)];
};

struct FConstantBufferRuntime
{
	TComPtr<ID3D11Buffer> Buffer;
	TArray<uint8> LocalData;
	uint32 BindPoint = 0;
	uint32 Size = 0;
	bool bDirty = true;
};

struct FTextureRuntime
{
	TComPtr<ID3D11ShaderResourceView> SRV;
	uint32 BindPoint = 0;
};

struct FSamplerRuntime
{
	TComPtr<ID3D11SamplerState> Sampler;
	uint32 BindPoint = 0;
};

class FShaderBindingInstance
{
public:
	bool Initialize(ID3D11Device* Device, const UShader* InShader);
	const UShader* GetShader() const { return ShaderAsset; }

	bool SetBytes(const FString& Name, const void* Data, uint32 Size);
	bool SetBool(const FString& Name, bool Value);
	bool SetInt(const FString& Name, int32 Value);
	bool SetUInt(const FString& Name, uint32 Value);
	bool SetFloat(const FString& Name, float Value);
	bool SetVector2(const FString& Name, const FVector2& Value);
	bool SetVector3(const FString& Name, const FVector& Value);
	bool SetVector4(const FString& Name, const FVector4& Value);
	bool SetMatrix4(const FString& Name, const FMatrix& Value);

	bool SetTexture(const FString& Name, UTexture* Texture);
	bool SetSRV(const FString& Name, ID3D11ShaderResourceView* SRV);
	void SetSampler(const FString& Name, ID3D11SamplerState* Sampler);
	void SetAllSamplers(ID3D11SamplerState* Sampler);

	void ApplyFrameParameters(const FRenderBus& RenderBus, ID3D11ShaderResourceView* SceneGlobalLightBufferSRV = nullptr, uint32 SceneGlobalLightCount = 0);
	void ApplyPerObjectParameters(const FPerObjectConstants& Constants);
	void ApplyUberPerObjectParameters(const FPerObjectConstants& Constants);
	void ApplyDecalParameters(const FDecalConstants& Constants);

	void Bind(ID3D11DeviceContext* Context);

private:
	bool SetValueInternal(const FString& Name, const void* Data, uint32 Size, bool bRequireExactSize);
	void UploadConstantBuffers(ID3D11DeviceContext* Context);
	void BindConstantBuffers(ID3D11DeviceContext* Context) const;
	void BindTextures(ID3D11DeviceContext* Context) const;
	void BindSamplers(ID3D11DeviceContext* Context) const;

	const UShader* ShaderAsset = nullptr;
	TMap<FString, FConstantBufferRuntime> ConstantBuffers[static_cast<uint32>(EShaderStage::Count)];
	TMap<FString, FTextureRuntime> Textures[static_cast<uint32>(EShaderStage::Count)];
	TMap<FString, FSamplerRuntime> Samplers[static_cast<uint32>(EShaderStage::Count)];
};

namespace std
{
	template<>
	struct hash<FShaderMacro>
	{
		size_t operator()(const FShaderMacro& Macro) const noexcept
		{
			size_t Hash = std::hash<FString>{}(Macro.Name);
			Hash ^= std::hash<FString>{}(Macro.Value) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
			return Hash;
		}
	};

	template<>
	struct hash<FShaderCompileKey>
	{
		size_t operator()(const FShaderCompileKey& Key) const noexcept
		{
			size_t Hash = std::hash<FString>{}(Key.FilePath);
			Hash ^= std::hash<FString>{}(Key.VSEntryPoint) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
			Hash ^= std::hash<FString>{}(Key.PSEntryPoint) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);

			for (const FShaderMacro& Macro : Key.Macros)
			{
				Hash ^= std::hash<FShaderMacro>{}(Macro) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
			}

			return Hash;
		}
	};
}
