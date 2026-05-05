#include "Game/UI/RmlUi/RmlUiRenderInterfaceD3D11.h"

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs")

struct FRmlUiRenderInterfaceD3D11::FGeometry
{
	TComPtr<ID3D11Buffer> VertexBuffer;
	TComPtr<ID3D11Buffer> IndexBuffer;
	UINT IndexCount = 0;
};

struct FRmlUiRenderInterfaceD3D11::FTexture
{
	TComPtr<ID3D11Texture2D> Texture;
	TComPtr<ID3D11ShaderResourceView> SRV;
	int Width = 0;
	int Height = 0;
};

namespace
{
	struct FRmlUiConstants
	{
		float ViewportSize[2];
		float Translation[2];
		float FlashFactor;
		float IsTextured;
		float Padding[2];
	};

	const char* RmlUiShaderSource = R"(
cbuffer RmlUiConstants : register(b0)
{
	float2 ViewportSize;
	float2 Translation;
	float FlashFactor;
	float IsTextured;
};

struct VSInput
{
	float2 Position : POSITION;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR0;
	float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(VSInput Input)
{
	VSOutput Output;
	float2 Position = Input.Position + Translation;
	Output.Position = float4(Position.x * 2.0f / ViewportSize.x - 1.0f, 1.0f - Position.y * 2.0f / ViewportSize.y, 0.0f, 1.0f);
	Output.Color = Input.Color;
	Output.TexCoord = Input.TexCoord;
	return Output;
}

Texture2D DiffuseTexture : register(t0);
SamplerState LinearSampler : register(s0);

float4 PSMain(VSOutput Input) : SV_TARGET
{
	float4 Color = DiffuseTexture.Sample(LinearSampler, Input.TexCoord) * Input.Color;
	if (IsTextured > 0.5f)
	{
		Color.rgb += float3(1.0f, 1.0f, 1.0f) * FlashFactor * Color.a;
	}
	return Color;
}
)";

	void LogShaderError(ID3DBlob* ErrorBlob)
	{
		if (ErrorBlob)
		{
			OutputDebugStringA(static_cast<const char*>(ErrorBlob->GetBufferPointer()));
			OutputDebugStringA("\n");
		}
	}

	std::wstring ToWideString(const std::string& Text)
	{
		if (Text.empty())
			return {};

		const int WideLength = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, nullptr, 0);
		if (WideLength <= 0)
			return {};

		std::wstring WideText(static_cast<size_t>(WideLength - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, WideText.data(), WideLength);
		return WideText;
	}

	std::vector<std::string> GetTexturePathCandidates(const std::string& Source)
	{
		std::vector<std::string> Paths;
		Paths.push_back(Source);

		if (Source.rfind("Asset/", 0) == 0 || Source.rfind("Asset\\", 0) == 0)
			Paths.push_back("NipsEngine/" + Source);

		return Paths;
	}

	class FScopedComInit
	{
	public:
		FScopedComInit()
			: Result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
		{
		}

		~FScopedComInit()
		{
			if (Result == S_OK || Result == S_FALSE)
				CoUninitialize();
		}

		bool IsReady() const
		{
			return SUCCEEDED(Result) || Result == RPC_E_CHANGED_MODE;
		}

	private:
		HRESULT Result;
	};
}

FRmlUiRenderInterfaceD3D11::~FRmlUiRenderInterfaceD3D11()
{
	Shutdown();
}

bool FRmlUiRenderInterfaceD3D11::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
{
	Device = InDevice;
	Context = InContext;

	if (!Device || !Context)
		return false;

	return CreateShaders() && CreateStates();
}

void FRmlUiRenderInterfaceD3D11::Shutdown()
{
	SamplerState.Reset();
	WhiteTextureSRV.Reset();
	WhiteTexture.Reset();
	DepthStencilState.Reset();
	ScissorRasterizerState.Reset();
	RasterizerState.Reset();
	BlendState.Reset();
	ConstantBuffer.Reset();
	InputLayout.Reset();
	PixelShader.Reset();
	VertexShader.Reset();
	Context = nullptr;
	Device = nullptr;
}

