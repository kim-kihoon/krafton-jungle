#include "Render/Proxy/ShapeSceneProxy.h"

#include "Component/ShapeComponent.h"
#include "Object/Object.h"

FShapeSceneProxy::FShapeSceneProxy(UShapeComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags = EPrimitiveProxyFlags::EditorOnly | EPrimitiveProxyFlags::NeverCull;

	bDrawOnlyIfSelected = InComponent->IsDrawOnlyIfSelected();

	bCastShadow = false;
	bCastShadowAsTwoSided = false;
}

void FShapeSceneProxy::UpdateVisibility()
{
	FPrimitiveSceneProxy::UpdateVisibility();

	if (bVisible && bDrawOnlyIfSelected)
	{
		bVisible = IsSelected();
	}
}
