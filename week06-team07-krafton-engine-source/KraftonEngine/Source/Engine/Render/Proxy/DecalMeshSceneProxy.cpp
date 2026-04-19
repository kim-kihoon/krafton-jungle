#include "DecalMeshSceneProxy.h"
#include "Component/MeshDecalComponent.h"
#include "Materials/Material.h"
#include "Texture/Texture2D.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Resource/ConstantBufferPool.h"

FDecalMeshSceneProxy::FDecalMeshSceneProxy(UMeshDecalComponent* InComponent) : FPrimitiveSceneProxy(InComponent)
{
	bSupportsOutline = false;

	FMeshSectionDraw DecalSection;
	DecalSection.DiffuseSRV = GetDecalComponent()->GetSRV();
	DecalSection.DiffuseColor = { 1.f, 1.f, 1.f, 1.f };
	DecalSection.FirstIndex = 0;
	DecalSection.IndexCount = 0;
	DecalSection.bIsUVScroll = false;
	SectionDraws.push_back(DecalSection);

	auto& CB = ExtraCB.Bind<FMeshDecalConstants>(
		FConstantBufferPool::Get().GetBuffer(ECBSlot::MeshDecal, sizeof(FMeshDecalConstants)),
		ECBSlot::MeshDecal);
	CB.Opacity = Cast<UMeshDecalComponent>(Owner)->GetOpacity();
	CB.bFade = Cast<UMeshDecalComponent>(Owner)->IsFading();
}



UMeshDecalComponent* FDecalMeshSceneProxy::GetDecalComponent() const
{
	return static_cast<UMeshDecalComponent*>(Owner);
}

void FDecalMeshSceneProxy::UpdateMaterial()
{
	if (SectionDraws.empty())
	{
		return;
	}

	SectionDraws[0].DiffuseSRV = GetDecalComponent()->GetSRV();
	UpdateSortKey();
}

void FDecalMeshSceneProxy::UpdateMesh()
{
	MeshBuffer = Owner->GetMeshBuffer();
	Shader = FShaderManager::Get().GetShader(EShaderType::MeshDecal);
	Pass = ERenderPass::MeshDecal;

	if (!SectionDraws.empty() && MeshBuffer)
	{
		SectionDraws[0].FirstIndex = 0;
		SectionDraws[0].IndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
	}

	UpdateMaterial();
	UpdateSortKey();
}

void FDecalMeshSceneProxy::UpdateVisibility()
{
	FPrimitiveSceneProxy::UpdateVisibility();
}

void FDecalMeshSceneProxy::UpdateTransform()
{
	UpdateMesh();
	FPrimitiveSceneProxy::UpdateTransform();
}

void FDecalMeshSceneProxy::UpdateOpacity()
{
	ExtraCB.As<FMeshDecalConstants>().Opacity =
		Cast<UMeshDecalComponent>(Owner)->GetOpacity();
}

void FDecalMeshSceneProxy::UpdateFade()
{
	ExtraCB.As<FMeshDecalConstants>().bFade = Cast<UMeshDecalComponent>(Owner)->IsFading();
}
