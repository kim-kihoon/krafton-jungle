#include "UI/UIManager.h"

#include "Core/Logging/Log.h"
#include "Component/Script/LuaBlueprintComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Core/Types/PropertyTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/GameMode/GameplayStatics.h"
#include "Input/InputSystem.h"
#include "Object/Object.h"
#include "Object/FName.h"
#include "Object/Reflection/UClass.h"
#include "Platform/Paths.h"
#include "Render/Command/DrawCommandList.h"
#include "Render/Device/D3DDevice.h"
#include "Render/RenderPass/RenderPassBase.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Types/FrameContext.h"
#include "UI/CrosshairOverlay.h"
#include "UI/PhotoOverlay.h"
#include "UI/UserWidget.h"
#include "WICTextureLoader.h"

#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#include <RmlUi/Core.h>
#include <RmlUi/Core/Factory.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <memory>

namespace
{
	struct FRmlVertexD3D11
	{
		float X, Y;
		float R, G, B, A;
		float U, V;
	};

	struct FRmlGeometryD3D11
	{
		ID3D11Buffer* VertexBuffer = nullptr;
		ID3D11Buffer* IndexBuffer = nullptr;
		UINT IndexCount = 0;
	};

	struct FRmlTextureD3D11
	{
		ID3D11ShaderResourceView* SRV = nullptr;
	};

