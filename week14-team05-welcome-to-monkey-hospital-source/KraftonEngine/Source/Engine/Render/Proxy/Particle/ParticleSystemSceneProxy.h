#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"
#include "Particle/ParticleHelper.h"
#include "Component/Particle/ParticleSystemComponent.h"

#include <memory>

class FParticleVertexFactory;
class UMaterial;

// =============================================================================
// FParticleSystemSceneProxy
//   UParticleSystemComponent 의 렌더 측 mirror.
//   - 매 프레임 PSC 가 만든 FDynamicData (emitter snapshot 묶음) 를 받아 보관
//   - DrawCall 시 각 emitter type 의 VertexFactory 로 dispatch
//   - Dynamic VB 는 proxy 가 소유 (per-frame ring)
//   - Blend: Material(.mat)이 BlendState/RenderPass 등 렌더 상태의 single source of truth
// =============================================================================
class FParticleSystemSceneProxy : public FPrimitiveSceneProxy
{
public:
	FParticleSystemSceneProxy(UParticleSystemComponent* InComponent);
	~FParticleSystemSceneProxy() override;

	void UpdateTransform()  override;
	void UpdateMaterial()   override;
	void UpdateVisibility() override;
	void UpdateMesh()       override;
	void UpdatePerViewport(const struct FFrameContext& Frame) override;

	// PSC 가 매 프레임 tick 끝에 호출 — proxy 가 ownership 인수.
	void SetDynamicData(UParticleSystemComponent::FDynamicData* InData);
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	void AppendVectorFieldDebugLines(class FScene& Scene) const;

	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
	                       FDrawCommandBuffer& OutBuffer) const override;

protected:
	UParticleSystemComponent* GetPSC() const;

	// 매 프레임 교체되는 emitter snapshot. proxy 가 소유.
	UParticleSystemComponent::FDynamicData* DynamicData = nullptr;

	// EDynamicEmitterType → factory 매핑. lazy 생성 (사용된 타입만 채워짐).
	// PrepareDrawBuffer가 const라 mutable.
	mutable FParticleVertexFactory* Factories[(int)EDynamicEmitterType::Count] = {};

	// 타입 보고 factory 반환. 없으면 lazy 생성 + InitResources.
	FParticleVertexFactory* GetOrCreateFactory(EDynamicEmitterType Type, ID3D11Device* Device) const;

	// emitter 인스턴스별 dedicated dynamic buffer — replay 순서(EmitterIdx)로 인덱싱.
	// 같은 type emitter가 여러 개여도 각자 전용 VB/IB를 가져 서로 덮어쓰지 않는다.
	//   Sprite/Mesh : VB = per-instance 스트림(slot 1), IB는 미사용(정적 quad/mesh IB 사용)
	//   Beam/Ribbon : VB = strip 정점, IB = strip 인덱스 (둘 다 동적)
	// 슬롯은 lazy 생성되고 프레임 간 유지되며 EnsureCapacity로 성장. emitter index→type은 고정 가정.
	mutable TArray<std::unique_ptr<FDynamicVertexBuffer>> EmitterVBs;
	mutable TArray<std::unique_ptr<FDynamicIndexBuffer>>  EmitterIBs;

	// emitter type별 fallback Material (Template 없을 때 사용).
	// 자산 기반 (.mat 파일) — FMaterialManager::GetOrCreateMaterial로 로드.
	UMaterial* SpriteMaterial    = nullptr;  // fallback
	UMaterial* MeshMaterial      = nullptr;  // fallback
	UMaterial* BeamTrailMaterial = nullptr;  // fallback (Beam/Ribbon 공용)

	// PSC.Template.Emitters[i].LODLevels[0].RequiredModule.ResolveMaterial() 결과 캐시.
	// Template 있을 때 emitter index 1:1로 채워짐. 없는 index는 fallback 사용.
	TArray<UMaterial*> EmitterMaterials;

	// UpdatePerViewport에서 캐시한 카메라 벡터 (PrepareDrawBuffer에서 빌보드 expansion + instance sort 시 사용).
	FVector CachedCameraRight    = { 1, 0, 0 };
	FVector CachedCameraUp       = { 0, 1, 0 };
	FVector CachedCameraPosition = { 0, 0, 0 };

	// 가장 최근에 생성된 index 수 — SectionDraws[0].IndexCount 갱신용 (mutable: PrepareDrawBuffer에서 갱신).
	mutable uint32 LastIndexCount = 0;
};
