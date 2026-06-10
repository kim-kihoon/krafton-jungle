#include "Render/Proxy/PhotoPolaroidSceneProxy.h"

#include "Component/Primitive/PhotoPolaroidComponent.h"
#include "GameFramework/AActor.h"
#include "Materials/Material.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Render/Shader/ShaderManager.h"
#include "UI/PhotoOverlay.h"

#include <algorithm>

namespace
{
	constexpr float FrameAspect = 1672.0f / 941.0f;
	constexpr float PhotoForwardOffset = -0.004f;

	struct FPhotoDevelopConstants
	{
		float DevelopAlpha = 0.0f;
		float Padding[3] = {};
	};

	float Clamp01(float Value)
	{
		return (std::max)(0.0f, (std::min)(1.0f, Value));
	}

	void AddQuad(
		TArray<FVertexPNCT>& Vertices,
		TArray<uint32>& Indices,
		float X,
		float Left,
		float Top,
		float Right,
		float Bottom,
		const FVector4& Color)
	{
		const uint32 Base = static_cast<uint32>(Vertices.size());
		const FVector Normal(1.0f, 0.0f, 0.0f);
		Vertices.push_back({ FVector(X, Left, Top), Normal, Color, FVector2(0.0f, 0.0f) });
		Vertices.push_back({ FVector(X, Right, Top), Normal, Color, FVector2(1.0f, 0.0f) });
		Vertices.push_back({ FVector(X, Right, Bottom), Normal, Color, FVector2(1.0f, 1.0f) });
		Vertices.push_back({ FVector(X, Left, Bottom), Normal, Color, FVector2(0.0f, 1.0f) });
		Indices.insert(Indices.end(), { Base + 0, Base + 1, Base + 2, Base + 0, Base + 2, Base + 3 });
	}
}

FPhotoPolaroidSceneProxy::FPhotoPolaroidSceneProxy(UPhotoPolaroidComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::NeverCull;
	ProxyFlags &= ~EPrimitiveProxyFlags::SupportsOutline;
	ProxyFlags &= ~EPrimitiveProxyFlags::ShowAABB;
}

FPhotoPolaroidSceneProxy::~FPhotoPolaroidSceneProxy()
{
	PhotoDevelopCB.Release();
	MeshBufferStorage.Release();
	if (FrameMaterial)
	{
		UObjectManager::Get().DestroyObject(FrameMaterial);
		FrameMaterial = nullptr;
	}
	if (PhotoMaterial)
	{
		UObjectManager::Get().DestroyObject(PhotoMaterial);
		PhotoMaterial = nullptr;
	}
}

void FPhotoPolaroidSceneProxy::AddReferencedObjects(FReferenceCollector& Collector)
{
	FPrimitiveSceneProxy::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(FrameMaterial);
	Collector.AddReferencedObject(PhotoMaterial);
}

UPhotoPolaroidComponent* FPhotoPolaroidSceneProxy::GetPhotoComponent() const
{
	return static_cast<UPhotoPolaroidComponent*>(GetOwner());
}

void FPhotoPolaroidSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
}

