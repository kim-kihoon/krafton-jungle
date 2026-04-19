#include "DecalSceneProxy.h"
#include "Component/DecalComponent.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Pipeline/RenderBus.h"
#include "Render/Pipeline/RenderConstants.h"
#include "Render/Resource/ConstantBufferPool.h"

FDecalSceneProxy::FDecalSceneProxy(UDecalComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	bPerViewportUpdate = true;
	bSupportsOutline = false;

	FMeshSectionDraw DecalSection;

	DecalSection.DiffuseSRV = GetDecalComponent()->GetSRV();
	DecalSection.DiffuseColor = { 1.f, 1.f, 1.f, 1.f }; // 텍스처 그대로
	DecalSection.FirstIndex = 0;               
	DecalSection.IndexCount = 36;               
	DecalSection.bIsUVScroll = false;

	SectionDraws.push_back(DecalSection);
}

UDecalComponent* FDecalSceneProxy::GetDecalComponent() const
{
	return static_cast<UDecalComponent*>(Owner);
}

void FDecalSceneProxy::UpdateMaterial()
{
	SectionDraws[0].DiffuseSRV = GetDecalComponent()->GetSRV();
}

void FDecalSceneProxy::UpdateMesh()
{
	MeshBuffer = Owner->GetMeshBuffer();
	Shader = FShaderManager::Get().GetShader(EShaderType::Decal);
	Pass = ERenderPass::Decal;
	UpdateSortKey();
}

void FDecalSceneProxy::UpdatePerViewport(const FRenderBus& Bus)
{
	// 뷰별 갱신 시점에 각 데칼의 고유한 데이터를 바인딩합니다.
	// FExtraConstantBuffer::Bind 내부에서 개별 드로우마다 Update가 호출되도록 보장합니다.
	auto& G = ExtraCB.Bind<FDecalConstants>(
		FConstantBufferPool::Get().GetBuffer(ECBSlot::Decal, sizeof(FDecalConstants)),
		ECBSlot::Decal);

	G.WorldToDecal = PerObjectConstants.Model.GetInverse();
	
	// 소유 컴포넌트로부터 이 프록시만의 페이드 값을 가져와 G에 채워넣습니다.
	GetDecalComponent()->SetFadeConstants(G);
}