	struct FRmlPerFrameCB
	{
		float ViewportWidth = 1.0f;
		float ViewportHeight = 1.0f;
		float TranslationX = 0.0f;
		float TranslationY = 0.0f;
		float Transform[16] = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};
	};

	constexpr const char* UIShaderPath = "Shaders/UI/RmlUi.hlsl";
	constexpr float NavigationStickThreshold = 0.5f;
	constexpr double NavigationInitialRepeatDelay = 0.32;
	constexpr double NavigationRepeatDelay = 0.12;
	constexpr float UIDesignWidth = 1920.0f;
	constexpr float UIDesignHeight = 1080.0f;

	void UpdateUILayoutMetrics(float ViewportWidth, float ViewportHeight, float& OutScale, float& OutOffsetX, float& OutOffsetY)
	{
		if (ViewportWidth <= 0.0f || ViewportHeight <= 0.0f)
		{
			OutScale = 1.0f;
			OutOffsetX = 0.0f;
			OutOffsetY = 0.0f;
			return;
		}

		OutScale = std::min(ViewportWidth / UIDesignWidth, ViewportHeight / UIDesignHeight);
		const float ScaledWidth = UIDesignWidth * OutScale;
		const float ScaledHeight = UIDesignHeight * OutScale;
		OutOffsetX = (ViewportWidth - ScaledWidth) * 0.5f;
		OutOffsetY = (ViewportHeight - ScaledHeight) * 0.5f;
	}

	Rml::Matrix4f BuildUILayoutTransform(float Scale, float OffsetX, float OffsetY)
	{
		return Rml::Matrix4f::Translate(OffsetX, OffsetY, 0.0f) * Rml::Matrix4f::Scale(Scale, Scale, 1.0f);
	}

	int32 MapViewportMouseToDesign(float MouseCoord, float Offset, float Scale)
	{
		if (Scale <= 0.0f)
		{
			return static_cast<int32>(MouseCoord);
		}
		return static_cast<int32>((MouseCoord - Offset) / Scale);
	}

	constexpr float HudRootFontPxAt1080p = 16.0f;

	EUIRenderLayout ResolveLayoutMode(const FString& DocumentPath, Rml::ElementDocument* Document)
	{
		if (Document)
		{
			const Rml::String LayoutAttr = Document->GetAttribute<Rml::String>("data-ui-layout", "scaled");
			if (LayoutAttr == "hud" || LayoutAttr == "screen")
			{
				return EUIRenderLayout::ScreenHud;
			}
			if (LayoutAttr == "scaled" || LayoutAttr == "menu")
			{
				return EUIRenderLayout::ScaledDesign;
			}
		}

		const FString LowerPath = [&DocumentPath]()
		{
			FString Lower = DocumentPath;
			std::transform(Lower.begin(), Lower.end(), Lower.begin(),
				[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
			return Lower;
		}();

		if (LowerPath.find("hospital") != FString::npos
			|| LowerPath.find("doorprompt") != FString::npos)
		{
			return EUIRenderLayout::ScreenHud;
		}

		return EUIRenderLayout::ScaledDesign;
	}

	constexpr float PhotoFlashSeconds = 0.2f;
	constexpr int32 CrosshairSegments = 10;
	constexpr float CrosshairRadius = 1.4f;
	constexpr float Pi = 3.14159265358979323846f;

	float Clamp01(float Value)
	{
		if (Value < 0.0f)
		{
			return 0.0f;
		}
		if (Value > 1.0f)
		{
			return 1.0f;
		}
		return Value;
	}

	uint8 AlphaByte(float Alpha)
	{
		return static_cast<uint8>(Clamp01(Alpha) * 255.0f);
	}

	Rml::Vertex MakeOverlayVertex(float X, float Y, uint8 R, uint8 G, uint8 B, uint8 A)
	{
		Rml::Vertex Vertex;
		Vertex.position = Rml::Vector2f(X, Y);
		Vertex.colour.red = R;
		Vertex.colour.green = G;
		Vertex.colour.blue = B;
		Vertex.colour.alpha = A;
		Vertex.tex_coord = Rml::Vector2f(0.0f, 0.0f);
		return Vertex;
	}

	void AppendOverlayRect(TArray<Rml::Vertex>& Vertices, TArray<int>& Indices,
		float Left, float Top, float Right, float Bottom, uint8 R, uint8 G, uint8 B, uint8 A)
	{
		const int BaseIndex = static_cast<int>(Vertices.size());
		Vertices.push_back(MakeOverlayVertex(Left, Top, R, G, B, A));
		Vertices.push_back(MakeOverlayVertex(Right, Top, R, G, B, A));
		Vertices.push_back(MakeOverlayVertex(Right, Bottom, R, G, B, A));
		Vertices.push_back(MakeOverlayVertex(Left, Bottom, R, G, B, A));

		Indices.push_back(BaseIndex + 0);
		Indices.push_back(BaseIndex + 1);
		Indices.push_back(BaseIndex + 2);
		Indices.push_back(BaseIndex + 0);
		Indices.push_back(BaseIndex + 2);
		Indices.push_back(BaseIndex + 3);
	}

	void AppendOverlayCircle(TArray<Rml::Vertex>& Vertices, TArray<int>& Indices,
		float CenterX, float CenterY, float Radius, uint8 R, uint8 G, uint8 B, uint8 A)
	{
		const int CenterIndex = static_cast<int>(Vertices.size());
		Vertices.push_back(MakeOverlayVertex(CenterX, CenterY, R, G, B, A));

		for (int32 Segment = 0; Segment < CrosshairSegments; ++Segment)
		{
			const float Angle = (static_cast<float>(Segment) / static_cast<float>(CrosshairSegments)) * Pi * 2.0f;
			Vertices.push_back(MakeOverlayVertex(
				CenterX + std::cos(Angle) * Radius,
				CenterY + std::sin(Angle) * Radius,
				R, G, B, A));
		}

		for (int32 Segment = 0; Segment < CrosshairSegments; ++Segment)
		{
			Indices.push_back(CenterIndex);
			Indices.push_back(CenterIndex + 1 + Segment);
			Indices.push_back(CenterIndex + 1 + ((Segment + 1) % CrosshairSegments));
		}
	}

	std::filesystem::path ToProjectPath(const FString& Path)
	{
		std::filesystem::path Result(FPaths::ToWide(Path));
		if (Result.is_relative())
		{
			Result = std::filesystem::path(FPaths::RootDir()) / Result;
		}
		return Result;
	}

	Rml::String ToRmlPath(const std::filesystem::path& Path)
	{
		return FPaths::ToUtf8(Path.generic_wstring());
	}

	bool InvokeActorClickFunction(AActor* Actor, const FString& FunctionName)
	{
		if (!Actor || FunctionName.empty())
		{
			return false;
		}

		if (UClass* Class = Actor->GetClass())
		{
			const FFunction* Function = Class->FindFunctionByName(FunctionName.c_str(), true);
			if (Function)
			{
				if (Function->GetParameterCount() == 0 && !Function->HasReturnValue())
				{
					return Actor->ProcessEvent(Function, nullptr, nullptr);
				}

				UE_LOG(
					"[RmlUi] UI click function '%s' on actor '%s' has parameters or return value; skipped C++ reflection call.",
					FunctionName.c_str(),
					Actor->GetName().c_str()
				);
			}
		}

		if (ULuaScriptComponent* LuaScript = Actor->GetComponentByClass<ULuaScriptComponent>())
		{
			if (LuaScript->CallFunction(FunctionName))
			{
				return true;
			}
		}

		if (ULuaBlueprintComponent* LuaBlueprint = Actor->GetComponentByClass<ULuaBlueprintComponent>())
		{
			if (LuaBlueprint->CallFunction(FunctionName))
			{
				return true;
			}
		}

		return false;
	}

	bool ContainsAsciiInsensitive(FString Text, const FString& Needle)
	{
		auto ToLower = [](FString Value)
		{
			std::transform(Value.begin(), Value.end(), Value.begin(),
				[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
			return Value;
		};
		return ToLower(Text).find(ToLower(Needle)) != FString::npos;
	}

	bool WasGamepadButtonPressed(const FInputDeviceSnapshot& Snapshot, EGamepadButton Button)
	{
		const int32 ButtonIndex = static_cast<int32>(Button);
		return ButtonIndex >= 0 &&
			ButtonIndex < static_cast<int32>(EGamepadButton::Count) &&
			Snapshot.Buttons[ButtonIndex] &&
			!Snapshot.PrevButtons[ButtonIndex];
	}

	bool IsGamepadButtonDown(const FInputDeviceSnapshot& Snapshot, EGamepadButton Button)
	{
		const int32 ButtonIndex = static_cast<int32>(Button);
		return ButtonIndex >= 0 &&
			ButtonIndex < static_cast<int32>(EGamepadButton::Count) &&
			Snapshot.Buttons[ButtonIndex];
	}

	float GetGamepadAxisValue(const FInputDeviceSnapshot& Snapshot, EGamepadAxis Axis)
	{
		const int32 AxisIndex = static_cast<int32>(Axis);
		return AxisIndex >= 0 && AxisIndex < static_cast<int32>(EGamepadAxis::Count)
			? Snapshot.Axes[AxisIndex]
			: 0.0f;
	}

	int32 AxisToNavigationDirection(float Value)
	{
		if (Value <= -NavigationStickThreshold)
		{
			return -1;
		}
		if (Value >= NavigationStickThreshold)
		{
			return 1;
		}
		return 0;
	}
}

double FRmlSystemInterface::GetElapsedTime()
{
	using namespace std::chrono;
	const auto Now = steady_clock::now();
	return duration<double>(Now - StartTime).count();
}

void FRmlSystemInterface::JoinPath(Rml::String& TranslatedPath, const Rml::String& DocumentPath, const Rml::String& Path)
{
	std::filesystem::path ResourcePath(FPaths::ToWide(Path));
	if (!ResourcePath.is_relative())
	{
		TranslatedPath = ToRmlPath(ResourcePath);
		return;
	}

	if (!ResourcePath.empty())
	{
		const std::wstring FirstPart = ResourcePath.begin()->wstring();
		if (FirstPart == L"Content" || FirstPart == L"Shaders" || FirstPart == L"ThirdParty")
		{
			TranslatedPath = ToRmlPath(ToProjectPath(Path));
			return;
		}
	}

	std::filesystem::path BasePath(FPaths::ToWide(DocumentPath));
	TranslatedPath = ToRmlPath(BasePath.parent_path() / ResourcePath);
}

bool FRmlSystemInterface::LogMessage(Rml::Log::Type Type, const Rml::String& Message)
{
	UE_LOG("[RmlUi] %s", Message.c_str());
	return Type != Rml::Log::LT_ASSERT;
}

// FRmlFileInterfaceWide — 모든 RmlUi 파일 열기를 wide API 로 우회. 한글 경로의 디렉토리
// 에서 실행될 때 기본 fopen 경로가 ANSI 로 해석되며 깨지는 것을 방지.
Rml::FileHandle FRmlFileInterfaceWide::Open(const Rml::String& Path)
{
	const std::wstring WidePath = FPaths::ToWide(Path);
	FILE* Fp = nullptr;
	if (_wfopen_s(&Fp, WidePath.c_str(), L"rb") != 0 || !Fp)
	{
		return Rml::FileHandle{};
	}
	return reinterpret_cast<Rml::FileHandle>(Fp);
}

void FRmlFileInterfaceWide::Close(Rml::FileHandle FileHandle)
{
	if (FileHandle)
	{
		fclose(reinterpret_cast<FILE*>(FileHandle));
	}
}

size_t FRmlFileInterfaceWide::Read(void* Buffer, size_t Size, Rml::FileHandle FileHandle)
{
	if (!FileHandle) return 0;
	return fread(Buffer, 1, Size, reinterpret_cast<FILE*>(FileHandle));
}

bool FRmlFileInterfaceWide::Seek(Rml::FileHandle FileHandle, long Offset, int Origin)
{
	if (!FileHandle) return false;
	return fseek(reinterpret_cast<FILE*>(FileHandle), Offset, Origin) == 0;
}

size_t FRmlFileInterfaceWide::Tell(Rml::FileHandle FileHandle)
{
	if (!FileHandle) return 0;
	const long Pos = ftell(reinterpret_cast<FILE*>(FileHandle));
	return Pos < 0 ? 0 : static_cast<size_t>(Pos);
}

FRmlRenderInterfaceD3D11::FRmlRenderInterfaceD3D11(ID3D11Device* InDevice)
	: Device(InDevice)
	, CurrentTransform(Rml::Matrix4f::Identity())
{
	CreateConstantBuffer();
}

FRmlRenderInterfaceD3D11::~FRmlRenderInterfaceD3D11()
{
	ReleaseWhiteTexture();
	if (ScissorRasterizerState)
	{
		ScissorRasterizerState->Release();
		ScissorRasterizerState = nullptr;
	}
	if (PerFrameCB)
	{
		PerFrameCB->Release();
		PerFrameCB = nullptr;
	}
}

void FRmlRenderInterfaceD3D11::BeginFrame(const FPassContext& InCtx)
{
	Ctx = &InCtx;

	ID3D11DeviceContext* DC = Ctx->Device.GetDeviceContext();
	if (!DC)
	{
		return;
	}

	D3D11_VIEWPORT Viewport = {};
	Viewport.TopLeftX = 0.0f;
	Viewport.TopLeftY = 0.0f;
	Viewport.Width = Ctx->Frame.ViewportWidth;
	Viewport.Height = Ctx->Frame.ViewportHeight;
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;
	DC->RSSetViewports(1, &Viewport);
}

void FRmlRenderInterfaceD3D11::EndFrame()
{
	Ctx = nullptr;
}

void FRmlRenderInterfaceD3D11::SetTransform(const Rml::Matrix4f* Transform)
{
	CurrentTransform = Transform ? *Transform : Rml::Matrix4f::Identity();
}

Rml::CompiledGeometryHandle FRmlRenderInterfaceD3D11::CompileGeometry(Rml::Span<const Rml::Vertex> Vertices, Rml::Span<const int> Indices)
{
	if (!Device || Vertices.empty() || Indices.empty())
	{
		return 0;
	}

	TArray<FRmlVertexD3D11> ConvertedVertices;
	ConvertedVertices.reserve(Vertices.size());
	for (const Rml::Vertex& Vertex : Vertices)
	{
		ConvertedVertices.push_back({
			Vertex.position.x,
			Vertex.position.y,
			Vertex.colour.red / 255.0f,
			Vertex.colour.green / 255.0f,
			Vertex.colour.blue / 255.0f,
			Vertex.colour.alpha / 255.0f,
			Vertex.tex_coord.x,
			Vertex.tex_coord.y,
		});
	}

	TArray<uint32> ConvertedIndices;
	ConvertedIndices.reserve(Indices.size());
	for (int Index : Indices)
	{
		ConvertedIndices.push_back(static_cast<uint32>(Index));
	}

	auto* Geometry = new FRmlGeometryD3D11();
	Geometry->IndexCount = static_cast<UINT>(ConvertedIndices.size());

	D3D11_BUFFER_DESC VBDesc = {};
	VBDesc.Usage = D3D11_USAGE_DEFAULT;
	VBDesc.ByteWidth = static_cast<UINT>(sizeof(FRmlVertexD3D11) * ConvertedVertices.size());
	VBDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA VBData = {};
	VBData.pSysMem = ConvertedVertices.data();
	if (FAILED(Device->CreateBuffer(&VBDesc, &VBData, &Geometry->VertexBuffer)))
	{
		delete Geometry;
		return 0;
	}
	Geometry->VertexBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("RmlGeometryVertexBuffer")), "RmlGeometryVertexBuffer");

	D3D11_BUFFER_DESC IBDesc = {};
	IBDesc.Usage = D3D11_USAGE_DEFAULT;
	IBDesc.ByteWidth = static_cast<UINT>(sizeof(uint32) * ConvertedIndices.size());
	IBDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

	D3D11_SUBRESOURCE_DATA IBData = {};
	IBData.pSysMem = ConvertedIndices.data();
	if (FAILED(Device->CreateBuffer(&IBDesc, &IBData, &Geometry->IndexBuffer)))
	{
		ReleaseGeometry(reinterpret_cast<Rml::CompiledGeometryHandle>(Geometry));
		return 0;
	}
	Geometry->IndexBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("RmlGeometryIndexBuffer")), "RmlGeometryIndexBuffer");

	return reinterpret_cast<Rml::CompiledGeometryHandle>(Geometry);
}

