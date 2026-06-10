#include "Render/Proxy/DecalSceneProxy.h"

#include "Component/Primitive/DecalComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Render/Shader/ShaderManager.h"

#include "Materials/Material.h"
#include "Materials/MaterialDomain.h"
#include "Texture/Texture2D.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include <algorithm>

namespace
{
	struct FDecalConstants
	{
		FMatrix WorldToDecal;
		FVector4 Color;
	};

	FShader* ResolveDecalProjectionShader(const UMaterial* Material)
	{
		if (Material && Material->GetDomain() == EMaterialDomain::Decal && Material->GetShader())
		{
			const FString& ShaderPath = Material->GetShaderPathForSerialize();
			if (ShaderPath == EShaderPath::Decal || ShaderPath.find("_Decal.hlsl") != FString::npos)
			{
				return Material->GetShader();
			}
		}

		return FShaderManager::Get().GetOrCreate(EShaderPath::Decal);
	}
}

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
	Collector.AddReferencedObject(DecalMaterial, "DecalMaterial");
	Collector.AddReferencedObject(DecalProxyMaterial, "DecalProxyMaterial");
}

void FDecalSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	UpdateDecalConstants();
}

void FDecalSceneProxy::UpdateDecalConstants()
{
	UDecalComponent* DecalComp = GetDecalComponent();
	if (!IsValid(DecalComp) || !DecalProxyMaterial)
	{
		return;
	}

	// 데칼 박스 클리핑은 transform 기반 상수에 의존하므로 이동/회전/스케일 변경 시 즉시 갱신한다.
	auto& CB = DecalProxyMaterial->BindPerShaderCB<FDecalConstants>(DecalCB, ECBSlot::PerShader0);
	CB.WorldToDecal = DecalComp->GetWorldMatrix().GetInverse();
	CB.Color = DecalComp->GetColor();
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

	FShader* Shader = ResolveDecalProjectionShader(DecalMaterial);

	// 데칼 투영은 receiver 메시를 다시 그리므로 박스 discard가 있는 데칼 셰이더만 사용해야 한다.
	const EBlendMode DecalBlend = DecalMaterial ? DecalMaterial->GetBlendMode() : EBlendMode::Transparent;
	const FMaterialRenderState DecalState = ResolveMaterialRenderState(EMaterialDomain::Decal, DecalBlend);
	ERenderPass Pass = DecalState.Pass;
	EBlendState Blend = DecalState.Blend;
	EDepthStencilState Depth = DecalState.DepthStencil;
	ERasterizerState Raster = DecalState.Rasterizer;

	if (DecalProxyMaterial &&
		(DecalProxyMaterial->GetShader() != Shader ||
		 DecalProxyMaterial->GetRenderPass() != Pass ||
		 DecalProxyMaterial->GetBlendState() != Blend ||
		 DecalProxyMaterial->GetDepthStencilState() != Depth ||
		 DecalProxyMaterial->GetRasterizerState() != Raster))
	{
		UObjectManager::Get().DestroyObject(DecalProxyMaterial);
		DecalProxyMaterial = nullptr;
	}

	// 프록시 전용 transient Material 래퍼 생성 (공유 DecalMaterial에 직접 CB를 쓸 수 없음)
	if (!DecalProxyMaterial)
	{
		DecalProxyMaterial = UMaterial::CreateTransient(Pass, Blend, Depth, Raster, Shader);
	}

	// SRV 동기화 (DecalMaterial의 텍스처를 래퍼에 복사)
	if (DecalMaterial)
	{
		const ID3D11ShaderResourceView* const* SRVs = DecalMaterial->GetCachedSRVs();
		for (int s = 0; s < (int)EMaterialTextureSlot::Max; s++)
			DecalProxyMaterial->SetCachedSRV(static_cast<EMaterialTextureSlot>(s),
				const_cast<ID3D11ShaderResourceView*>(SRVs[s]));
	}

	// Per-shader CB (WorldToDecal, Color) 바인딩
	UpdateDecalConstants();

	// SectionDraws — 래퍼 Material 사용
	SectionDraws.clear();
	SectionDraws.push_back({ DecalProxyMaterial, 0, 0 });
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
