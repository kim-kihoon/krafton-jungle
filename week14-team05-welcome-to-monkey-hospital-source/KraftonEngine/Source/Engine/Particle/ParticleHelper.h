#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Render/Types/VertexTypes.h"     // Windows.h 를 끌어오므로 RenderStateTypes 보다 먼저
#include "Render/Types/RenderStateTypes.h"

#include <utility>

// =============================================================================
// ParticleHelper.h
//   Particle 시스템 전역에서 공유되는 POD/구조체/상수 모음.
//   - FBaseParticle: 모든 입자가 공통으로 갖는 헤더 (alignment-친화 layout).
//   - Payload 매크로/엑세서: 모듈이 require 하는 추가 바이트를 BaseParticle 뒤에
//     이어 붙여 사용하기 위한 도우미.
//   - FDynamicEmitterData* / F*ReplayData*: GameThread → RenderThread 로
//     전달되는 immutable snapshot. ReplayData 는 직렬화/복기 (replay) 까지 지원.
//   - Constants: 정렬, 최대 입자 수, sprite/mesh draw 상수 등.
//   ※ 본 헤더는 UObject 를 직접 포함하지 않는다. (engine/render 양쪽에서 include)
// =============================================================================

class UMaterial;
class UStaticMesh;
class UParticleModule;
class FParticleEmitterInstance;
class FParticleSystemSceneProxy;
class FParticleVertexFactory;

// -----------------------------------------------------------------------------
// 상수
// -----------------------------------------------------------------------------
namespace ParticleConstants
{
	// 모든 ParticleData 할당의 정렬 단위 (SIMD/cache line 친화).
	constexpr uint32 ParticleDataAlignment = 16;

	// PSC 1개가 보유할 수 있는 활성 입자 상한 (안전 가드).
	constexpr uint32 MaxParticlesPerEmitter = 16384;

	// Spawn 모듈이 매 프레임 누적할 수 있는 최대 보너스 (burst safety).
	constexpr uint32 MaxBurstCountPerFrame = 4096;
}

namespace ParticleUtils
{
	inline uint32 AlignParticleDataSize(uint32 Size)
	{
		constexpr uint32 Align = ParticleConstants::ParticleDataAlignment;
		return (Size + Align - 1) & ~(Align - 1);
	}
}

// -----------------------------------------------------------------------------
// FBaseParticle
//   모든 입자의 공통 헤더. 정확한 layout (offset) 이 Update/Render 양쪽에서
//   고정되어야 함. Module 이 추가 payload 를 원하면 RequiredBytes() 만큼
//   이 구조체 뒤에 이어 붙여 EmitterInstance 가 관리한다.
// -----------------------------------------------------------------------------
struct FBaseParticle
{
    FVector  Location           = { 0, 0, 0 };
    FVector  OldLocation        = { 0, 0, 0 };
    FVector  Velocity           = { 0, 0, 0 };
    FVector  BaseVelocity       = { 0, 0, 0 };
    FVector4 Color              = { 1, 1, 1, 1 };
    FVector4 BaseColor          = { 1, 1, 1, 1 };
    FVector4 DynamicParameter   = { 0, 0, 0, 0 };
    FVector  Size               = { 1, 1, 1 };
    FVector  BaseSize           = { 1, 1, 1 };
    float    Rotation           = 0.0f;
    float    BaseRotation       = 0.0f;
    float    RotationRate       = 0.0f;
    float    BaseRotationRate   = 0.0f;
    float    RelativeTime       = 0.0f; // 0..1, 1 도달 시 kill
	float    OneOverMaxLifetime = 1.0f;
    uint32   Flags              = 0;
    int32    SubImageIndex      = -1; // SubUV 프레임 인덱스 (-1 = 렌더 fallback)
    uint8    SimulationLODIndex = 0;  // Spawn 시점 LOD contract. live particle update continuity 용.
    uint8    Reserved0          = 0;
    uint8    Reserved1          = 0;
    uint8    Reserved2          = 0;
};