void FRmlRenderInterfaceD3D11::RenderGeometry(Rml::CompiledGeometryHandle GeometryHandle, Rml::Vector2f Translation, Rml::TextureHandle Texture)
{
	if (!Ctx || !GeometryHandle)
	{
		return;
	}

	auto* Geometry = reinterpret_cast<FRmlGeometryD3D11*>(GeometryHandle);
	ID3D11DeviceContext* DC = Ctx->Device.GetDeviceContext();
	if (!DC || !Geometry->VertexBuffer || !Geometry->IndexBuffer)
	{
		return;
	}

	FShader* Shader = FShaderManager::Get().GetOrCreate(UIShaderPath);
	if (!Shader || !Shader->IsValid())
	{
		return;
	}

	Ctx->Resources.SetDepthStencilState(Ctx->Device, EDepthStencilState::NoDepth);
	Ctx->Resources.SetBlendState(Ctx->Device, EBlendState::AlphaBlend);
	Ctx->Resources.SetRasterizerState(Ctx->Device, ERasterizerState::SolidNoCull);

	DC->OMSetRenderTargets(1, &Ctx->Cache.RTV, Ctx->Cache.DSV);
	DC->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Shader->Bind(DC);

	FRmlPerFrameCB CBData;
	CBData.ViewportWidth = Ctx->Frame.ViewportWidth;
	CBData.ViewportHeight = Ctx->Frame.ViewportHeight;
	CBData.TranslationX = Translation.x;
	CBData.TranslationY = Translation.y;
	const float* TransformData = CurrentTransform.data();
	std::copy(TransformData, TransformData + 16, CBData.Transform);
	DC->UpdateSubresource(PerFrameCB, 0, nullptr, &CBData, 0, 0);
	DC->VSSetConstantBuffers(0, 1, &PerFrameCB);

	ID3D11ShaderResourceView* SRV = WhiteTextureSRV;
	if (Texture)
	{
		auto* TextureResource = reinterpret_cast<FRmlTextureD3D11*>(Texture);
		SRV = TextureResource ? TextureResource->SRV : nullptr;
	}
	DC->PSSetShaderResources(0, 1, &SRV);

	UINT Stride = sizeof(FRmlVertexD3D11);
	UINT Offset = 0;
	DC->IASetVertexBuffers(0, 1, &Geometry->VertexBuffer, &Stride, &Offset);
	DC->IASetIndexBuffer(Geometry->IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	DC->DrawIndexed(Geometry->IndexCount, 0, 0);
}

static void RenderImmediateOverlayGeometry(FRmlRenderInterfaceD3D11* RenderInterface,
	const TArray<Rml::Vertex>& Vertices, const TArray<int>& Indices)
{
	if (!RenderInterface || Vertices.empty() || Indices.empty())
	{
		return;
	}

	const Rml::CompiledGeometryHandle Geometry = RenderInterface->CompileGeometry(
		Rml::Span<const Rml::Vertex>(Vertices.data(), Vertices.size()),
		Rml::Span<const int>(Indices.data(), Indices.size()));
	if (!Geometry)
	{
		return;
	}

	RenderInterface->RenderGeometry(Geometry, Rml::Vector2f(0.0f, 0.0f), 0);
	RenderInterface->ReleaseGeometry(Geometry);
}

void FRmlRenderInterfaceD3D11::ReleaseGeometry(Rml::CompiledGeometryHandle GeometryHandle)
{
	auto* Geometry = reinterpret_cast<FRmlGeometryD3D11*>(GeometryHandle);
	if (!Geometry)
	{
		return;
	}

	if (Geometry->VertexBuffer)
	{
		Geometry->VertexBuffer->Release();
	}
	if (Geometry->IndexBuffer)
	{
		Geometry->IndexBuffer->Release();
	}
	delete Geometry;
}

Rml::TextureHandle FRmlRenderInterfaceD3D11::LoadTexture(Rml::Vector2i& TextureDimensions, const Rml::String& Source)
{
	TextureDimensions = { 0, 0 };

	if (!Device || Source.empty())
	{
		return 0;
	}

	const std::wstring WidePath = FPaths::ToWide(Source);

	ID3D11Resource* Resource = nullptr;
	ID3D11ShaderResourceView* SRV = nullptr;
	const HRESULT HR = DirectX::CreateWICTextureFromFileEx(
		Device,
		WidePath.c_str(),
		0,
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_SHADER_RESOURCE,
		0,
		0,
		DirectX::WIC_LOADER_IGNORE_SRGB,
		&Resource,
		&SRV);

	if (FAILED(HR) || !SRV)
	{
		if (Resource)
		{
			Resource->Release();
		}
		UE_LOG("[RmlUi] Failed to load texture: %s", Source.c_str());
		return 0;
	}

	if (Resource)
	{
		ID3D11Texture2D* Texture2D = nullptr;
		if (SUCCEEDED(Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture2D))) && Texture2D)
		{
			D3D11_TEXTURE2D_DESC Desc = {};
			Texture2D->GetDesc(&Desc);
			TextureDimensions = {
				static_cast<int>(Desc.Width),
				static_cast<int>(Desc.Height)
			};
			Texture2D->Release();
		}
		Resource->Release();
	}

	auto* TextureResource = new FRmlTextureD3D11();
	TextureResource->SRV = SRV;
	return reinterpret_cast<Rml::TextureHandle>(TextureResource);
}