void FRmlUiRenderInterfaceD3D11::BeginFrame(int InWidth, int InHeight)
{
	Width = InWidth > 0 ? InWidth : 1;
	Height = InHeight > 0 ? InHeight : 1;
	bScissorEnabled = false;
}

Rml::CompiledGeometryHandle FRmlUiRenderInterfaceD3D11::CompileGeometry(Rml::Span<const Rml::Vertex> Vertices, Rml::Span<const int> Indices)
{
	if (!Device || Vertices.empty() || Indices.empty())
		return {};

	FGeometry* Geometry = new FGeometry();
	Geometry->IndexCount = static_cast<UINT>(Indices.size());

	D3D11_BUFFER_DESC VertexDesc = {};
	VertexDesc.ByteWidth = static_cast<UINT>(Vertices.size() * sizeof(Rml::Vertex));
	VertexDesc.Usage = D3D11_USAGE_DEFAULT;
	VertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA VertexData = {};
	VertexData.pSysMem = Vertices.data();
	if (FAILED(Device->CreateBuffer(&VertexDesc, &VertexData, Geometry->VertexBuffer.GetAddressOf())))
	{
		delete Geometry;
		return {};
	}

	std::vector<unsigned int> ConvertedIndices;
	ConvertedIndices.reserve(Indices.size());
	for (int Index : Indices)
		ConvertedIndices.push_back(static_cast<unsigned int>(Index));

	D3D11_BUFFER_DESC IndexDesc = {};
	IndexDesc.ByteWidth = static_cast<UINT>(ConvertedIndices.size() * sizeof(unsigned int));
	IndexDesc.Usage = D3D11_USAGE_DEFAULT;
	IndexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA IndexData = {};
	IndexData.pSysMem = ConvertedIndices.data();
	if (FAILED(Device->CreateBuffer(&IndexDesc, &IndexData, Geometry->IndexBuffer.GetAddressOf())))
	{
		delete Geometry;
		return {};
	}

	return reinterpret_cast<Rml::CompiledGeometryHandle>(Geometry);
}

void FRmlUiRenderInterfaceD3D11::RenderGeometry(Rml::CompiledGeometryHandle GeometryHandle, Rml::Vector2f Translation, Rml::TextureHandle TextureHandle)
{
	FGeometry* Geometry = reinterpret_cast<FGeometry*>(GeometryHandle);
	if (!Geometry || !Context)
		return;

	SetPipelineState();

	FRmlUiConstants Constants = {};
	Constants.ViewportSize[0] = static_cast<float>(Width);
	Constants.ViewportSize[1] = static_cast<float>(Height);
	Constants.Translation[0] = Translation.x;
	Constants.Translation[1] = Translation.y;
	Constants.FlashFactor = FlashFactor;
	Constants.IsTextured = TextureHandle != 0 ? 1.0f : 0.0f;
	Context->UpdateSubresource(ConstantBuffer.Get(), 0, nullptr, &Constants, 0, 0);

	FTexture* Texture = reinterpret_cast<FTexture*>(TextureHandle);
	ID3D11ShaderResourceView* SRV = Texture ? Texture->SRV.Get() : WhiteTextureSRV.Get();
	Context->PSSetShaderResources(0, 1, &SRV);

	UINT Stride = sizeof(Rml::Vertex);
	UINT Offset = 0;
	ID3D11Buffer* VertexBuffer = Geometry->VertexBuffer.Get();
	Context->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);
	Context->IASetIndexBuffer(Geometry->IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	Context->DrawIndexed(Geometry->IndexCount, 0, 0);
}

void FRmlUiRenderInterfaceD3D11::ReleaseGeometry(Rml::CompiledGeometryHandle Geometry)
{
	delete reinterpret_cast<FGeometry*>(Geometry);
}

