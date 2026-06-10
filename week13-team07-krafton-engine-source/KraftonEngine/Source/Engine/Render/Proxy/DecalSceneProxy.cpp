#include "Render/Proxy/DecalSceneProxy.h"

#include "Component/Primitive/DecalComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Render/Resource/Buffer.h"
#include "Render/Shader/ShaderManager.h"

#include "Materials/Material.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include <algorithm>

FDecalSceneProxy::FDecalSceneProxy(UDecalComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::Decal;
	ProxyFlags &= ~EPrimitiveProxyFlags::SupportsOutline;
	DecalCB = new FConstantBuffer();
	// 최초 1회 초기화
	UpdateMesh();
}

FDecalSceneProxy::~FDecalSceneProxy()
{
	InvalidateReceiverCache();

	if (DecalCB)
	{
		DecalCB->Release();
		delete DecalCB;
		DecalCB = nullptr;
	}
	if (DecalProxyMaterial)
	{
		UObjectManager::Get().DestroyObject(DecalProxyMaterial);
		DecalProxyMaterial = nullptr;
	}
}

UDecalComponent* FDecalSceneProxy::GetDecalComponent() const
{
	return Cast<UDecalComponent>(GetOwner());
}

void FDecalSceneProxy::AddReferencedObjects(FReferenceCollector& Collector)
{
	FPrimitiveSceneProxy::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(DecalMaterial);
	Collector.AddReferencedObject(DecalProxyMaterial);
}

void FDecalSceneProxy::RemoveReceiverProxy(FPrimitiveSceneProxy* ReceiverProxy)
{
	if (!ReceiverProxy)
	{
		return;
	}

	CachedReceiverProxies.erase(
		std::remove(CachedReceiverProxies.begin(), CachedReceiverProxies.end(), ReceiverProxy),
		CachedReceiverProxies.end());
}

void FDecalSceneProxy::InvalidateReceiverCache()
{
	CachedReceiverProxies.clear();
}

void FDecalSceneProxy::UpdateMaterial()
{
	UDecalComponent* DecalComp = GetDecalComponent();
	if (!IsValid(DecalComp)) return;

	DecalMaterial = DecalComp->GetMaterial();

	// 원본 Material이 없는 경우에만 fallback decal material을 사용한다.
	// 원본 Material을 직접 SectionDraws에 넣어야 graph material의 parameter CB/SRV가 유지된다.
	if (!DecalMaterial && !DecalProxyMaterial)
	{
		DecalProxyMaterial = UMaterial::CreateTransient(
			ERenderPass::Decal,
			EBlendState::AlphaBlend,
			EDepthStencilState::DepthReadOnly,
			ERasterizerState::SolidNoCull,
			FShaderManager::Get().GetOrCreate(EShaderPath::Decal));
	}

	DecalCBData.WorldToDecal = DecalComp->GetWorldMatrix().GetInverse();
	DecalCBData.Color = DecalComp->GetColor();

	UMaterial* DrawMaterial = DecalMaterial ? DecalMaterial : DecalProxyMaterial;

	SectionDraws.clear();
	if (DrawMaterial)
	{
		SectionDraws.push_back({ DrawMaterial, 0, 0 });
	}
}

void FDecalSceneProxy::UploadDecalConstantBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) const
{
	if (!DecalCB || !Device || !Context)
	{
		return;
	}

	if (!DecalCB->GetBuffer())
	{
		DecalCB->Create(Device, sizeof(FDecalConstants), "DecalConstants");
	}
	DecalCB->Update(Context, &DecalCBData, sizeof(FDecalConstants));
}

void FDecalSceneProxy::UpdateMesh()
{
	UpdateMaterial();
	RebuildReceiverProxies();

	MeshBuffer = nullptr;
	ProxyFlags &= ~EPrimitiveProxyFlags::SupportsOutline;
}

void FDecalSceneProxy::RebuildReceiverProxies()
{
	CachedReceiverProxies.clear();

	UDecalComponent* DecalComp = GetDecalComponent();
	if (!IsValid(DecalComp)) return;

	for (UStaticMeshComponent* Receiver : DecalComp->GetReceivers())
	{
		if (IsValid(Receiver))
		{
			FPrimitiveSceneProxy* ReceiverProxy = Receiver->GetSceneProxy();
			if (ReceiverProxy && ReceiverProxy->HasValidOwner())
				CachedReceiverProxies.push_back(ReceiverProxy);
		}
	}
}
