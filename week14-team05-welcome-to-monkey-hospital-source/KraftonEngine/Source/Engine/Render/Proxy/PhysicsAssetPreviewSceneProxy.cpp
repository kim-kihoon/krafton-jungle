#include "Render/Proxy/PhysicsAssetPreviewSceneProxy.h"

#include "Component/Debug/PhysicsAssetPreviewComponent.h"
#include "Materials/Material.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Render/Shader/ShaderManager.h"

namespace
{
	struct FPhysicsPreviewMaterialConstants
	{
		FVector4 DiffuseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	};
}

FPhysicsAssetPreviewSceneProxy::FPhysicsAssetPreviewSceneProxy(UPhysicsAssetPreviewComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags = EPrimitiveProxyFlags::EditorOnly | EPrimitiveProxyFlags::NeverCull;
	bCastShadow = false;
	bCastShadowAsTwoSided = false;

	PreviewMaterial = UMaterial::CreateTransient(
		ERenderPass::Transparent,
		EBlendState::AlphaBlend,
		EDepthStencilState::NoDepth,
		ERasterizerState::SolidNoCull,
		FShaderManager::Get().GetOrCreate(EShaderPath::Primitive));

	FPhysicsPreviewMaterialConstants& Constants =
		PreviewMaterial->BindPerShaderCB<FPhysicsPreviewMaterialConstants>(
			&PreviewMaterialCB,
			ECBSlot::PerShader0);
	Constants.DiffuseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
}

FPhysicsAssetPreviewSceneProxy::~FPhysicsAssetPreviewSceneProxy()
{
	PreviewMaterialCB.Release();
	if (PreviewMaterial)
	{
		UObjectManager::Get().DestroyObject(PreviewMaterial);
		PreviewMaterial = nullptr;
	}
}

void FPhysicsAssetPreviewSceneProxy::AddReferencedObjects(FReferenceCollector& Collector)
{
	FPrimitiveSceneProxy::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(PreviewMaterial);
}

void FPhysicsAssetPreviewSceneProxy::UpdateMesh()
{
	UPrimitiveComponent* OwnerComponent = GetOwner();
	if (!OwnerComponent)
	{
		MeshBuffer = nullptr;
		SectionDraws.clear();
		bVisible = false;
		return;
	}

	MeshBuffer = OwnerComponent->GetMeshBuffer();
	SectionDraws.clear();
	if (MeshBuffer && MeshBuffer->IsValid() && PreviewMaterial)
	{
		const uint32 IndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
		SectionDraws.push_back({ PreviewMaterial, 0, IndexCount });
	}
}