// 입자 상태 플래그
enum class EParticleStateFlags : uint32
{
	None    = 0,
	Active  = 1u << 0,
	Spawned = 1u << 1, // 이번 프레임 생성된 입자 (Spawn 모듈만 Update)
	Killed  = 1u << 2,
};

// -----------------------------------------------------------------------------
// Payload 헬퍼
//   Module 이 RequiredBytes() 로 알려준 크기는 BaseParticle 뒤에 연속 배치된다.
//   EmitterInstance 가 ModuleOffsetMap 으로 (module → byte offset) 를 관리하고,
//   Spawn/Update 시 PARTICLE_PAYLOAD 매크로로 typed 포인터를 얻는다.
// -----------------------------------------------------------------------------
#define PARTICLE_PAYLOAD(Particle, Offset, Type) \
	reinterpret_cast<Type*>(reinterpret_cast<uint8*>(Particle) + (Offset))

#define PARTICLE_PAYLOAD_CONST(Particle, Offset, Type) \
	reinterpret_cast<const Type*>(reinterpret_cast<const uint8*>(Particle) + (Offset))

// 자주 쓰는 payload struct 들. 모듈이 자기 자신의 payload 를 정의할 수도 있다.
struct FParticlePayloadLocation { FVector InitialLocation; };
struct FParticlePayloadVelocity { FVector InitialVelocity; };
struct FParticlePayloadColor    { FVector4 InitialColor; };
struct FParticlePayloadSize     { FVector InitialSize; };

// -----------------------------------------------------------------------------
// Sprite / Mesh 정점 (Dynamic VB 에 기록됨)
// -----------------------------------------------------------------------------
// Sprite 인스턴싱 정점 (GPU billboard).
//   slot 0 = 정적 unit quad (per-vertex). VS가 CornerSign 기준으로 빌보드 corner를 펼친다.
//   slot 1 = per-instance. 입자 1개당 1세트만 전송하고 GPU가 4정점으로 확장(DrawIndexedInstanced).
struct FParticleSpriteQuadVertex
{
	FVector  CornerSign;   // POSITION   — (±0.5, ±0.5, 0)
	FVector2 UV;           // TEXTCOORD  — (0..1) base tile UV
};

// per-instance. ※ Sprite.hlsl 의 VS_Input_SpriteParticle INSTANCE_* 멤버 순서와 1:1 일치해야 함.
struct FParticleSpriteInstanceVertex
{
	FVector  Center;        // INSTANCE_CENTER    — world (bUseLocalSpace면 GT에서 변환 완료)
	FVector  Velocity;      // INSTANCE_VELOCITY  — world, Velocity alignment 전용
	FVector2 Size;          // INSTANCE_SIZE      — (X=width, Y=height) world units
	float    Rotation;      // INSTANCE_ROTATION  — radians
	FVector4 Color;         // INSTANCE_COLOR
	int32    SubImageIndex; // INSTANCE_SUBIMAGE
	int32    Alignment;     // INSTANCE_ALIGNMENT — EParticleSpriteReplayAlignment
    FVector4 DynamicParam;  // INSTANCE_DYNAMICPARAM — graph DynamicParameter node.
};

struct FParticleMeshInstanceVertex
{
	// per-instance world transform (row-major, 4 rows of float4).
	// FMatrix가 row-major + HLSL이 mul(v, M)으로 받으니 4 row 모두 전송 (row 3에 translation).
	FVector4 Transform0;
	FVector4 Transform1;
	FVector4 Transform2;
	FVector4 Transform3;
	FVector4 Color;
	int32    SubImageIndex;
    FVector4 DynamicParam;
};

struct FParticleBeamTrailVertex
{
	FVector  Position;
	FVector4 Color;
	FVector2 UV;
};