Rml::TextureHandle FRmlRenderInterfaceD3D11::GenerateTexture(Rml::Span<const Rml::byte> Source, Rml::Vector2i SourceDimensions)
{
	if (!Device || Source.empty() || SourceDimensions.x <= 0 || SourceDimensions.y <= 0)
	{
		return 0;
	}

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = static_cast<UINT>(SourceDimensions.x);
	TextureDesc.Height = static_cast<UINT>(SourceDimensions.y);
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA InitialData = {};
	InitialData.pSysMem = Source.data();
	InitialData.SysMemPitch = static_cast<UINT>(SourceDimensions.x * 4);

	ID3D11Texture2D* Texture = nullptr;
	if (FAILED(Device->CreateTexture2D(&TextureDesc, &InitialData, &Texture)))
	{
		return 0;
	}

	ID3D11ShaderResourceView* SRV = nullptr;
	HRESULT HR = Device->CreateShaderResourceView(Texture, nullptr, &SRV);
	Texture->Release();
	if (FAILED(HR))
	{
		return 0;
	}

	auto* TextureResource = new FRmlTextureD3D11();
	TextureResource->SRV = SRV;
	return reinterpret_cast<Rml::TextureHandle>(TextureResource);
}

void FRmlRenderInterfaceD3D11::ReleaseTexture(Rml::TextureHandle Texture)
{
	auto* TextureResource = reinterpret_cast<FRmlTextureD3D11*>(Texture);
	if (!TextureResource)
	{
		return;
	}
	if (TextureResource->SRV)
	{
		TextureResource->SRV->Release();
	}
	delete TextureResource;
}

void FRmlRenderInterfaceD3D11::EnableScissorRegion(bool Enable)
{
	if (!Ctx)
	{
		return;
	}

	ID3D11DeviceContext* DC = Ctx->Device.GetDeviceContext();
	if (Enable && ScissorRasterizerState)
	{
		DC->RSSetState(ScissorRasterizerState);
	}
	else
	{
		Ctx->Resources.SetRasterizerState(Ctx->Device, ERasterizerState::SolidNoCull);
	}

	if (!Enable)
	{
		DC->RSSetScissorRects(0, nullptr);
	}
}

void FRmlRenderInterfaceD3D11::SetScissorRegion(Rml::Rectanglei Region)
{
	if (!Ctx)
	{
		return;
	}

	D3D11_RECT Rect = {};
	Rect.left = Region.Left();
	Rect.top = Region.Top();
	Rect.right = Region.Right();
	Rect.bottom = Region.Bottom();
	Ctx->Device.GetDeviceContext()->RSSetScissorRects(1, &Rect);
}

void FRmlRenderInterfaceD3D11::CreateConstantBuffer()
{
	if (!Device)
	{
		return;
	}

	D3D11_BUFFER_DESC Desc = {};
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.ByteWidth = sizeof(FRmlPerFrameCB);
	Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	Device->CreateBuffer(&Desc, nullptr, &PerFrameCB);
	PerFrameCB->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("RmlPerFrameCB")), "RmlPerFrameCB");

	CreateWhiteTexture();

	D3D11_RASTERIZER_DESC RasterDesc = {};
	RasterDesc.FillMode = D3D11_FILL_SOLID;
	RasterDesc.CullMode = D3D11_CULL_NONE;
	RasterDesc.ScissorEnable = TRUE;
	Device->CreateRasterizerState(&RasterDesc, &ScissorRasterizerState);
}

void FRmlRenderInterfaceD3D11::CreateWhiteTexture()
{
	const uint32 WhitePixel = 0xffffffff;

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = 1;
	TextureDesc.Height = 1;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = 1;
	TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA InitialData = {};
	InitialData.pSysMem = &WhitePixel;
	InitialData.SysMemPitch = sizeof(uint32);

	ID3D11Texture2D* Texture = nullptr;
	if (SUCCEEDED(Device->CreateTexture2D(&TextureDesc, &InitialData, &Texture)))
	{
		Device->CreateShaderResourceView(Texture, nullptr, &WhiteTextureSRV);
		Texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("RmlWhiteTexture")), "RmlWhiteTexture");
		WhiteTextureSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen("RmlWhiteTextureSRV")), "RmlWhiteTextureSRV");
		Texture->Release();
	}
}

void FRmlRenderInterfaceD3D11::ReleaseWhiteTexture()
{
	if (WhiteTextureSRV)
	{
		WhiteTextureSRV->Release();
		WhiteTextureSRV = nullptr;
	}
}

