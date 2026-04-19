#pragma once
#include "Component/PrimitiveComponent.h"
#include <memory>

struct FDynamicMesh;

/*
CameraActor의 방향을 시각적으로 나타내는 화살표 컴포넌트
PrimitiveGizmo의 Transition Axis Mesh를 사용해 기즈모와 동일한 스타일의 화살표 렌더
*/
class ENGINE_API UCameraArrowComponent : public UPrimitiveComponent
{
public:
	DECLARE_RTTI(UCameraArrowComponent, UPrimitiveComponent)
	GENERATE_SHALLOW_CLONE(UCameraArrowComponent);

	void PostConstruct() override;

	void SetArrowMesh(std::shared_ptr<FDynamicMesh> InMesh) { ArrowMesh = InMesh; }
	FDynamicMesh* GetArrowMesh() const { return ArrowMesh.get(); }

	FRenderMesh* GetRenderMesh() const override;
	FBoxSphereBounds GetWorldBounds() const override;

private:
	std::shared_ptr<FDynamicMesh> ArrowMesh;
};