Rml::TextureHandle FRmlUiRenderInterfaceD3D11::LoadTexture(Rml::Vector2i& TextureDimensions, const Rml::String& Source)
{
	if (!Device || Source.empty())
		return {};

	FScopedComInit ComInit;
	if (!ComInit.IsReady())
		return {};

	TComPtr<IWICImagingFactory> WicFactory;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(WicFactory.GetAddressOf()))))
		return {};

	TComPtr<IWICBitmapFrameDecode> Frame;
	for (const std::string& Path : GetTexturePathCandidates(Source))
	{
		TComPtr<IWICBitmapDecoder> Decoder;
		const std::wstring WidePath = ToWideString(Path);
		if (WidePath.empty())
			continue;

		if (SUCCEEDED(WicFactory->CreateDecoderFromFilename(WidePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, Decoder.GetAddressOf())) &&
			SUCCEEDED(Decoder->GetFrame(0, Frame.GetAddressOf())))
		{
			break;
		}
	}

	if (!Frame)
		return {};

	UINT Width = 0;
	UINT Height = 0;
	if (FAILED(Frame->GetSize(&Width, &Height)) || Width == 0 || Height == 0)
		return {};

	TComPtr<IWICFormatConverter> Converter;
	if (FAILED(WicFactory->CreateFormatConverter(Converter.GetAddressOf())) ||
		FAILED(Converter->Initialize(Frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
	{
		return {};
	}

	std::vector<Rml::byte> Pixels(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4);
	if (FAILED(Converter->CopyPixels(nullptr, Width * 4, static_cast<UINT>(Pixels.size()), Pixels.data())))
		return {};

	TextureDimensions = Rml::Vector2i(static_cast<int>(Width), static_cast<int>(Height));
	return GenerateTexture(Rml::Span<const Rml::byte>(Pixels.data(), Pixels.size()), TextureDimensions);
}

Rml::TextureHandle FRmlUiRenderInterfaceD3D11::GenerateTexture(Rml::Span<const Rml::byte> Source, Rml::Vector2i SourceDimensions)
{
	if (!Device || Source.empty() || SourceDimensions.x <= 0 || SourceDimensions.y <= 0)
		return {};

	FTexture* Texture = new FTexture();
	Texture->Width = SourceDimensions.x;
	Texture->Height = SourceDimensions.y;

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = static_cast<UINT>(SourceDimensions.x);
	TextureDesc.Height = static_cast<UINT>(SourceDimensions.y);
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA TextureData = {};
	TextureData.pSysMem = Source.data();
	TextureData.SysMemPitch = static_cast<UINT>(SourceDimensions.x * 4);

	if (FAILED(Device->CreateTexture2D(&TextureDesc, &TextureData, Texture->Texture.GetAddressOf())) ||
		FAILED(Device->CreateShaderResourceView(Texture->Texture.Get(), nullptr, Texture->SRV.GetAddressOf())))
	{
		delete Texture;
		return {};
	}

	return reinterpret_cast<Rml::TextureHandle>(Texture);
}

void FRmlUiRenderInterfaceD3D11::ReleaseTexture(Rml::TextureHandle Texture)
{
	delete reinterpret_cast<FTexture*>(Texture);
}

void FRmlUiRenderInterfaceD3D11::EnableScissorRegion(bool bEnable)
{
	bScissorEnabled = bEnable;
	ID3D11RasterizerState* State = bScissorEnabled ? ScissorRasterizerState.Get() : RasterizerState.Get();
	Context->RSSetState(State);
}

void FRmlUiRenderInterfaceD3D11::SetScissorRegion(Rml::Rectanglei Region)
{
	D3D11_RECT Rect = {};
	Rect.left = Region.Left();
	Rect.top = Region.Top();
	Rect.right = Region.Right();
	Rect.bottom = Region.Bottom();
	Context->RSSetScissorRects(1, &Rect);
}

bool FRmlUiRenderInterfaceD3D11::CreateShaders()
{
	TComPtr<ID3DBlob> VertexBlob;
	TComPtr<ID3DBlob> PixelBlob;
	TComPtr<ID3DBlob> ErrorBlob;

	if (FAILED(D3DCompile(RmlUiShaderSource, strlen(RmlUiShaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, VertexBlob.GetAddressOf(), ErrorBlob.GetAddressOf())))
	{
		LogShaderError(ErrorBlob.Get());
		return false;
	}

	ErrorBlob.Reset();
	if (FAILED(D3DCompile(RmlUiShaderSource, strlen(RmlUiShaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, PixelBlob.GetAddressOf(), ErrorBlob.GetAddressOf())))
	{
		LogShaderError(ErrorBlob.Get());
		return false;
	}

	if (FAILED(Device->CreateVertexShader(VertexBlob->GetBufferPointer(), VertexBlob->GetBufferSize(), nullptr, VertexShader.GetAddressOf())) ||
		FAILED(Device->CreatePixelShader(PixelBlob->GetBufferPointer(), PixelBlob->GetBufferSize(), nullptr, PixelShader.GetAddressOf())))
	{
		return false;
	}

	D3D11_INPUT_ELEMENT_DESC Layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Rml::Vertex, position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, static_cast<UINT>(offsetof(Rml::Vertex, colour)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Rml::Vertex, tex_coord)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	if (FAILED(Device->CreateInputLayout(Layout, ARRAYSIZE(Layout), VertexBlob->GetBufferPointer(), VertexBlob->GetBufferSize(), InputLayout.GetAddressOf())))
		return false;

	D3D11_BUFFER_DESC ConstantDesc = {};
	ConstantDesc.ByteWidth = sizeof(FRmlUiConstants);
	ConstantDesc.Usage = D3D11_USAGE_DEFAULT;
	ConstantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	return SUCCEEDED(Device->CreateBuffer(&ConstantDesc, nullptr, ConstantBuffer.GetAddressOf()));
}

bool FRmlUiRenderInterfaceD3D11::CreateStates()
{
	D3D11_BLEND_DESC BlendDesc = {};
	BlendDesc.RenderTarget[0].BlendEnable = TRUE;
	BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	if (FAILED(Device->CreateBlendState(&BlendDesc, BlendState.GetAddressOf())))
		return false;

	D3D11_RASTERIZER_DESC RasterDesc = {};
	RasterDesc.FillMode = D3D11_FILL_SOLID;
	RasterDesc.CullMode = D3D11_CULL_NONE;
	RasterDesc.DepthClipEnable = TRUE;

	if (FAILED(Device->CreateRasterizerState(&RasterDesc, RasterizerState.GetAddressOf())))
		return false;

	RasterDesc.ScissorEnable = TRUE;
	if (FAILED(Device->CreateRasterizerState(&RasterDesc, ScissorRasterizerState.GetAddressOf())))
		return false;

	D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
	DepthDesc.DepthEnable = FALSE;
	DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	if (FAILED(Device->CreateDepthStencilState(&DepthDesc, DepthStencilState.GetAddressOf())))
		return false;

	D3D11_SAMPLER_DESC SamplerDesc = {};
	SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD = 0;
	SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(Device->CreateSamplerState(&SamplerDesc, SamplerState.GetAddressOf())))
		return false;

	const unsigned int WhitePixel = 0xffffffffu;
	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = 1;
	TextureDesc.Height = 1;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA TextureData = {};
	TextureData.pSysMem = &WhitePixel;
	TextureData.SysMemPitch = sizeof(WhitePixel);

	return SUCCEEDED(Device->CreateTexture2D(&TextureDesc, &TextureData, WhiteTexture.GetAddressOf())) &&
		SUCCEEDED(Device->CreateShaderResourceView(WhiteTexture.Get(), nullptr, WhiteTextureSRV.GetAddressOf()));
}

void FRmlUiRenderInterfaceD3D11::SetPipelineState()
{
	float BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	Context->OMSetBlendState(BlendState.Get(), BlendFactor, 0xffffffff);
	Context->OMSetDepthStencilState(DepthStencilState.Get(), 0);
	Context->RSSetState(bScissorEnabled ? ScissorRasterizerState.Get() : RasterizerState.Get());

	Context->IASetInputLayout(InputLayout.Get());
	Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Context->VSSetShader(VertexShader.Get(), nullptr, 0);
	Context->PSSetShader(PixelShader.Get(), nullptr, 0);
	ID3D11Buffer* Constants = ConstantBuffer.Get();
	Context->VSSetConstantBuffers(0, 1, &Constants);
	Context->PSSetConstantBuffers(0, 1, &Constants);
	ID3D11SamplerState* Sampler = SamplerState.Get();
	Context->PSSetSamplers(0, 1, &Sampler);
}