void UUIManager::Initialize(ID3D11Device* InDevice)
{
	CachedDevice = InDevice;

	if (bRmlInitialized || !CachedDevice)
	{
		return;
	}

	SystemInterface = new FRmlSystemInterface();
	FileInterface = new FRmlFileInterfaceWide();
	RenderInterface = new FRmlRenderInterfaceD3D11(CachedDevice);

	Rml::SetSystemInterface(SystemInterface);
	// Initialise 전에 등록해야 RmlUi 가 default file 인터페이스 대신 우리 wide 버전을 쓴다.
	Rml::SetFileInterface(FileInterface);
	Rml::SetRenderInterface(RenderInterface);
	bRmlInitialized = Rml::Initialise();
	if (!bRmlInitialized)
	{
		UE_LOG("[RmlUi] Initialise failed.");
		return;
	}

	RmlContext = Rml::CreateContext("GameViewport", Rml::Vector2i(1, 1));
	if (!RmlContext)
	{
		UE_LOG("[RmlUi] Failed to create GameViewport context.");
	}

	const std::filesystem::path NormalFontPath = ToProjectPath("Content/Font/Maplestory Light.ttf");
	if (!Rml::LoadFontFace(ToRmlPath(NormalFontPath), "Maplestory", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal))
	{
		UE_LOG("[RmlUi] Failed to load font: Content/Font/Maplestory Light.ttf");
	}

	const std::filesystem::path BoldFontPath = ToProjectPath("Content/Font/Maplestory Bold.ttf");
	if (!Rml::LoadFontFace(ToRmlPath(BoldFontPath), "Maplestory", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold))
	{
		UE_LOG("[RmlUi] Failed to load font: Content/Font/Maplestory Bold.ttf");
	}

	const std::filesystem::path FontRoot = ToProjectPath("Content/Font");
	std::error_code Ec;
	for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(FontRoot, Ec))
	{
		if (Ec || !Entry.is_regular_file())
		{
			continue;
		}

		const std::filesystem::path Path = Entry.path();
		const FString Extension = FPaths::ToUtf8(Path.extension().generic_wstring());
		if (!ContainsAsciiInsensitive(Extension, ".ttf") && !ContainsAsciiInsensitive(Extension, ".otf"))
		{
			continue;
		}

		const FString Family = FPaths::ToUtf8(Path.stem().generic_wstring());
		const Rml::Style::FontWeight Weight = ContainsAsciiInsensitive(Family, "bold")
			? Rml::Style::FontWeight::Bold
			: Rml::Style::FontWeight::Normal;
		if (!Rml::LoadFontFace(ToRmlPath(Path), Family, Rml::Style::FontStyle::Normal, Weight))
		{
			UE_LOG("[RmlUi] Failed to load font: %s", ToRmlPath(Path).c_str());
		}
	}
}

void UUIManager::Shutdown()
{
	DestroyAllWidgets();

	if (RmlContext)
	{
		Rml::RemoveContext("GameViewport");
		RmlContext = nullptr;
	}

	if (bRmlInitialized)
	{
		Rml::Shutdown();
		bRmlInitialized = false;
	}

	delete RenderInterface;
	RenderInterface = nullptr;
	delete FileInterface;
	FileInterface = nullptr;
	delete SystemInterface;
	SystemInterface = nullptr;
	CachedDevice = nullptr;
}

UUserWidget* UUIManager::CreateWidget(APlayerController* OwningPlayer, const FString& DocumentPath)
{
	CompactInvalidWidgets();
	UUserWidget* Widget = UObjectManager::Get().CreateObject<UUserWidget>();
	Widget->Initialize(OwningPlayer, DocumentPath);
	CreatedWidgets.push_back(Widget);
	return Widget;
}

void UUIManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(CreatedWidgets, "UUIManager.CreatedWidgets");
	Collector.AddReferencedObjects(ViewportWidgets, "UUIManager.ViewportWidgets");
	Collector.AddReferencedObjects(PendingRemoveWidgets, "UUIManager.PendingRemoveWidgets");
}

void UUIManager::CompactInvalidWidgets()
{
	auto RemoveInvalid = [](TArray<UUserWidget*>& Widgets)
	{
		Widgets.erase(
			std::remove_if(
				Widgets.begin(),
				Widgets.end(),
				[](UUserWidget* Widget)
				{
					return !IsValid(Widget);
				}),
			Widgets.end());
	};

	RemoveInvalid(ViewportWidgets);
	RemoveInvalid(CreatedWidgets);
	RemoveInvalid(PendingRemoveWidgets);
}

FUIInputCaptureState UUIManager::GetViewportInputCaptureState() const
{
	FUIInputCaptureState State;
	for (const UUserWidget* Widget : ViewportWidgets)
	{
		if (!IsValid(Widget))
		{
			continue;
		}

		State.bWantsMouse = State.bWantsMouse || Widget->WantsMouse();
		State.bWantsKeyboard = State.bWantsKeyboard || Widget->WantsKeyboard();
		State.bWantsTextInput = State.bWantsTextInput || Widget->WantsTextInput();
		State.bBlocksGameInput = State.bBlocksGameInput || Widget->BlocksGameInput();
		State.bBlocksGameKeyboard = State.bBlocksGameKeyboard || Widget->BlocksGameKeyboard();
		State.bBlocksGameMouseLook = State.bBlocksGameMouseLook || Widget->BlocksGameMouseLook();
	}
	return State;
}

bool UUIManager::AnyViewportWidgetWantsMouse() const
{
	return GetViewportInputCaptureState().bWantsMouse;
}

void UUIManager::PrepareOpenedMenuWithoutInitialHover(UUserWidget* Widget)
{
	if (!IsValid(Widget))
	{
		bPauseMenuAwaitingArmClick = false;
		bPauseMenuArmClickInProgress = false;
		bPauseMenuIgnoreMouseUntilRelease = false;
		PauseMenuArmWidget = nullptr;
		return;
	}

	Widget->ClearAllNavigationHighlightStates();
	Widget->SetGamepadNavigationHighlightEnabled(false);

	if (RmlContext)
	{
		RmlContext->ProcessMouseLeave();
	}

	bPauseMenuAwaitingArmClick = true;
	bPauseMenuArmClickInProgress = false;
	bPauseMenuIgnoreMouseUntilRelease = InputSystem::Get().GetKey(VK_LBUTTON);
	PauseMenuArmWidget = Widget;
}

void UUIManager::AddToViewport(UUserWidget* Widget, int32 /*ZOrder*/)
{
	CompactInvalidWidgets();
	if (!IsValid(Widget))
	{
		return;
	}

	if (!LoadDocument(Widget))
	{
		return;
	}

	auto It = std::find(ViewportWidgets.begin(), ViewportWidgets.end(), Widget);
	if (It == ViewportWidgets.end())
	{
		ViewportWidgets.push_back(Widget);
	}

	std::sort(ViewportWidgets.begin(), ViewportWidgets.end(),
		[](const UUserWidget* A, const UUserWidget* B)
		{
			return A->GetZOrder() < B->GetZOrder();
		});

	if (Widget->GetDocumentPath().find("PauseMenuUI") != FString::npos)
	{
		PrepareOpenedMenuWithoutInitialHover(Widget);
	}
}

void UUIManager::RemoveFromViewport(UUserWidget* Widget)
{
	CompactInvalidWidgets();
	if (!IsAliveObject(Widget))
	{
		return;
	}

	if (bDispatchingRmlEvents)
	{
		if (std::find(PendingRemoveWidgets.begin(), PendingRemoveWidgets.end(), Widget) == PendingRemoveWidgets.end())
		{
			PendingRemoveWidgets.push_back(Widget);
			Widget->MarkRemovedFromViewport();
		}
		return;
	}

	RemoveFromViewportImmediate(Widget);
}

