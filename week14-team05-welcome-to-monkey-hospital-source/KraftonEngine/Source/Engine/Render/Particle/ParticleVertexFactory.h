#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Particle/ParticleHelper.h"
#include "Render/Resource/Buffer.h"   // FVertexBuffer/FIndexBuffer (sprite unit quad 멤버)

struct ID3D11Device;
struct ID3D11DeviceContext;
class FShader;
class FReferenceCollector;
class FDynamicVertexBuffer;
class FDynamicIndexBuffer;

// =============================================================================
// FParticleVertexFactory
//   "EmitterReplayData → DX 정점/인덱스" 변환의 추상 인터페이스.
//   각 type (Sprite/Mesh/Beam/Ribbon) 별 서브클래스가 정점 layout 과
//   ConvertAndUpload 를 구현한다.
// =============================================================================
class FParticleVertexFactory
{
public:
	virtual ~FParticleVertexFactory() = default;

	virtual EDynamicEmitterType GetType() const = 0;

	// RHI 의존 리소스 초기화/해제 (Shader 등). lazy-init 패턴.
	// InputLayout은 FShader가 VS reflection으로 내부 관리 — factory는 별도 보유 안 함.
	virtual void InitResources(ID3D11Device* Device) = 0;
	virtual void ReleaseResources() = 0;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) {}

	virtual FShader* GetShader() const = 0;

	// EmitterReplayData 의 입자 buffer 를 읽어 dynamic VB 로 변환/업로드.
	// 반환: 이 draw 의 vertex/index 카운트.
	// CameraRight/CameraUp: 빌보드 expansion에 사용 (Mesh/Beam/Ribbon은 무시 가능).
	// CameraPosition: 같은 proxy 내 입자 간 back-to-front sort에 사용 (Mesh particle).
	struct FDrawSpec
	{
		uint32 VertexCount = 0;
		uint32 IndexCount  = 0;
		uint32 VertexByteOffset = 0;
		uint32 IndexByteOffset  = 0;

		// 인스턴싱 경로 (Mesh particle 등) — InstanceCount > 0이면 정적 mesh + per-instance.
		// InOutVB는 instance stream을, StaticVB/IB는 정적 mesh 외부 자원을 가리킴.
		ID3D11Buffer* StaticVB       = nullptr;
		uint32        StaticVBStride = 0;
		ID3D11Buffer* StaticIB       = nullptr;
		uint32        InstanceCount  = 0;

		// Transparent 섹션 depth 정렬용 대표 월드 위치 (입자 평균 / Beam 중점). SceneProxy가 Section에 복사.
		FVector       SortWorldPos = { 0, 0, 0 };
	};
	// bRequiresSort: caller(SceneProxy)가 Material.BlendState 등으로 결정.
	// AlphaBlend면 true, Opaque/Additive/Modulate면 false. 입자 간 카메라 거리 정렬을 skip 가능.
	// InOutVB/InOutIB: caller(SceneProxy)가 emitter 인스턴스별로 소유하는 동적 버퍼.
	//   Sprite/Mesh: InOutVB만 per-instance 스트림으로 사용, InOutIB는 미사용(정적 quad/mesh IB).
	//   Beam/Ribbon: InOutVB=strip 정점, InOutIB=strip 인덱스 둘 다 사용.
	virtual bool BuildDraw(ID3D11Device* Device, ID3D11DeviceContext* Context,
	                       const FDynamicEmitterReplayDataBase& Replay,
	                       const FVector& CameraRight, const FVector& CameraUp,
	                       const FVector& CameraPosition,
	                       bool bRequiresSort,
	                       EParticleReplaySortMode SortMode,
	                       FDynamicVertexBuffer& InOutVB,
	                       FDynamicIndexBuffer& InOutIB,
	                       FDrawSpec& OutDraw) = 0;
};

// -----------------------------------------------------------------------------
// FParticleSpriteVertexFactory
//   per-instance FParticleSpriteInstanceVertex(center/velocity/size/rotation/...) 와
//   정적 unit quad(slot 0)로 GPU 인스턴싱(DrawIndexedInstanced(6, N))한다.
//   ScreenAlignment/Rotation/SubUV는 Sprite.hlsl VS/PS가 적용. Sort는 instance 적재 순서로.
// -----------------------------------------------------------------------------
class FParticleSpriteVertexFactory : public FParticleVertexFactory
{
public:
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Sprite; }
	FShader* GetShader() const override { return Shader; }

	bool BuildDraw(ID3D11Device* Device, ID3D11DeviceContext* Context,
	               const FDynamicEmitterReplayDataBase& Replay,
	               const FVector& CameraRight, const FVector& CameraUp,
	               const FVector& CameraPosition,
	               bool bRequiresSort,
	               EParticleReplaySortMode SortMode,
	               FDynamicVertexBuffer& InOutVB,
	               FDynamicIndexBuffer& InOutIB,
	               FDrawSpec& OutDraw) override;

	void InitResources(ID3D11Device* Device) override;
	void ReleaseResources() override;