// -----------------------------------------------------------------------------
// Layout / Storage / View
//   Runtime particle memory model의 기초 타입.
//   - Layout : emitter 1개의 particle stride / instance payload / module offsets
//   - Storage: particle bytes + indices + instance data 를 single block 으로 소유
//   - View   : storage 를 읽기 전용으로 바라보는 경량 view
// -----------------------------------------------------------------------------
struct FParticleLayout
{
	uint32 ParticleStride = ParticleUtils::AlignParticleDataSize(sizeof(FBaseParticle));
	uint32 InstancePayloadSize = 0;
	TMap<const UParticleModule*, uint32> ModuleOffsets;
	TMap<const UParticleModule*, uint32> InstanceModuleOffsets;
};

struct FParticleStorage
{
	TArray<uint8> MemBlock;

	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;
	uint8* InstanceData = nullptr;

	uint32 ParticleDataBytes = 0;
	uint32 ParticleIndexCount = 0;
	uint32 InstanceDataBytes = 0;
	uint32 MaxActiveParticles = 0;

	FParticleStorage() = default;

	FParticleStorage(const FParticleStorage& Other)
		: MemBlock(Other.MemBlock)
		, ParticleDataBytes(Other.ParticleDataBytes)
		, ParticleIndexCount(Other.ParticleIndexCount)
		, InstanceDataBytes(Other.InstanceDataBytes)
		, MaxActiveParticles(Other.MaxActiveParticles)
	{
		RebindViews();
	}

	FParticleStorage& operator=(const FParticleStorage& Other)
	{
		if (this == &Other)
		{
			return *this;
		}

		MemBlock = Other.MemBlock;
		ParticleDataBytes = Other.ParticleDataBytes;
		ParticleIndexCount = Other.ParticleIndexCount;
		InstanceDataBytes = Other.InstanceDataBytes;
		MaxActiveParticles = Other.MaxActiveParticles;
		RebindViews();
		return *this;
	}

	FParticleStorage(FParticleStorage&& Other) noexcept
		: MemBlock(std::move(Other.MemBlock))
		, ParticleDataBytes(Other.ParticleDataBytes)
		, ParticleIndexCount(Other.ParticleIndexCount)
		, InstanceDataBytes(Other.InstanceDataBytes)
		, MaxActiveParticles(Other.MaxActiveParticles)
	{
		RebindViews();
		Other.Release();
	}

	FParticleStorage& operator=(FParticleStorage&& Other) noexcept
	{
		if (this == &Other)
		{
			return *this;
		}

		MemBlock = std::move(Other.MemBlock);
		ParticleDataBytes = Other.ParticleDataBytes;
		ParticleIndexCount = Other.ParticleIndexCount;
		InstanceDataBytes = Other.InstanceDataBytes;
		MaxActiveParticles = Other.MaxActiveParticles;
		RebindViews();
		Other.Release();
		return *this;
	}

	void Allocate(uint32 InParticleDataBytes, uint32 InParticleIndexCount, uint32 InInstanceDataBytes = 0)
	{
		ParticleDataBytes = InParticleDataBytes;
		ParticleIndexCount = InParticleIndexCount;
		InstanceDataBytes = InInstanceDataBytes;
		MaxActiveParticles = InParticleIndexCount;

		const uint32 IndexBytes = ParticleIndexCount * static_cast<uint32>(sizeof(uint16));
		const uint32 TotalBytes = ParticleDataBytes + IndexBytes + InstanceDataBytes;

		MemBlock.resize(TotalBytes, 0);
		RebindViews();
	}

	void Release()
	{
		MemBlock.clear();
		ParticleData = nullptr;
		ParticleIndices = nullptr;
		InstanceData = nullptr;
		ParticleDataBytes = 0;
		ParticleIndexCount = 0;
		InstanceDataBytes = 0;
		MaxActiveParticles = 0;
	}

	void Reset()
	{
		for (uint8& Byte : MemBlock)
		{
			Byte = 0;
		}

		RebindViews();
	}

