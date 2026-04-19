#include "Engine/Render/Proxy/ArrowSceneProxy.h"
#include "Engine/Component/ArrowComponent.h"
#include "Render/Resource/ShaderManager.h"

FArrowSceneProxy::FArrowSceneProxy(UArrowComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	bShowAABB = false;
	bShowOBB = false;
	bSupportsOutline = false;
	bNeverCull = true;
	bVisible = false;

	GetArrowComponent()->SetVisibility(false);
	Shader = FShaderManager::Get().GetShader(EShaderType::Gizmo);
}

UArrowComponent* FArrowSceneProxy::GetArrowComponent() const
{
	return static_cast<UArrowComponent*>(Owner);
}

void FArrowSceneProxy::UpdateMesh()
{
	MeshBuffer = Owner->GetMeshBuffer();
	Shader = FShaderManager::Get().GetShader(EShaderType::Primitive);
	Pass = ERenderPass::GizmoOuter;
	UpdateSortKey();
}