void UUIManager::RemoveFromViewportImmediate(UUserWidget* Widget)
{
	ViewportWidgets.erase(std::remove(ViewportWidgets.begin(), ViewportWidgets.end(), Widget), ViewportWidgets.end());
	if (Widget == PauseMenuArmWidget)
	{
		bPauseMenuAwaitingArmClick = false;
		bPauseMenuArmClickInProgress = false;
		bPauseMenuIgnoreMouseUntilRelease = false;
		PauseMenuArmWidget = nullptr;
	}
	CloseDocument(Widget);
	if (IsAliveObject(Widget))
	{
		Widget->MarkRemovedFromViewport();
	}
}

bool UUIManager::ReloadDocument(UUserWidget* Widget)
{
	if (!IsAliveObject(Widget))
	{
		return false;
	}

	const bool bWasInViewport = Widget->IsInViewport();
	CloseDocument(Widget);

	if (!bWasInViewport)
	{
		return true;
	}

	return LoadDocument(Widget);
}

void UUIManager::ClearRmlCaches()
{
	if (!bRmlInitialized)
	{
		return;
	}

	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
}

int32 UUIManager::ReloadDocumentsByPath(const FString& DocumentPath)
{
	int32 ReloadedCount = 0;
	ClearRmlCaches();

	const std::filesystem::path TargetPath = ToProjectPath(DocumentPath).lexically_normal();
	for (UUserWidget* Widget : ViewportWidgets)
	{
		if (!IsAliveObject(Widget))
		{
			continue;
		}

		const std::filesystem::path WidgetPath = ToProjectPath(Widget->GetDocumentPath()).lexically_normal();
		if (WidgetPath != TargetPath)
		{
			continue;
		}

		if (ReloadDocument(Widget))
		{
			++ReloadedCount;
		}
	}

	return ReloadedCount;
}

void UUIManager::ClearViewport()
{
	// 위젯을 viewport 에서만 떼고 UObject 자체는 유지. UUIManager 는 widgets 의 owner —
	// 같은 Lua VM 안의 widgets[] 테이블이 그대로 살아있고, PIE 재시작 / TransitionToScene
	// 후 UIManager.Init re-entry 경로가 동일 위젯을 재사용한다 (위젯 destroy 시 Lua 측
	// 캐시가 dangling 이 되어 RemoveFromParent → CloseDocument 가 stale Rml::Document 를
	// 참조해 크래시). UObject 까지 파괴하는 건 Shutdown 만의 책임.
	PendingRemoveWidgets.clear();

	for (UUserWidget* Widget : ViewportWidgets)
	{
		if (IsAliveObject(Widget))
		{
			CloseDocument(Widget);
			Widget->MarkRemovedFromViewport();
		}
	}
	ViewportWidgets.clear();

	if (RmlContext)
	{
		RmlContext->Update();
	}
}

void UUIManager::DestroyAllWidgets()
{
	ClearViewport();
	CompactInvalidWidgets();

	for (UUserWidget* Widget : CreatedWidgets)
	{
		if (IsAliveObject(Widget))
		{
			UObjectManager::Get().DestroyObject(Widget);
		}
	}
	CreatedWidgets.clear();
}

bool UUIManager::LoadDocument(UUserWidget* Widget)
{
	if (!IsValid(Widget))
	{
		return false;
	}
	if (Widget->IsDocumentLoaded())
	{
		return true;
	}
	if (!RmlContext)
	{
		return false;
	}

	const std::filesystem::path Path = ToProjectPath(Widget->GetDocumentPath());
	if (!std::filesystem::exists(Path))
	{
		UE_LOG("[RmlUi] Document not found: %s", Widget->GetDocumentPath().c_str());
		return false;
	}

	Rml::ElementDocument* Document = RmlContext->LoadDocument(ToRmlPath(Path));
	if (!Document)
	{
		UE_LOG("[RmlUi] Failed to load document: %s", Widget->GetDocumentPath().c_str());
		return false;
	}

	Document->Show();
	Widget->SetLayoutMode(ResolveLayoutMode(Widget->GetDocumentPath(), Document));
	Widget->MarkDocumentLoaded(Document);
	Widget->RegisterEventListeners();
	return true;
}

void UUIManager::CloseDocument(UUserWidget* Widget)
{
	if (!IsAliveObject(Widget) || !Widget->GetDocument())
	{
		return;
	}

	Widget->ClearEventListeners();
	Widget->ReleasePendingBindings();
	Widget->GetDocument()->Close();
	Widget->ClearDocument();
}

bool UUIManager::DispatchTaggedActorClick(const FString& TargetTag, const FString& FunctionName)
{
	if (TargetTag.empty() || FunctionName.empty())
	{
		return false;
	}

	if (!DispatchWorld)
	{
		UE_LOG("[RmlUi] UI click '%s' skipped: no dispatch world for target tag '%s'.", FunctionName.c_str(), TargetTag.c_str());
		return false;
	}

	AActor* TargetActor = FGameplayStatics::FindFirstActorByTag(DispatchWorld, FName(TargetTag));
	if (!TargetActor)
	{
		UE_LOG("[RmlUi] UI click target tag not found: %s", TargetTag.c_str());
		return false;
	}

	if (!InvokeActorClickFunction(TargetActor, FunctionName))
	{
		UE_LOG(
			"[RmlUi] UI click function '%s' not found or failed on actor '%s' with tag '%s'.",
			FunctionName.c_str(),
			TargetActor->GetName().c_str(),
			TargetTag.c_str()
		);
		return false;
	}

	return true;
}

void UUIManager::SetViewportLayerVisibility(EUIRenderLayout TargetLayout)
{
	for (UUserWidget* Widget : ViewportWidgets)
	{
		if (!IsAliveObject(Widget) || !Widget->IsDocumentLoaded() || Widget->GetDocument() == nullptr)
		{
			continue;
		}

		if (Widget->GetLayoutMode() == TargetLayout)
		{
			Widget->GetDocument()->Show();
		}
		else
		{
			Widget->GetDocument()->Hide();
		}
	}
}

void UUIManager::RestoreViewportDocumentVisibility()
{
	for (UUserWidget* Widget : ViewportWidgets)
	{
		if (!IsAliveObject(Widget) || !Widget->IsDocumentLoaded() || Widget->GetDocument() == nullptr)
		{
			continue;
		}

		Widget->GetDocument()->Show();
	}
}

void UUIManager::ApplyHudDocumentRootScale(float ViewportHeight)
{
	if (ViewportHeight <= 0.0f)
	{
		return;
	}

	const float RootFontPx = HudRootFontPxAt1080p * (ViewportHeight / UIDesignHeight);
	const Rml::String RootFontValue = std::to_string(RootFontPx) + "px";

	for (UUserWidget* Widget : ViewportWidgets)
	{
		if (!IsAliveObject(Widget)
			|| Widget->GetLayoutMode() != EUIRenderLayout::ScreenHud
			|| !Widget->IsDocumentLoaded()
			|| Widget->GetDocument() == nullptr)
		{
			continue;
		}

		Widget->GetDocument()->SetProperty("font-size", RootFontValue);
	}
}