	void RebindViews()
	{
		if (MemBlock.empty())
		{
			ParticleData = nullptr;
			ParticleIndices = nullptr;
			InstanceData = nullptr;
			return;
		}

		uint8* Base = MemBlock.data();
		ParticleData = Base;
		ParticleIndices = reinterpret_cast<uint16*>(Base + ParticleDataBytes);
		InstanceData = Base + ParticleDataBytes + ParticleIndexCount * static_cast<uint32>(sizeof(uint16));
	}

	bool IsAllocated() const
	{
		return !MemBlock.empty();
	}
};

struct FParticleDataView
{
	uint32 ActiveParticleCount = 0;
	uint32 ParticleStride = 0;
	const uint8* ParticleData = nullptr;
	const uint16* ParticleIndices = nullptr;
	const uint8* InstanceData = nullptr;
};

// -----------------------------------------------------------------------------
// Dynamic Emitter Data 계층
//   GameThread 측 EmitterInstance 가 매 프레임 만들어 RenderThread 의
//   SceneProxy 로 넘긴다. 한 번 채워지면 immutable.
//   ReplayData 는 (FDynamicEmitterReplayDataBase) 이걸 그대로 직렬화/리플레이
//   할 수 있도록 분리한 plain struct. DynamicData 는 ReplayData + transient 캐시.
// -----------------------------------------------------------------------------

enum class EDynamicEmitterType : uint8
{
	Unknown = 0,
	Sprite,
	Mesh,
	Beam,
	Ribbon,
	Count,	// 배열 크기 산출용 — 새 타입 추가 시 이 위에.
};

enum class EParticleReplaySortMode : uint8
{
	// ReplayData가 RT에 전달하는 정렬 정책. UObject enum을 직접 끌어오지 않고
	// GT→RT 계약만 독립적으로 유지하기 위한 경량 enum.
	None = 0,
	ViewProjDepth,
	ViewDistance,
	Age_OldestFirst,
	Age_NewestFirst,
};

struct FDynamicEmitterReplayDataBase
{
	// One emitter / one frame replay snapshot.
	// GT simulation owns the live particles; RT consumes this reduced emitter-level
	// contract after current render replay LOD interpretation and shared fallback
	// resolution have already happened on GT.
	EDynamicEmitterType EmitterType = EDynamicEmitterType::Unknown;
	// RequiredModule.SortMode를 GT에서 복사해 둔 값.
	// RT는 이 필드만 보고 emitter 내부 입자 정렬 정책을 선택한다.
	EParticleReplaySortMode SortMode = EParticleReplaySortMode::None;

	uint32 ActiveParticleCount = 0;
	uint32 ParticleStride      = 0;            // BaseParticle + payload
	FParticleStorage SnapshotStorage;

	// Material이 BlendState/DepthStencilState 등 렌더 상태의 single source of truth.
	// RT는 이 값을 section material의 primary source로 우선 사용하고, 없을 때만
	// SceneProxy cached material / type fallback으로 내려간다.
	// 별도 BlendState 필드를 두지 않음 — Material->GetBlendState() 사용.
	UMaterial* Material = nullptr;
	bool bUseLocalSpace = false;
	FMatrix LocalToWorld;      // bUseLocalSpace == true 일 때만 의미 있음 (default ctor = zero)
	// NOTE:
	//   Base replay metadata는 현재 per-particle SimulationLODIndex별 render contract가 아니다.
	//   지금 구조에서는 emitter의 current render LOD view에서 해석한 emitter-level snapshot을
	//   GT가 RT로 넘긴다. live particle simulation continuity와 render replay shaping basis는
	//   의도적으로 분리될 수 있다.
	//
	//   또한 이 snapshot은 GT가 render-ready에 가깝게 정리한 계약이다. fallback/default
	//   resolve와 authoring-derived sanitize는 가능하면 GT replay build 단계에서
	//   authoritative 하게 끝내고, RT는 일부 값에 대해서만 legacy/bad replay 방어용
	//   lightweight defensive validation을 추가로 둘 수 있다.