protected:
	FShader* Shader = nullptr;
	// 정적 unit quad (slot 0) — 모든 sprite 입자가 공유. InitResources에서 1회 생성.
	FVertexBuffer QuadVB;
	FIndexBuffer  QuadIB;
};

// -----------------------------------------------------------------------------
// FParticleMeshVertexFactory
//   StaticMesh 의 vertex/index 를 그대로 쓰고, per-instance buffer 에
//   transform/color/sub-image 를 흘려넣는다 (instanced draw).
// -----------------------------------------------------------------------------
class UStaticMesh;

class FParticleMeshVertexFactory : public FParticleVertexFactory
{
public:
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Mesh; }
	FShader* GetShader() const override { return Shader; }

	bool BuildDraw(ID3D11Device* Device, ID3D11DeviceContext* Context,
	               const FDynamicEmitterReplayDataBase& Replay,
	               const FVector& CameraRight, const FVector& CameraUp,
	               const FVector& CameraPosition,
	               bool bRequiresSort,
	               EParticleReplaySortMode SortMode,
	               FDynamicVertexBuffer& InOutVB,
	               FDynamicIndexBuffer& InOutIB,
	               FDrawSpec& OutDraw) override;

	void InitResources(ID3D11Device* Device) override;
	void ReleaseResources() override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

protected:
	FShader*     Shader             = nullptr;
	// Replay.Mesh == nullptr 일 때 fallback. InitResources에서 한 번 로드 후 재사용.
	// (정점 포맷이 FVertexPNCT라 Mesh.hlsl과 매칭 — 엔진 빌트인 Cube primitive와 달리)
	UStaticMesh* CachedCubeFallback = nullptr;
};

// -----------------------------------------------------------------------------
// FParticleBeamVertexFactory / FParticleRibbonVertexFactory
//   Beam: source→target 사이 tessellation. Ribbon: 시간순 sample 자취.
//   각자 본인의 dynamic vertex layout 을 가진다.
// -----------------------------------------------------------------------------
class FParticleBeamVertexFactory : public FParticleVertexFactory
{
public:
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Beam; }
	FShader* GetShader() const override { return Shader; }

	bool BuildDraw(ID3D11Device* Device, ID3D11DeviceContext* Context,
	               const FDynamicEmitterReplayDataBase& Replay,
	               const FVector& CameraRight, const FVector& CameraUp,
	               const FVector& CameraPosition,
	               bool bRequiresSort,
	               EParticleReplaySortMode SortMode,
	               FDynamicVertexBuffer& InOutVB,
	               FDynamicIndexBuffer& InOutIB,
	               FDrawSpec& OutDraw) override;

	void InitResources(ID3D11Device* Device) override;
	void ReleaseResources() override;

protected:
	FShader* Shader = nullptr;
	// strip 인덱스는 더 이상 factory가 소유하지 않는다 — SceneProxy가 emitter별로 소유해
	// BuildDraw에 InOutIB로 넘긴다(같은 type 여러 emitter가 IB를 덮어쓰는 문제 방지).
};

class FParticleRibbonVertexFactory : public FParticleVertexFactory
{
public:
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Ribbon; }
	FShader* GetShader() const override { return Shader; }

	bool BuildDraw(ID3D11Device* Device, ID3D11DeviceContext* Context,
	               const FDynamicEmitterReplayDataBase& Replay,
	               const FVector& CameraRight, const FVector& CameraUp,
	               const FVector& CameraPosition,
	               bool bRequiresSort,
	               EParticleReplaySortMode SortMode,
	               FDynamicVertexBuffer& InOutVB,
	               FDynamicIndexBuffer& InOutIB,
	               FDrawSpec& OutDraw) override;

	void InitResources(ID3D11Device* Device) override;
	void ReleaseResources() override;

protected:
	FShader* Shader = nullptr;
	// strip 인덱스는 더 이상 factory가 소유하지 않는다 — SceneProxy가 emitter별로 소유해
	// BuildDraw에 InOutIB로 넘긴다(같은 type 여러 emitter가 IB를 덮어쓰는 문제 방지).
};