bool UUIManager::AnyScaledWidgetWantsMouse() const
{
	for (UUserWidget* Widget : ViewportWidgets)
	{
		if (!IsAliveObject(Widget) || !Widget->IsInViewport())
		{
			continue;
		}

		if (Widget->GetLayoutMode() == EUIRenderLayout::ScaledDesign && Widget->WantsMouse())
		{
			return true;
		}
	}

	return false;
}

bool UUIManager::IsMouseInsideScaledCanvas(float MouseX, float MouseY) const
{
	const float DesignX = (MouseX - UILayoutOffsetX) / UILayoutScale;
	const float DesignY = (MouseY - UILayoutOffsetY) / UILayoutScale;
	return DesignX >= 0.0f
		&& DesignX <= UIDesignWidth
		&& DesignY >= 0.0f
		&& DesignY <= UIDesignHeight;
}

void UUIManager::Render(const FPassContext& Ctx)
{
	CompactInvalidWidgets();
	if (!RmlContext || !RenderInterface || ViewportWidgets.empty() || Ctx.Frame.ViewportWidth <= 0.0f || Ctx.Frame.ViewportHeight <= 0.0f)
	{
		return;
	}

	UpdateUILayoutMetrics(
		Ctx.Frame.ViewportWidth,
		Ctx.Frame.ViewportHeight,
		UILayoutScale,
		UILayoutOffsetX,
		UILayoutOffsetY);

	if (AnyScaledWidgetWantsMouse())
	{
		RmlContext->SetDimensions({
			static_cast<int>(UIDesignWidth),
			static_cast<int>(UIDesignHeight)
		});
	}
	else
	{
		RmlContext->SetDimensions({
			static_cast<int>(Ctx.Frame.ViewportWidth),
			static_cast<int>(Ctx.Frame.ViewportHeight)
		});
	}

	UWorld* PreviousDispatchWorld = DispatchWorld;
	DispatchWorld = Ctx.World;
	ProcessInput(Ctx.Frame);
	DispatchWorld = PreviousDispatchWorld;

	FlushDeferredViewportRemovals();
	if (ViewportWidgets.empty())
	{
		return;
	}

	RenderInterface->BeginFrame(Ctx);

	SetViewportLayerVisibility(EUIRenderLayout::ScaledDesign);
	RmlContext->SetDimensions({
		static_cast<int>(UIDesignWidth),
		static_cast<int>(UIDesignHeight)
	});
	RmlContext->Update();
	const Rml::Matrix4f ScaledTransform = BuildUILayoutTransform(UILayoutScale, UILayoutOffsetX, UILayoutOffsetY);
	RenderInterface->SetTransform(&ScaledTransform);
	RmlContext->Render();
	RenderInterface->SetTransform(nullptr);

	SetViewportLayerVisibility(EUIRenderLayout::ScreenHud);
	ApplyHudDocumentRootScale(Ctx.Frame.ViewportHeight);
	RmlContext->SetDimensions({
		static_cast<int>(Ctx.Frame.ViewportWidth),
		static_cast<int>(Ctx.Frame.ViewportHeight)
	});
	RmlContext->Update();
	RmlContext->Render();

	RestoreViewportDocumentVisibility();
	RmlContext->Update();
	RenderInterface->EndFrame();
}

bool UUIManager::HasRuntimeOverlays(const FFrameContext& Frame) const
{
	if (Frame.WorldType != EWorldType::Game)
	{
		return false;
	}

	return FCrosshairOverlay::IsVisible() || FPhotoOverlay::IsFlashVisible();
}

void UUIManager::RenderRuntimeOverlays(const FPassContext& Ctx)
{
	if (!RenderInterface || !HasRuntimeOverlays(Ctx.Frame) ||
		Ctx.Frame.ViewportWidth <= 0.0f || Ctx.Frame.ViewportHeight <= 0.0f)
	{
		return;
	}

	TArray<Rml::Vertex> Vertices;
	TArray<int> Indices;

	if (FCrosshairOverlay::IsVisible())
	{
		AppendOverlayCircle(
			Vertices,
			Indices,
			Ctx.Frame.ViewportWidth * 0.5f,
			Ctx.Frame.ViewportHeight * 0.5f,
			CrosshairRadius,
			150, 150, 150, 255);
	}

	if (FPhotoOverlay::IsFlashVisible())
	{
		const float FlashAlpha = 1.0f - Clamp01(FPhotoOverlay::GetFlashTime() / PhotoFlashSeconds);
		AppendOverlayRect(
			Vertices,
			Indices,
			0.0f,
			0.0f,
			Ctx.Frame.ViewportWidth,
			Ctx.Frame.ViewportHeight,
			255, 255, 255, AlphaByte(FlashAlpha * 0.9f));
	}

	RenderInterface->BeginFrame(Ctx);
	RenderImmediateOverlayGeometry(RenderInterface, Vertices, Indices);
	RenderInterface->EndFrame();
}

void UUIManager::ProcessInput(const FFrameContext& Frame)
{
	if (!RmlContext)
	{
		return;
	}

	InputSystem& Input = InputSystem::Get();
	const int KeyModifierState = 0;

	int MouseX = 0;
	int MouseY = 0;
	if (Frame.CursorViewportX != UINT32_MAX && Frame.CursorViewportY != UINT32_MAX)
	{
		MouseX = static_cast<int>(Frame.CursorViewportX);
		MouseY = static_cast<int>(Frame.CursorViewportY);
	}
	else
	{
		const POINT MousePos = Input.GetMouseClientPos();
		MouseX = MousePos.x;
		MouseY = MousePos.y;
	}

	const bool bRouteScaledInput = AnyScaledWidgetWantsMouse()
		&& IsMouseInsideScaledCanvas(static_cast<float>(MouseX), static_cast<float>(MouseY));
	if (bRouteScaledInput)
	{
		MouseX = MapViewportMouseToDesign(static_cast<float>(MouseX), UILayoutOffsetX, UILayoutScale);
		MouseY = MapViewportMouseToDesign(static_cast<float>(MouseY), UILayoutOffsetY, UILayoutScale);
	}

	const bool bPauseMenuArmGateActive = bPauseMenuAwaitingArmClick
		&& IsValid(PauseMenuArmWidget)
		&& std::find(ViewportWidgets.begin(), ViewportWidgets.end(), PauseMenuArmWidget) != ViewportWidgets.end();

	bool bBlockPauseMenuRmlMouse = false;
	if (bPauseMenuArmGateActive)
	{
		PauseMenuArmWidget->ClearAllNavigationHighlightStates();
		PauseMenuArmWidget->SetGamepadNavigationHighlightEnabled(false);
		RmlContext->ProcessMouseLeave();
		bBlockPauseMenuRmlMouse = true;

		if (bPauseMenuIgnoreMouseUntilRelease)
		{
			if (Input.GetKeyUp(VK_LBUTTON))
			{
				bPauseMenuIgnoreMouseUntilRelease = false;
			}
		}
		else
		{
			if (Input.GetKeyDown(VK_LBUTTON))
			{
				bPauseMenuArmClickInProgress = true;
			}

			if (bPauseMenuArmClickInProgress && Input.GetKeyUp(VK_LBUTTON))
			{
				bPauseMenuAwaitingArmClick = false;
				bPauseMenuArmClickInProgress = false;
				PauseMenuArmWidget = nullptr;
			}
		}
	}

	bDispatchingRmlEvents = true;
	if (!bBlockPauseMenuRmlMouse)
	{
		RmlContext->ProcessMouseMove(MouseX, MouseY, KeyModifierState);
	}
	if (!bBlockPauseMenuRmlMouse)
	{
		if (Input.GetKeyDown(VK_LBUTTON))
		{
			RmlContext->ProcessMouseButtonDown(0, KeyModifierState);
		}
		if (Input.GetKeyUp(VK_LBUTTON))
		{
			RmlContext->ProcessMouseButtonUp(0, KeyModifierState);
		}
		if (Input.GetKeyDown(VK_RBUTTON))
		{
			RmlContext->ProcessMouseButtonDown(1, KeyModifierState);
		}
		if (Input.GetKeyUp(VK_RBUTTON))
		{
			RmlContext->ProcessMouseButtonUp(1, KeyModifierState);
		}
		if (Input.GetKeyDown(VK_MBUTTON))
		{
			RmlContext->ProcessMouseButtonDown(2, KeyModifierState);
		}
		if (Input.GetKeyUp(VK_MBUTTON))
		{
			RmlContext->ProcessMouseButtonUp(2, KeyModifierState);
		}
		const float WheelDelta = Input.GetScrollNotches();
		if (WheelDelta != 0.0f)
		{
			RmlContext->ProcessMouseWheel(WheelDelta, KeyModifierState);
		}
	}
	ProcessNavigationInput();
	bDispatchingRmlEvents = false;
}