	FParticleDataView GetParticleView() const
	{
		FParticleDataView View;
		View.ActiveParticleCount = ActiveParticleCount;
		View.ParticleStride = ParticleStride;
		View.ParticleData = SnapshotStorage.ParticleData;
		View.ParticleIndices = SnapshotStorage.ParticleIndices;
		View.InstanceData = SnapshotStorage.InstanceData;
		return View;
	}
};

struct FDynamicEmitterDataBase
{
	virtual ~FDynamicEmitterDataBase() = default;
	virtual EDynamicEmitterType GetType() const = 0;
	virtual const FDynamicEmitterReplayDataBase& GetReplayDataBase() const = 0;
};

// -- Sprite ----
enum class EParticleSpriteReplayAlignment : uint8
{
	// RequiredModule.ScreenAlignment를 GT에서 복사한 값. RT(VS)가 빌보드 축 계산에 사용.
	// 값 순서는 Sprite.hlsl의 ALIGN_* 상수와 1:1 일치해야 함.
	Square = 0,            // 정사각, view-plane 정렬
	Rectangle,             // Size.X×Size.Y 직사각, view-plane 정렬
	Velocity,              // 화면 투영된 속도 방향으로 정렬
	FacingCameraPosition,  // 각 입자가 카메라 위치를 향함 (point-facing)
};

struct FDynamicSpriteEmitterReplayData : FDynamicEmitterReplayDataBase
{
	// Sprite shaping metadata는 current render replay LOD의 RequiredModule에서 해석한
	// emitter-level 값이다. 개별 particle의 SimulationLODIndex를 따로 반영하지 않는다.
	int32 SubImagesHorizontal = 1;
	int32 SubImagesVertical   = 1;
	EParticleSpriteReplayAlignment Alignment = EParticleSpriteReplayAlignment::Square;
};

struct FDynamicSpriteEmitterData : FDynamicEmitterDataBase
{
	FDynamicSpriteEmitterReplayData Source;
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Sprite; }
	const FDynamicEmitterReplayDataBase& GetReplayDataBase() const override { return Source; }
};

// -- Mesh ----
enum class EParticleMeshReplayAlignment : uint8
{
	None = 0,
	Velocity,
	FacingCamera,
	AxisLock,
};

struct FDynamicMeshEmitterReplayData : FDynamicEmitterReplayDataBase
{
	// Mesh render metadata는 current render replay LOD의 TypeDataModule view에서 복사된다.
	//
	// Current RT Mesh contract:
	//   - Mesh                : actively consumed (static mesh selection / fallback)
	//   - Material            : actively consumed via base replay contract (section material,
	//                           SubImagesH/V lookup for frame count)
	//   - Alignment           : currently representational only; carried from GT but not yet
	//                           applied to RT instance orientation
	//   - bOverrideMaterial   : currently representational only; actual RT material authority
	//                           is still driven by the shared replay-first material chain
	UStaticMesh* Mesh = nullptr;
	EParticleMeshReplayAlignment Alignment = EParticleMeshReplayAlignment::None;
	bool bOverrideMaterial = false;
};

struct FDynamicMeshEmitterData : FDynamicEmitterDataBase
{
	FDynamicMeshEmitterReplayData Source;
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Mesh; }
	const FDynamicEmitterReplayDataBase& GetReplayDataBase() const override { return Source; }
};