void FPhotoPolaroidSceneProxy::UpdateMaterial()
{
	UPhotoPolaroidComponent* Comp = GetPhotoComponent();
	if (!Comp)
	{
		bVisible = false;
		return;
	}

	if (!FrameMaterial)
	{
		FrameMaterial = UMaterial::CreateTransient(
			ERenderPass::Transparent,
			EBlendState::AlphaBlend,
			EDepthStencilState::DepthReadOnly,
			ERasterizerState::SolidNoCull,
			FShaderManager::Get().GetOrCreate(EShaderPath::Billboard));
	}
	if (!PhotoMaterial)
	{
		PhotoMaterial = UMaterial::CreateTransient(
			ERenderPass::Transparent,
			EBlendState::AlphaBlend,
			EDepthStencilState::DepthReadOnly,
			ERasterizerState::SolidNoCull,
			FShaderManager::Get().GetOrCreate(EShaderPath::PhotoDevelop));
	}

	FrameMaterial->SetCachedSRV(EMaterialTextureSlot::Diffuse, Comp->GetFrameSRV());
	PhotoMaterial->SetCachedSRV(EMaterialTextureSlot::Diffuse, Comp->GetPhotoSRV());
	FPhotoDevelopConstants& DevelopConstants = PhotoMaterial->BindPerShaderCB<FPhotoDevelopConstants>(&PhotoDevelopCB, ECBSlot::PerShader0);
	DevelopConstants.DevelopAlpha = Clamp01(Comp->GetDevelopTime() / FPhotoOverlay::GetDevelopSeconds());

	SectionDraws.clear();
	if (FrameMaterial && PhotoMaterial)
	{
		FMeshSectionDraw FrameSection;
		FrameSection.Material = FrameMaterial;
		FrameSection.FirstIndex = 0;
		FrameSection.IndexCount = 6;
		FrameSection.PassOverride = ERenderPass::Transparent;
		FrameSection.SortPriority = 0;
		SectionDraws.push_back(FrameSection);

		FMeshSectionDraw PhotoSection;
		PhotoSection.Material = PhotoMaterial;
		PhotoSection.FirstIndex = 6;
		PhotoSection.IndexCount = 6;
		PhotoSection.PassOverride = ERenderPass::Transparent;
		PhotoSection.SortPriority = 1;
		SectionDraws.push_back(PhotoSection);
	}
}

void FPhotoPolaroidSceneProxy::UpdateMesh()
{
	UPhotoPolaroidComponent* Comp = GetPhotoComponent();
	if (!Comp)
	{
		bVisible = false;
		return;
	}

	CachedDisplayTime = Comp->GetDisplayTime();
	CachedDevelopTime = Comp->GetDevelopTime();
	MeshBuffer = &MeshBufferStorage;
	UpdateMaterial();
	MeshBufferStorage.Release();
}

bool FPhotoPolaroidSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	(void)Context;
	RebuildMesh(Device);

	if (!MeshBufferStorage.IsValid())
	{
		return false;
	}

	OutBuffer = {};
	OutBuffer.VB = MeshBufferStorage.GetVertexBuffer().GetBuffer();
	OutBuffer.VBStride = MeshBufferStorage.GetVertexBuffer().GetStride();
	OutBuffer.IB = MeshBufferStorage.GetIndexBuffer().GetBuffer();
	return OutBuffer.VB && OutBuffer.IB;
}

void FPhotoPolaroidSceneProxy::RebuildMesh(ID3D11Device* Device) const
{
	if (!Device || MeshBufferStorage.IsValid())
	{
		return;
	}

	const float FrameHeight = 1.0f;
	const float FrameWidth = FrameHeight * FrameAspect;
	const float FullTop = FrameHeight * 0.5f;
	const float FullBottom = -FrameHeight * 0.5f;
	const float FullLeft = -FrameWidth * 0.5f;
	const float FullRight = FrameWidth * 0.5f;
	const float PhotoLeft = FullLeft + FrameWidth * (58.0f / 1672.0f);
	const float PhotoRight = FullLeft + FrameWidth * (1614.0f / 1672.0f);
	const float PhotoTop = FullTop - FrameHeight * (103.0f / 941.0f);
	const float PhotoBottom = FullTop - FrameHeight * (801.0f / 941.0f);

	TMeshData<FVertexPNCT> MeshData;
	AddQuad(MeshData.Vertices, MeshData.Indices, 0.0f, FullLeft, FullTop, FullRight, FullBottom, FVector4(1.0f, 1.0f, 1.0f, 1.0f));
	AddQuad(MeshData.Vertices, MeshData.Indices, PhotoForwardOffset, PhotoLeft, PhotoTop, PhotoRight, PhotoBottom, FVector4(1.0f, 1.0f, 1.0f, 1.0f));

	MeshBufferStorage.Create(Device, MeshData);
}
