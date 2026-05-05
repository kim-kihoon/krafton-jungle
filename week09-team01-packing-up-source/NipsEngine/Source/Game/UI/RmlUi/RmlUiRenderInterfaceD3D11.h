#pragma once

#include "Render/Common/RenderTypes.h"

#ifdef GetFirstChild
#undef GetFirstChild
#endif
#ifdef GetNextSibling
#undef GetNextSibling
#endif

#include "RmlUi/Core/RenderInterface.h"

class FRmlUiRenderInterfaceD3D11 : public Rml::RenderInterface
{
public:
	FRmlUiRenderInterfaceD3D11() = default;
	~FRmlUiRenderInterfaceD3D11() override;

	bool Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InContext);
	void Shutdown();
	void BeginFrame(int InWidth, int InHeight);

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> Vertices, Rml::Span<const int> Indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle Geometry, Rml::Vector2f Translation, Rml::TextureHandle Texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle Geometry) override;

	Rml::TextureHandle LoadTexture(Rml::Vector2i& TextureDimensions, const Rml::String& Source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> Source, Rml::Vector2i SourceDimensions) override;
	void ReleaseTexture(Rml::TextureHandle Texture) override;

	void EnableScissorRegion(bool bEnable) override;
	void SetScissorRegion(Rml::Rectanglei Region) override;

	void SetFlashFactor(float InFactor) { FlashFactor = InFactor; }

private:
	struct FGeometry;
	struct FTexture;

	bool CreateShaders();
	bool CreateStates();
	void SetPipelineState();

private:
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* Context = nullptr;
	int Width = 1;
	int Height = 1;
	bool bScissorEnabled = false;
	float FlashFactor = 0.0f;

	TComPtr<ID3D11VertexShader> VertexShader;
	TComPtr<ID3D11PixelShader> PixelShader;
	TComPtr<ID3D11InputLayout> InputLayout;
	TComPtr<ID3D11Buffer> ConstantBuffer;
	TComPtr<ID3D11BlendState> BlendState;
	TComPtr<ID3D11RasterizerState> RasterizerState;
	TComPtr<ID3D11RasterizerState> ScissorRasterizerState;
	TComPtr<ID3D11DepthStencilState> DepthStencilState;
	TComPtr<ID3D11SamplerState> SamplerState;
	TComPtr<ID3D11Texture2D> WhiteTexture;
	TComPtr<ID3D11ShaderResourceView> WhiteTextureSRV;
};
