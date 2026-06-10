#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"

class UBillboardComponent;

// ============================================================
// FBillboardSceneProxy — UBillboardComponent 전용 프록시
// ============================================================
// Quad 메시 + Primitive 셰이더 캐싱.
// bPerViewportUpdate = true (카메라 방향으로 빌보드 회전 필요).
class FBillboardSceneProxy : public FPrimitiveSceneProxy
{
public:
	FBillboardSceneProxy(UBillboardComponent* InComponent);

	void UpdateTransform() override;
	void UpdateMesh() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

	// 에디터 아이콘 빌보드는 포스트프로세스 이후 전용 오버레이 패스에서 그린다(깊이/포그 분리).
	// 머티리얼은 Transparent(AlphaBlend) 그대로지만, 이 패스로 라우팅되어 NoDepth 오버레이로 합성됨.
	ERenderPass GetRenderPass() const override;

protected:
	UBillboardComponent* GetBillboardComponent() const;

	FVector CachedScale;
	FVector CachedLocation;
};