// -- Beam ----
struct FDynamicBeamEmitterReplayData : FDynamicEmitterReplayDataBase
{
	// Beam shaping inputs도 emitter-level current render replay LOD view에서 해석한다.
	// 현재 RT beam path는 per-particle simulation LOD별 beam contract를 따로 들고 가지 않는다.
	//
	// 즉 이 struct의 Source/Target/Tangent/Noise 값은 "각 active particle마다 독립 beam"
	// 을 기술하는 per-particle payload가 아니라, emitter 하나가 이번 프레임에 보여줄
	// beam strip shape를 설명하는 emitter-level snapshot이다.
	//
	// 참고로 Base.ActiveParticleCount는 generic replay header에서 온 값이며, 현재 Beam RT
	// path에서는 독립 endpoint 집합 수가 아니라 strip multiplicity를 결정하는 제한적/
	// legacy hint로만 사용된다.
	int32 InterpolationPoints = 0;
	FVector SourcePoint = { 0, 0, 0 };
	FVector TargetPoint = { 0, 0, 0 };
	FVector SourceTangent = { 0, 0, 0 };
	FVector TargetTangent = { 0, 0, 0 };
	FVector NoiseDirection = { 0, 0, 1 };
	float Width = 10.0f;            // beam 띠 전체 폭
	float NoiseAmount = 0.0f;       // 수직 변위 진폭 (0 = 직선)
	float NoiseFrequency = 1.0f;    // beam 길이당 sin파 횟수
	float NoiseSpeed = 2.0f;        // 시간에 따른 noise 흐름 속도
	int32 NoiseTessellation = 0;    // noise 표현을 위한 최소 분할 수
	bool bSmoothNoise = true;       // 현재 sine 기반 smooth noise 플래그
	bool bTileUV = true;            // beam 전체 길이에 따라 UV를 반복(tile)할지 여부
	bool bRenderGeometry = true;    // false면 Beam geometry를 생성하지 않음
	float TaperFactor = 1.0f;       // target 방향 폭 배율
	bool bTaperFull = false;        // true면 source→target으로 Width를 TaperFactor까지 보간
	float EmitterTime = 0.0f;       // GT 누적 시간 (시간 기반 noise phase)
};

struct FDynamicBeamEmitterData : FDynamicEmitterDataBase
{
	FDynamicBeamEmitterReplayData Source;
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Beam; }
	const FDynamicEmitterReplayDataBase& GetReplayDataBase() const override { return Source; }
};

// -- Ribbon ----
struct FDynamicRibbonEmitterReplayData : FDynamicEmitterReplayDataBase
{
	// Current Ribbon render contract is emitter-level and single-trail:
	// one replay snapshot represents one emitter's active-particle trail for this
	// frame, not multiple independent ribbon chains.
	//
	// Shared replay base still carries generic SortMode metadata, but current
	// Ribbon RT does not treat that as "how many ribbon chains exist" or "how the
	// chain is topologically connected." Ribbon chain order is reconstructed from
	// trail continuity rules in the RT path.
	//
	// The fields below are authoring/type-data derived shaping inputs consumed by
	// the RT ribbon geometry builder. They describe how the single trail should be
	// curved/tessellated/UV-tiled from the current render replay LOD view; they are
	// not per-particle payload values.
	//
	// Sanitize responsibility:
	//   - GT replay build is authoritative for normal authoring-domain clamp
	//     (tessellation range, tangent tension range, non-negative UV tiling).
	//   - RT may still defensively revalidate the same fields before geometry build
	//     as a lightweight safety net for bad/incomplete replay inputs.
	int32 MaxTessellation = 8;
	float TangentTension = 0.5f;    // ribbon tangent 보간 강도 (0 = 느슨함, 1 = 강함)
	float TilesPerTrail = 1.0f;     // trail 전체 UV 반복 수
};

struct FDynamicRibbonEmitterData : FDynamicEmitterDataBase
{
	FDynamicRibbonEmitterReplayData Source;
	EDynamicEmitterType GetType() const override { return EDynamicEmitterType::Ribbon; }
	const FDynamicEmitterReplayDataBase& GetReplayDataBase() const override { return Source; }
};

// -----------------------------------------------------------------------------
// FParticleSystemReplayFrame
//   한 프레임 PSC 전체 상태 (모든 emitter 의 ReplayData 묶음). 디버깅/리플레이 용도.
// -----------------------------------------------------------------------------
struct FParticleSystemReplayFrame
{
	float TimeSeconds = 0.0f;
	TArray<FDynamicEmitterReplayDataBase*> Emitters; // owned by frame
	~FParticleSystemReplayFrame()
	{
		for (auto* E : Emitters) delete E;
	}
};