void UUIManager::ProcessNavigationInput()
{
	UUserWidget* Widget = GetTopNavigationWidget();
	if (!IsValid(Widget))
	{
		HeldNavigationX = 0;
		HeldNavigationY = 0;
		NextNavigationRepeatTime = 0.0;
		return;
	}

	InputSystem& Input = InputSystem::Get();
	const FInputDeviceSnapshot& Gamepad = Input.GetGamepadSnapshot();
	const bool bUseGamepadNavigation =
		Input.GetPrimaryInputDevice() == EInputDeviceClass::Gamepad &&
		Gamepad.Info.bConnected;

	Widget->SetGamepadNavigationHighlightEnabled(bUseGamepadNavigation);
	if (!bUseGamepadNavigation)
	{
		Widget->ClearNavigationSelection();
		HeldNavigationX = 0;
		HeldNavigationY = 0;
		NextNavigationRepeatTime = 0.0;
	}

	if (Input.GetKeyDown(VK_RETURN) ||
		Input.GetKeyDown(VK_SPACE) ||
		(bUseGamepadNavigation && WasGamepadButtonPressed(Gamepad, EGamepadButton::FaceDown)))
	{
		Widget->ActivateNavigationSelection();
	}

	if (Input.GetKeyDown(VK_ESCAPE) ||
		Input.GetKeyDown(VK_BACK) ||
		WasGamepadButtonPressed(Gamepad, EGamepadButton::FaceRight) ||
		WasGamepadButtonPressed(Gamepad, EGamepadButton::Back))
	{
		Widget->ActivateCloseNavigationTarget();
	}

	if (!bUseGamepadNavigation)
	{
		return;
	}

	int32 DirectionX = 0;
	int32 DirectionY = 0;
	if (WasGamepadButtonPressed(Gamepad, EGamepadButton::DPadLeft))
	{
		DirectionX = -1;
	}
	else if (WasGamepadButtonPressed(Gamepad, EGamepadButton::DPadRight))
	{
		DirectionX = 1;
	}

	if (WasGamepadButtonPressed(Gamepad, EGamepadButton::DPadUp))
	{
		DirectionY = -1;
	}
	else if (WasGamepadButtonPressed(Gamepad, EGamepadButton::DPadDown))
	{
		DirectionY = 1;
	}

	if (DirectionX != 0 || DirectionY != 0)
	{
		Widget->NavigateSelection(DirectionX, DirectionY);
		HeldNavigationX = DirectionX;
		HeldNavigationY = DirectionY;
		NextNavigationRepeatTime = (SystemInterface ? SystemInterface->GetElapsedTime() : 0.0) + NavigationInitialRepeatDelay;
		return;
	}

	int32 HeldX = 0;
	int32 HeldY = 0;
	if (IsGamepadButtonDown(Gamepad, EGamepadButton::DPadLeft))
	{
		HeldX = -1;
	}
	else if (IsGamepadButtonDown(Gamepad, EGamepadButton::DPadRight))
	{
		HeldX = 1;
	}
	else
	{
		HeldX = AxisToNavigationDirection(GetGamepadAxisValue(Gamepad, EGamepadAxis::LeftStickX));
	}

	if (IsGamepadButtonDown(Gamepad, EGamepadButton::DPadUp))
	{
		HeldY = -1;
	}
	else if (IsGamepadButtonDown(Gamepad, EGamepadButton::DPadDown))
	{
		HeldY = 1;
	}
	else
	{
		HeldY = AxisToNavigationDirection(GetGamepadAxisValue(Gamepad, EGamepadAxis::LeftStickY));
	}

	if (HeldX == 0 && HeldY == 0)
	{
		HeldNavigationX = 0;
		HeldNavigationY = 0;
		NextNavigationRepeatTime = 0.0;
		return;
	}

	const double CurrentTime = SystemInterface ? SystemInterface->GetElapsedTime() : 0.0;
	const bool bNewHeldDirection = HeldX != HeldNavigationX || HeldY != HeldNavigationY;
	if (bNewHeldDirection)
	{
		Widget->NavigateSelection(HeldX, HeldY);
		HeldNavigationX = HeldX;
		HeldNavigationY = HeldY;
		NextNavigationRepeatTime = CurrentTime + NavigationInitialRepeatDelay;
		return;
	}

	if (CurrentTime >= NextNavigationRepeatTime)
	{
		Widget->NavigateSelection(HeldX, HeldY);
		NextNavigationRepeatTime = CurrentTime + NavigationRepeatDelay;
	}
}

UUserWidget* UUIManager::GetTopNavigationWidget() const
{
	for (auto It = ViewportWidgets.rbegin(); It != ViewportWidgets.rend(); ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || !Widget->IsInViewport() || !Widget->IsDocumentLoaded())
		{
			continue;
		}
		if (std::find(PendingRemoveWidgets.begin(), PendingRemoveWidgets.end(), Widget) != PendingRemoveWidgets.end())
		{
			continue;
		}
		if (Widget->WantsKeyboard())
		{
			return Widget;
		}
	}
	return nullptr;
}

void UUIManager::FlushDeferredViewportRemovals()
{
	if (PendingRemoveWidgets.empty())
	{
		return;
	}

	TArray<UUserWidget*> WidgetsToRemove = PendingRemoveWidgets;
	PendingRemoveWidgets.clear();

	for (UUserWidget* Widget : WidgetsToRemove)
	{
		RemoveFromViewportImmediate(Widget);
	}
}
