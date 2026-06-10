#include "ParticleEmitterInstance.h"

#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"
#include "Particle/Modules/ParticleModuleCollision.h"
#include "Particle/Modules/ParticleModuleEventGenerator.h"
#include "Particle/Modules/ParticleModuleEventReceiverBase.h"
#include "Particle/Modules/ParticleModuleEventReceiverKillAll.h"
#include "Particle/Modules/ParticleModuleEventReceiverSpawn.h"
#include "Particle/Modules/ParticleModuleSpawn.h"
#include "Particle/Modules/ParticleModuleRequired.h"
#include "Particle/Modules/ParticleModuleBeamSource.h"
#include "Particle/Modules/ParticleModuleBeamTarget.h"
#include "Particle/Modules/ParticleModuleBeamNoise.h"
#include "Particle/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam.h"
#include "Particle/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Core/Logging/Log.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/World.h"
#include "Math/Transform.h"
#include "Profiling/Stats/ParticleStats.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

static EParticleReplaySortMode ToReplaySortMode(UParticleModuleRequired::ESortMode InSortMode);
static EParticleMeshReplayAlignment ToReplayMeshAlignment(
	UParticleModuleTypeDataMesh::EMeshAlignment InAlignment);
static EParticleSpriteReplayAlignment ToReplaySpriteAlignment(
	UParticleModuleRequired::EScreenAlignment InAlignment);

namespace
{
	constexpr float ParticleCollisionMinTravelDistance = 1.0e-3f;
	constexpr float ParticleCollisionMinMeaningfulSpeed = 5.0e-2f;
	constexpr float ParticleCollisionCooldownSeconds = 2.5e-2f;
	constexpr float ParticleCollisionSameSurfaceNormalDotThreshold = 0.95f;
	constexpr float ParticleCollisionSurfaceOffset = 1.0e-2f;
	constexpr float ParticleCollisionBudgetPrioritySpeedWeight = 0.1f;
	constexpr float ParticleCollisionBudgetNearLimitPenalty = 0.5f;
	constexpr float ParticleCollisionBudgetRecentHitPenalty = 0.35f;
	constexpr float ParticleCollisionBudgetLowSpeedPenalty = 0.5f;
	constexpr float ParticleCollisionHighPriorityScoreThreshold = 20.0f;
	constexpr int32 ParticleCollisionBudgetLOD0 = 512;
	constexpr int32 ParticleCollisionBudgetLOD1 = 128;
	constexpr int32 ParticleCollisionBudgetLOD2OrLower = 0;
	constexpr float ParticleCollisionEmitterPruneMinMovementDistance = 5.0f;
	constexpr float ParticleCollisionEmitterPruneMinProbeDistance = 100.0f;
	constexpr float ParticleCollisionEmitterPruneProbePadding = 50.0f;
	constexpr float ParticleCollisionNearbyCacheRefreshSeconds = 0.1f;
	constexpr float ParticleCollisionNearbyCacheMinCenterDelta = 25.0f;
	constexpr float ParticleCollisionNearbyCacheMinExtentDelta = 10.0f;
	constexpr float ParticleCollisionNearbyCacheLateralProbeMinExtent = 25.0f;
	// Narrow runtime gate for collision observability. Leave false by default so
	// normal gameplay stays quiet; flip during focused collision investigation.
	bool GParticleCollisionDebugEnabled = false;
	constexpr float ParticleCollisionDebugDrawDuration = 0.05f;
	constexpr float ParticleCollisionDebugPointSize = 0.1f;
	constexpr float ParticleCollisionDebugNormalLength = 10.0f;

	bool ResolveDefaultCollisionDisablePolicyForLOD(int32 LODIndex)
	{
		return LODIndex >= 2;
	}

	bool ResolveDefaultCollisionEventPolicyForLOD(int32 LODIndex)
	{
		return LODIndex <= 0;
	}

	int32 ResolveDefaultCollisionBudgetForLOD(int32 LODIndex)
	{
		if (LODIndex <= 0)
		{
			return ParticleCollisionBudgetLOD0;
		}

		if (LODIndex == 1)
		{
			return ParticleCollisionBudgetLOD1;
		}

		return ParticleCollisionBudgetLOD2OrLower;
	}

	void DecomposeVelocityAgainstSurfaceNormal(
		const FVector& Velocity,
		const FVector& SurfaceNormal,
		FVector& OutNormalComponent,
		FVector& OutTangentialComponent)
	{
		const float NormalSpeed = Velocity.Dot(SurfaceNormal);
		OutNormalComponent = SurfaceNormal * NormalSpeed;
		OutTangentialComponent = Velocity - OutNormalComponent;
	}

	bool HasCollisionCountLimit(const UParticleModuleCollision& CollisionModule)
	{
		return CollisionModule.MaxCollisions > 0;
	}

	float GetCollisionSpeed(const FVector& Velocity)
	{
		return Velocity.Length();
	}

	bool IsMeaningfulCollisionSpeed(float Speed)
	{
		return Speed > ParticleCollisionMinMeaningfulSpeed;
	}

	bool WasCollisionHandledRecently(
		const UParticleModuleCollision::FCollisionParticlePayload& Payload,
		float EvaluationTimeSeconds)
	{
		return Payload.LastCollisionTime >= 0.0f &&
			(EvaluationTimeSeconds - Payload.LastCollisionTime) <= ParticleCollisionCooldownSeconds;
	}

	UParticleModuleCollision::ECollisionResponseMode ResolveImmediateCollisionResponseMode(
		const UParticleModuleCollision& CollisionModule)
	{
		if (CollisionModule.bKillOnCollision)
		{
			return UParticleModuleCollision::ECollisionResponseMode::Kill;
		}

		return CollisionModule.ResponseMode;
	}

	void ApplyCollisionCompletionBehavior(
		FBaseParticle& Particle,
		UParticleModuleCollision::FCollisionParticlePayload& Payload,
		const UParticleModuleCollision& CollisionModule)
	{
		switch (CollisionModule.CompletionMode)
		{
		case UParticleModuleCollision::ECollisionCompletionMode::Kill:
			Particle.Flags |= static_cast<uint32>(EParticleStateFlags::Killed);
			break;
		case UParticleModuleCollision::ECollisionCompletionMode::IgnoreFurtherCollisions:
			Payload.bIgnoreFurtherCollisions = true;
			break;
		case UParticleModuleCollision::ECollisionCompletionMode::Freeze:
		default:
			Payload.bIgnoreFurtherCollisions = true;
			Payload.bFrozenAfterLimit = true;
			Particle.Velocity = FVector::ZeroVector;
			Particle.OldLocation = Particle.Location;
			break;
		}
	}

	bool ShouldSuppressRepeatedCollisionHit(
		const UParticleModuleCollision::FCollisionParticlePayload& Payload,
		const FVector& CollisionNormal,
		float CollisionTimeSeconds)
	{
		if (Payload.LastCollisionTime < 0.0f)
		{
			return false;
		}

		const float TimeSinceLastCollision = CollisionTimeSeconds - Payload.LastCollisionTime;
		if (TimeSinceLastCollision > ParticleCollisionCooldownSeconds)
		{
			return false;
		}

		const FVector PriorNormal = Payload.LastCollisionNormal.IsNearlyZero()
			? FVector::UpVector
			: Payload.LastCollisionNormal.Normalized();
		const float SameSurfaceDot = CollisionNormal.Dot(PriorNormal);
		return SameSurfaceDot >= ParticleCollisionSameSurfaceNormalDotThreshold;
	}

	void UpdateRecentCollisionState(
		UParticleModuleCollision::FCollisionParticlePayload& Payload,
		float CollisionTimeSeconds,
		const FVector& CollisionNormal)
	{
		Payload.LastCollisionTime = CollisionTimeSeconds;
		Payload.LastCollisionNormal = CollisionNormal;
	}

	struct FSanitizedRibbonReplayShaping
	{
		int32 MaxTessellation = 8;
		float TangentTension = 0.5f;
		float TilesPerTrail = 1.0f;
	};

	FSanitizedRibbonReplayShaping BuildSanitizedRibbonReplayShapingOnGT(
		const UParticleModuleTypeDataRibbon& RibbonTypeData)
	{
		FSanitizedRibbonReplayShaping Shaping;
		Shaping.MaxTessellation = std::clamp(RibbonTypeData.MaxTessellation, 1, 64);
		Shaping.TangentTension = std::clamp(RibbonTypeData.TangentTension, 0.0f, 1.0f);
		Shaping.TilesPerTrail = std::max(0.0f, RibbonTypeData.TilesPerTrail);
		return Shaping;
	}

	void CopyParticleSnapshotToReplay(
		const FParticleEmitterInstance& EmitterInstance,
		FDynamicEmitterReplayDataBase& OutData)
	{
		const uint32 ActiveParticleCount = EmitterInstance.GetActiveParticleCount();
		const uint32 ParticleStride = EmitterInstance.GetParticleStride();

		OutData.ActiveParticleCount = ActiveParticleCount;
		OutData.ParticleStride = ParticleStride;
		OutData.SnapshotStorage.Allocate(ActiveParticleCount * ParticleStride, ActiveParticleCount, 0);

		for (uint32 i = 0; i < ActiveParticleCount; ++i)
		{
			const FBaseParticle* Particle = EmitterInstance.GetParticleAt(i);
			if (!Particle)
			{
				continue;
			}

			std::memcpy(
				OutData.SnapshotStorage.ParticleData + i * ParticleStride,
				Particle,
				ParticleStride);

			OutData.SnapshotStorage.ParticleIndices[i] = static_cast<uint16>(i);
		}
	}

	void FillBaseReplayMetadataFromRequiredModule(
		const FParticleEmitterInstance& EmitterInstance,
		UParticleLODLevel& RenderReplayLOD,
		FDynamicEmitterReplayDataBase& OutData)
	{
		UParticleModuleRequired* Required = RenderReplayLOD.RequiredModule;
		if (!Required)
		{
			return;
		}

		OutData.Material = Required->ResolveMaterial();
		// Replay.Material은 SceneProxy가 section material을 고를 때 우선 사용하는 primary source다.
		// cached emitter material은 이 값이 비었을 때만 fallback으로 사용된다.
		// SortMode는 material blend와 별개로 "어떤 기준으로 particle를 재배치할지"를 RT에 알려준다.
		OutData.SortMode = ToReplaySortMode(Required->SortMode);
		// NOTE: Replay에 BlendState 필드 없음 — Material.GetBlendState()가 single source of truth.
		// RequiredModule.BlendState로 Material을 override하고 싶으면 SceneProxy의
		// Material 캐싱 단계에서 SetBlendState 같은 API 추가 필요 (현재 RequiredModule.SubImagesH/V와 동일 패턴).
		OutData.bUseLocalSpace = Required->bUseLocalSpace;
		// Base replay metadata는 current render replay LOD의 RequiredModule view에서 채워진다.
		// 이미 살아 있는 particle의 SimulationLODIndex continuity와는 다른 계층의 계약이다.
		// 또한 GT는 여기서 fallback/default를 resolve한 render-ready snapshot을 만드는 쪽이
		// authoritative하다. RT는 draw 직전 일부 값만 defensive safety net으로 재검증한다.

		if (const UParticleSystemComponent* Component = EmitterInstance.GetComponent())
		{
			OutData.LocalToWorld = Component->GetWorldMatrix();
		}
	}

	void FillSpriteReplayShapingFromRenderLOD(
		UParticleLODLevel& RenderReplayLOD,
		FDynamicSpriteEmitterReplayData& OutData)
	{
		if (!RenderReplayLOD.RequiredModule)
		{
			return;
		}

		// Sprite replay shaping은 current render replay LOD의 RequiredModule 기준이다.
		OutData.SubImagesHorizontal = RenderReplayLOD.RequiredModule->SubImagesHorizontal;
		OutData.SubImagesVertical = RenderReplayLOD.RequiredModule->SubImagesVertical;
		OutData.Alignment = ToReplaySpriteAlignment(RenderReplayLOD.RequiredModule->ScreenAlignment);
	}

	void FillMeshReplayShapingFromRenderLOD(
		UParticleLODLevel& RenderReplayLOD,
		FDynamicMeshEmitterReplayData& OutData)
	{
		// Mesh replay shaping은 current render replay LOD의 TypeDataModule view를 사용한다.
		if (auto* MeshTypeData = Cast<UParticleModuleTypeDataMesh>(RenderReplayLOD.TypeDataModule))
		{
			OutData.Mesh = MeshTypeData->ResolveMesh();
			// Alignment / bOverrideMaterial are still forwarded so the replay snapshot
			// preserves authoring intent, but the current RT Mesh path does not yet
			// interpret them as active orientation/material-behavior switches.
			OutData.Alignment = ToReplayMeshAlignment(MeshTypeData->Alignment);
			OutData.bOverrideMaterial = MeshTypeData->bOverrideMaterial;
		}
	}

	void FillBeamReplayShapingFromRenderLOD(
		FParticleBeamEmitterInstance& EmitterInstance,
		UParticleLODLevel& RenderReplayLOD,
		FDynamicBeamEmitterReplayData& OutData,
		FVector& InOutResolvedSource,
		FVector& InOutResolvedTarget,
		FVector& InOutLockedSourcePoint,
		FVector& InOutLockedTargetPoint,
		bool& bInOutHasLockedSourcePoint,
		bool& bInOutHasLockedTargetPoint,
		bool bHasExplicitEndpoints)
	{
		const float EvalTime = EmitterInstance.GetCurrentLoopTimeSeconds();
		UParticleModuleTypeDataBeam* BeamTypeData = Cast<UParticleModuleTypeDataBeam>(RenderReplayLOD.TypeDataModule);
		float BeamDistance = (InOutResolvedTarget - InOutResolvedSource).Length();

		// Beam replay inputs도 emitter-level current render replay LOD에서 해석한다.
		// Source/Target/Noise shaping은 현재 RT beam path가 소비할 단일 replay snapshot을 만들기 위한 값이다.
		// 즉 ActiveParticleCount와 별개로, GT는 여기서 "active particle마다 서로 다른 beam endpoint"
		// 배열을 만들지 않는다. Beam RT는 이 emitter-level shape contract를 받아 strip을 만든다.
		if (BeamTypeData)
		{
			if (!bHasExplicitEndpoints)
			{
				InOutResolvedSource =
					EmitterInstance.ConvertPositionToSimulation(BeamTypeData->DefaultSource, EParticleValueSpace::Local);
				InOutResolvedTarget =
					EmitterInstance.ConvertPositionToSimulation(BeamTypeData->DefaultTarget, EParticleValueSpace::Local);
			}

			BeamDistance = BeamTypeData->EvaluateDistance(EvalTime, EmitterInstance.GetComponent());
			OutData.InterpolationPoints = BeamTypeData->InterpolationPoints;
			OutData.Width = BeamTypeData->EvaluateWidth(EvalTime, EmitterInstance.GetComponent());
			OutData.bTileUV = BeamTypeData->bTileUV;
			OutData.bRenderGeometry = BeamTypeData->bRenderGeometry;
			OutData.TaperFactor = BeamTypeData->TaperFactor;
			OutData.bTaperFull =
				BeamTypeData->TaperMethod == PEBTM_Full;

			if (BeamTypeData->BeamMethod == PEB2M_Distance)
			{
				const FVector LocalXDistance(BeamDistance, 0.0f, 0.0f);
				InOutResolvedTarget = InOutResolvedSource +
					EmitterInstance.ConvertVectorToSimulation(LocalXDistance, EParticleValueSpace::Local);
			}
		}

		if (UParticleModuleBeamSource* SourceModule = RenderReplayLOD.FindModuleByClass<UParticleModuleBeamSource>())
		{
			if (SourceModule->IsEnabled())
			{
				FVector ModuleSource = SourceModule->ResolveSource(&EmitterInstance, EvalTime, InOutResolvedSource);
				if (SourceModule->bLockSource)
				{
					if (!bInOutHasLockedSourcePoint)
					{
						InOutLockedSourcePoint = ModuleSource;
						bInOutHasLockedSourcePoint = true;
					}
					ModuleSource = InOutLockedSourcePoint;
				}
				else
				{
					bInOutHasLockedSourcePoint = false;
				}
				InOutResolvedSource = ModuleSource;
				OutData.SourceTangent = SourceModule->ResolveSourceTangent(&EmitterInstance, EvalTime);
			}
		}
		else
		{
			bInOutHasLockedSourcePoint = false;
		}

		// Source가 모듈에 의해 바뀐 뒤 Distance Beam target을 다시 계산한다.
		if (BeamTypeData && BeamTypeData->BeamMethod == PEB2M_Distance)
		{
			const FVector LocalXDistance(BeamDistance, 0.0f, 0.0f);
			InOutResolvedTarget = InOutResolvedSource +
				EmitterInstance.ConvertVectorToSimulation(LocalXDistance, EParticleValueSpace::Local);
		}

		if (UParticleModuleBeamTarget* TargetModule = RenderReplayLOD.FindModuleByClass<UParticleModuleBeamTarget>())
		{
			if (TargetModule->IsEnabled())
			{
				FVector ModuleTarget = TargetModule->ResolveTarget(
					&EmitterInstance,
					EvalTime,
					InOutResolvedSource,
					InOutResolvedTarget,
					BeamDistance);
				if (TargetModule->bLockTarget)
				{
					if (!bInOutHasLockedTargetPoint)
					{
						InOutLockedTargetPoint = ModuleTarget;
						bInOutHasLockedTargetPoint = true;
					}
					ModuleTarget = InOutLockedTargetPoint;
				}
				else
				{
					bInOutHasLockedTargetPoint = false;
				}
				InOutResolvedTarget = ModuleTarget;
				OutData.TargetTangent = TargetModule->ResolveTargetTangent(&EmitterInstance, EvalTime);
			}
		}
		else
		{
			bInOutHasLockedTargetPoint = false;
		}

		if (BeamTypeData && BeamTypeData->Speed > 0.0f)
		{
			const FVector FullAxis = InOutResolvedTarget - InOutResolvedSource;
			const float FullLength = FullAxis.Length();
			const float VisibleLength = BeamTypeData->Speed * EvalTime;
			if (FullLength > 1e-4f && VisibleLength < FullLength)
			{
				const float VisibleRatio = VisibleLength / FullLength;
				InOutResolvedTarget = InOutResolvedSource + FullAxis * VisibleRatio;
				OutData.SourceTangent = OutData.SourceTangent * VisibleRatio;
				OutData.TargetTangent = OutData.TargetTangent * VisibleRatio;
			}
		}

		if (UParticleModuleBeamNoise* NoiseModule = RenderReplayLOD.FindModuleByClass<UParticleModuleBeamNoise>())
		{
			if (NoiseModule->IsEnabled())
			{
				OutData.NoiseAmount = NoiseModule->EvaluateNoiseRange(EvalTime, EmitterInstance.GetComponent());
				OutData.NoiseDirection = NoiseModule->ResolveNoiseDirection(&EmitterInstance, EvalTime);
				OutData.NoiseFrequency = NoiseModule->EvaluateNoiseFrequency(EvalTime, EmitterInstance.GetComponent());
				OutData.NoiseSpeed = NoiseModule->EvaluateNoiseSpeed(EvalTime, EmitterInstance.GetComponent());
				OutData.NoiseTessellation = NoiseModule->NoiseTessellation;
				OutData.bSmoothNoise = NoiseModule->bSmooth;
			}
		}

		OutData.EmitterTime = EmitterInstance.GetEmitterTimeSeconds();
	}

	void FillRibbonReplayShapingFromRenderLOD(
		UParticleLODLevel& RenderReplayLOD,
		FDynamicRibbonEmitterReplayData& OutData)
	{
		if (auto* RibbonTypeData = Cast<UParticleModuleTypeDataRibbon>(RenderReplayLOD.TypeDataModule))
		{
			// Ribbon replay metadata is currently an emitter-level render contract.
			// Even though live particle simulation can preserve spawn-time
			// SimulationLODIndex, the current Ribbon RT path still builds one trail
			// snapshot per emitter per frame, so its shaping inputs intentionally come
			// from the current emitter LOD rather than per-particle simulation LOD.
			//
			// TypeData values are sanitized here before crossing the GT->RT boundary so
			// the replay struct stays a practical, render-ready contract rather than a
			// raw bag of authoring values.
			// RT still revalidates these fields defensively before geometry emission,
			// but that second clamp is a safety net, not the authoritative source of
			// truth for normal authoring-domain sanitize.
			const FSanitizedRibbonReplayShaping Shaping =
				BuildSanitizedRibbonReplayShapingOnGT(*RibbonTypeData);
			OutData.MaxTessellation = Shaping.MaxTessellation;
			OutData.TangentTension = Shaping.TangentTension;
			OutData.TilesPerTrail = Shaping.TilesPerTrail;
		}
	}
}

static EParticleReplaySortMode ToReplaySortMode(UParticleModuleRequired::ESortMode InSortMode)
{
	// RequiredModule의 UObject enum을 replay 전용 POD enum으로 축소.
	// RT는 이 변환 결과만 보고 정렬 comparator를 선택한다.
	switch (InSortMode)
	{
	case UParticleModuleRequired::ESortMode::ViewProjDepth:
		return EParticleReplaySortMode::ViewProjDepth;
	case UParticleModuleRequired::ESortMode::ViewDistance:
		return EParticleReplaySortMode::ViewDistance;
	case UParticleModuleRequired::ESortMode::Age_OldestFirst:
		return EParticleReplaySortMode::Age_OldestFirst;
	case UParticleModuleRequired::ESortMode::Age_NewestFirst:
		return EParticleReplaySortMode::Age_NewestFirst;
	case UParticleModuleRequired::ESortMode::None:
	default:
		return EParticleReplaySortMode::None;
	}
}

static EParticleMeshReplayAlignment ToReplayMeshAlignment(
	UParticleModuleTypeDataMesh::EMeshAlignment InAlignment)
{
	switch (InAlignment)
	{
	case UParticleModuleTypeDataMesh::EMeshAlignment::Velocity:
		return EParticleMeshReplayAlignment::Velocity;
	case UParticleModuleTypeDataMesh::EMeshAlignment::FacingCamera:
		return EParticleMeshReplayAlignment::FacingCamera;
	case UParticleModuleTypeDataMesh::EMeshAlignment::AxisLock:
		return EParticleMeshReplayAlignment::AxisLock;
	case UParticleModuleTypeDataMesh::EMeshAlignment::None:
	default:
		return EParticleMeshReplayAlignment::None;
	}
}

static EParticleSpriteReplayAlignment ToReplaySpriteAlignment(
	UParticleModuleRequired::EScreenAlignment InAlignment)
{
	switch (InAlignment)
	{
	case UParticleModuleRequired::EScreenAlignment::Rectangle:
		return EParticleSpriteReplayAlignment::Rectangle;
	case UParticleModuleRequired::EScreenAlignment::Velocity:
		return EParticleSpriteReplayAlignment::Velocity;
	case UParticleModuleRequired::EScreenAlignment::FacingCameraPosition:
		return EParticleSpriteReplayAlignment::FacingCameraPosition;
	case UParticleModuleRequired::EScreenAlignment::Square:
	default:
		return EParticleSpriteReplayAlignment::Square;
	}
}

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	RuntimeStorage.Release();
}

void FParticleEmitterInstance::Init(UParticleEmitter* InEmitter, UParticleSystemComponent* InComponent)
{
	Emitter = InEmitter;
	Component = InComponent;

	CurrentLODIndex = 0;
	SpawnFraction = 0.0f;
	EmitterTimeSeconds = 0.0f;
	CurrentLoopTimeSeconds = 0.0f;
	LoopCount = 0;
	ActiveParticles = 0;
	LastCollisionPruneBoundsCenter = FVector::ZeroVector;
	bHasLastCollisionPruneBoundsCenter = false;
	NearbyCollisionCache = FEmitterNearbyCollisionCache{};
	bHaltSpawning = false;

	ClearPendingEvents();

	if (Emitter)
	{
		Emitter->CacheEmitterModuleInfo();

		ModuleOffsetMap = &Emitter->GetModuleOffsetMap();
		ModuleInstanceOffsetMap = &Emitter->GetParticleLayout().InstanceModuleOffsets;

		ParticleStride = std::max<uint32>(Emitter->GetParticleSize(), sizeof(FBaseParticle));
	}
	else
	{
		ModuleOffsetMap = nullptr;
		ModuleInstanceOffsetMap = nullptr;
		ParticleStride = sizeof(FBaseParticle);
	}

	ResizeParticleData(GetInitialParticleCapacity());
}

void FParticleEmitterInstance::Tick(float DeltaTime)
{
	if (!Emitter) return;
	if (!Component) return;
	if (DeltaTime <= 0.0f) return;

	ClearPendingEvents();

	float RemainingDeltaTime = DeltaTime;
	// duration이 있는 emitter는 loop 경계에서 step을 잘라서 처리해야
	// SpawnModule의 burst/time 누적과 LoopCount 갱신이 같은 프레임 안에서도 일관된다.
	while (RemainingDeltaTime > 0.0f)
	{
		float StepDeltaTime = RemainingDeltaTime;

		if (const UParticleModuleRequired* Required = GetRequiredModule())
		{
			if (Required->EmitterDuration > 0.0f && IsSpawningAllowed())
			{
				const float TimeUntilLoopEnd =
					std::max(0.0f, Required->EmitterDuration - CurrentLoopTimeSeconds);
				StepDeltaTime = std::min(StepDeltaTime, TimeUntilLoopEnd);
			}
		}

		if (StepDeltaTime <= 0.0f)
		{
			AdvanceLoopState(0.0f);
			break;
		}

		if (IsSpawningAllowed())
		{
			SpawnParticles(StepDeltaTime);
		}

		UpdateParticles(StepDeltaTime);
		AdvanceLoopState(StepDeltaTime);
		RemainingDeltaTime -= StepDeltaTime;
	}
}

void FParticleEmitterInstance::Reset()
{
	ActiveParticles = 0;
	SpawnFraction = 0.0f;
	EmitterTimeSeconds = 0.0f;
	CurrentLoopTimeSeconds = 0.0f;
	LoopCount = 0;
	LastCollisionPruneBoundsCenter = FVector::ZeroVector;
	bHasLastCollisionPruneBoundsCenter = false;
	NearbyCollisionCache = FEmitterNearbyCollisionCache{};
	bHaltSpawning = false;

	ClearPendingEvents();

	for (uint32 i = 0; i < MaxActiveParticles; ++i)
	{
		RuntimeStorage.ParticleIndices[i] = static_cast<uint16>(i);
	}

	if (RuntimeStorage.InstanceData && RuntimeStorage.InstanceDataBytes > 0)
	{
		std::memset(RuntimeStorage.InstanceData, 0, RuntimeStorage.InstanceDataBytes);
	}
}

FDynamicEmitterDataBase* FParticleEmitterInstance::GetDynamicData()
{
	// 베이스는 데이터 만들지 않음. type-별 서브클래스가 구현.
	return nullptr;
}

uint32 FParticleEmitterInstance::GetModuleDataOffset(const UParticleModule* InModule) const
{
	if (!ModuleOffsetMap) return 0;
	auto It = ModuleOffsetMap->find(InModule);
	if (It == ModuleOffsetMap->end()) return 0;
	return It->second;
}

FBaseParticle* FParticleEmitterInstance::GetParticleAt(uint32 InActiveIndex)
{
	if (InActiveIndex >= ActiveParticles) return nullptr;
	const uint16 Slot = RuntimeStorage.ParticleIndices[InActiveIndex];
	return reinterpret_cast<FBaseParticle*>(RuntimeStorage.ParticleData + Slot * ParticleStride);
}

const FBaseParticle* FParticleEmitterInstance::GetParticleAt(uint32 InActiveIndex) const
{
	if (InActiveIndex >= ActiveParticles) return nullptr;
	const uint16 Slot = RuntimeStorage.ParticleIndices[InActiveIndex];
	return reinterpret_cast<const FBaseParticle*>(RuntimeStorage.ParticleData + Slot * ParticleStride);
}

void* FParticleEmitterInstance::GetInstancePayloadData()
{
	return RuntimeStorage.InstanceData;
}

const void* FParticleEmitterInstance::GetInstancePayloadData() const
{
	return RuntimeStorage.InstanceData;
}

UParticleLODLevel* FParticleEmitterInstance::GetCurrentLOD() const
{
	if (!Emitter) return nullptr;
	return Emitter->GetCurrentLODLevel(CurrentLODIndex);
}

UParticleLODLevel* FParticleEmitterInstance::GetRenderReplayLODLevel() const
{
	// Render replay는 아직 per-particle SimulationLODIndex별 snapshot을 만들지 않는다.
	// GT는 현재 emitter가 보고 있는 render LOD view를 emitter-level snapshot으로 고정해
	// RT에 넘긴다. 이 helper는 그 current render LOD basis를 simulation continuity와
	// 구분해 드러내기 위한 contract boundary다.
	return GetCurrentLOD();
}

void FParticleEmitterInstance::BuildActiveParticleSimulationLODBuckets(TArray<TArray<uint32>>& OutBuckets) const
{
	const int32 LODCount = Emitter ? std::max(Emitter->GetLODCount(), 1) : 1;
	OutBuckets.clear();
	OutBuckets.resize(LODCount);

	for (uint32 ActiveIndex = 0; ActiveIndex < ActiveParticles; ++ActiveIndex)
	{
		const FBaseParticle* Particle = GetParticleAt(ActiveIndex);
		if (!Particle)
		{
			continue;
		}

		const int32 SimulationLODIndex = GetParticleSimulationLODIndex(*Particle);
		OutBuckets[SimulationLODIndex].push_back(ActiveIndex);
	}
}

bool FParticleEmitterInstance::UsesLocalSpace() const
{
	const UParticleModuleRequired* Required = GetRequiredModule();
	return Required ? Required->bUseLocalSpace : false;
}

FVector FParticleEmitterInstance::ConvertVectorToSimulation(
	const FVector& V,
	EParticleValueSpace SourceSpace) const
{
	switch (SourceSpace)
	{
	case EParticleValueSpace::Simulation:
		return V;
	case EParticleValueSpace::Local:
		if (UsesLocalSpace() || !Component)
		{
			return V;
		}

		return Component->GetWorldMatrix().TransformVector(V);
	case EParticleValueSpace::World:
		if (!UsesLocalSpace() || !Component)
		{
			return V;
		}

		return Component->GetWorldInverseMatrix().TransformVector(V);
	default:
		return V;
	}
}

FVector FParticleEmitterInstance::ConvertVectorFromSimulation(
	const FVector& V,
	EParticleValueSpace TargetSpace) const
{
	switch (TargetSpace)
	{
	case EParticleValueSpace::Simulation:
		return V;
	case EParticleValueSpace::Local:
		if (UsesLocalSpace() || !Component)
		{
			return V;
		}

		return Component->GetWorldInverseMatrix().TransformVector(V);
	case EParticleValueSpace::World:
		if (!UsesLocalSpace() || !Component)
		{
			return V;
		}

		return Component->GetWorldMatrix().TransformVector(V);
	default:
		return V;
	}
}

FVector FParticleEmitterInstance::ConvertPositionToSimulation(
	const FVector& P,
	EParticleValueSpace SourceSpace) const
{
	switch (SourceSpace)
	{
	case EParticleValueSpace::Simulation:
		return P;
	case EParticleValueSpace::Local:
		if (UsesLocalSpace() || !Component)
		{
			return P;
		}

		return Component->GetWorldMatrix().TransformPositionWithW(P);
	case EParticleValueSpace::World:
		if (!UsesLocalSpace() || !Component)
		{
			return P;
		}

		return Component->GetWorldInverseMatrix().TransformPositionWithW(P);
	default:
		return P;
	}
}

FVector FParticleEmitterInstance::ConvertPositionFromSimulation(
	const FVector& P,
	EParticleValueSpace TargetSpace) const
{
	switch (TargetSpace)
	{
	case EParticleValueSpace::Simulation:
		return P;
	case EParticleValueSpace::Local:
		if (UsesLocalSpace() || !Component)
		{
			return P;
		}

		return Component->GetWorldInverseMatrix().TransformPositionWithW(P);
	case EParticleValueSpace::World:
		if (!UsesLocalSpace() || !Component)
		{
			return P;
		}

		return Component->GetWorldMatrix().TransformPositionWithW(P);
	default:
		return P;
	}
}

void FParticleEmitterInstance::SetCurrentLODIndex(int32 InLODIndex)
{
	CurrentLODIndex = std::max(0, InLODIndex);
}

int32 FParticleEmitterInstance::GetParticleSimulationLODIndex(const FBaseParticle& Particle) const
{
	if (!Emitter)
	{
		return 0;
	}

	const int32 LODCount = std::max(Emitter->GetLODCount(), 1);
	return std::clamp(static_cast<int32>(Particle.SimulationLODIndex), 0, LODCount - 1);
}

UParticleLODLevel* FParticleEmitterInstance::GetParticleSimulationLOD(const FBaseParticle& Particle) const
{
	if (!Emitter)
	{
		return nullptr;
	}

	return Emitter->GetCurrentLODLevel(GetParticleSimulationLODIndex(Particle));
}

bool FParticleEmitterInstance::IsSpawningComplete() const
{
	const UParticleModuleRequired* Required = GetRequiredModule();
	if (!Required)
	{
		return false;
	}

	if (Required->EmitterDuration <= 0.0f || Required->EmitterLoops <= 0)
	{
		return false;
	}

	return LoopCount >= Required->EmitterLoops;
}

bool FParticleEmitterInstance::IsFinished() const
{
	// halt(graceful deactivate) 시엔 더 이상 spawn이 없으므로 입자가 0이면 완료로 본다.
	return (bHaltSpawning || IsSpawningComplete()) && ActiveParticles == 0;
}

bool FParticleEmitterInstance::ComputeDynamicBounds(FVector& OutMin, FVector& OutMax) const
{
	if (ActiveParticles == 0)
	{
		return false;
	}

	const UParticleModuleRequired* Required = GetRequiredModule();
	const bool bUseLocalSpace = Required ? Required->bUseLocalSpace : false;
	const FMatrix LocalToWorld = Component ? Component->GetWorldMatrix() : FMatrix{};

	bool bHasBounds = false;
	// 현재는 sprite/mesh 공통으로 Location +/- Size 의 느슨한 근사 박스를 사용.
	// culling/selection 용으로는 충분하고, 정확한 mesh bounds는 추후 확장 가능.
	for (uint32 i = 0; i < ActiveParticles; ++i)
	{
		const FBaseParticle* Particle = GetParticleAt(i);
		if (!Particle)
		{
			continue;
		}

		FVector Position = Particle->Location;
		if (bUseLocalSpace && Component)
		{
			Position = LocalToWorld.TransformPositionWithW(Position);
		}

		const FVector Extent{
			std::abs(Particle->Size.X),
			std::abs(Particle->Size.Y),
			std::abs(Particle->Size.Z)
		};

		const FVector ParticleMin = Position - Extent;
		const FVector ParticleMax = Position + Extent;

		if (!bHasBounds)
		{
			OutMin = ParticleMin;
			OutMax = ParticleMax;
			bHasBounds = true;
			continue;
		}

		OutMin.X = std::min(OutMin.X, ParticleMin.X);
		OutMin.Y = std::min(OutMin.Y, ParticleMin.Y);
		OutMin.Z = std::min(OutMin.Z, ParticleMin.Z);
		OutMax.X = std::max(OutMax.X, ParticleMax.X);
		OutMax.Y = std::max(OutMax.Y, ParticleMax.Y);
		OutMax.Z = std::max(OutMax.Z, ParticleMax.Z);
	}

	return bHasBounds;
}

FTransform FParticleEmitterInstance::GetComponentToWorld() const
{
	if (!Component) return FTransform{};
	return FTransform(Component->GetWorldMatrix());
}

void FParticleEmitterInstance::ClearPendingEvents()
{
	SpawnEvents.clear();
	DeathEvents.clear();
	CollisionEvents.clear();
	BurstEvents.clear();
}

void FParticleEmitterInstance::EnqueueSpawnEvent(const FParticleEventSpawnData& InEvent)
{
	SpawnEvents.push_back(InEvent);
}

void FParticleEmitterInstance::EnqueueDeathEvent(const FParticleEventDeathData& InEvent)
{
	DeathEvents.push_back(InEvent);
}

void FParticleEmitterInstance::EnqueueCollisionEvent(const FParticleEventCollideData& InEvent)
{
	CollisionEvents.push_back(InEvent);
}

void FParticleEmitterInstance::EnqueueBurstEvent(const FParticleEventBurstData& InEvent)
{
	BurstEvents.push_back(InEvent);
}

void FParticleEmitterInstance::EmitSpawnEvent(const FParticleEventSpawnData& InEvent)
{
	EnqueueSpawnEvent(InEvent);

	ForEachEventGenerator([this, &InEvent](const UParticleModuleEventGenerator* EventGenerator)
		{
			EventGenerator->HandleSpawnEvent(this, InEvent);
		});
}

void FParticleEmitterInstance::EmitDeathEvent(const FParticleEventDeathData& InEvent)
{
	EnqueueDeathEvent(InEvent);

	ForEachEventGenerator([this, &InEvent](const UParticleModuleEventGenerator* EventGenerator)
		{
			EventGenerator->HandleDeathEvent(this, InEvent);
		});
}

void FParticleEmitterInstance::EmitCollisionEvent(const FParticleEventCollideData& InEvent)
{
	EnqueueCollisionEvent(InEvent);

	ForEachEventGenerator([this, &InEvent](const UParticleModuleEventGenerator* EventGenerator)
		{
			EventGenerator->HandleCollisionEvent(this, InEvent);
		});
}

void FParticleEmitterInstance::EmitBurstEvent(const FParticleEventBurstData& InEvent)
{
	EnqueueBurstEvent(InEvent);

	ForEachEventGenerator([this, &InEvent](const UParticleModuleEventGenerator* EventGenerator)
		{
			EventGenerator->HandleBurstEvent(this, InEvent);
		});
}

void FParticleEmitterInstance::ApplyEventSpawnOverride(
	FBaseParticle& Particle,
	const FParticleEventSpawnOverride& SpawnOverride) const
{
	if (SpawnOverride.bUseLocation)
	{
		Particle.Location = ConvertPositionToSimulation(
			SpawnOverride.LocationWorld,
			EParticleValueSpace::World);
		Particle.OldLocation = Particle.Location;
	}

	if (SpawnOverride.bInheritVelocity)
	{
		const FVector EventVelocity = ConvertVectorToSimulation(
			SpawnOverride.VelocityWorld * SpawnOverride.InheritVelocityScale,
			EParticleValueSpace::World);

		Particle.Velocity = Particle.Velocity + EventVelocity;
		Particle.BaseVelocity = Particle.Velocity;
	}
}

int32 FParticleEmitterInstance::SpawnFromEvent(
	int32 Count,
	const FParticleEventSpawnOverride& SpawnOverride)
{
	if (Count <= 0)
	{
		return 0;
	}

	return SpawnInternal(
		Count,
		CurrentLoopTimeSeconds,
		0.0f,
		0.0f,
		&SpawnOverride);
}

void FParticleEmitterInstance::KillAllParticles(bool bStopSpawning)
{
	ActiveParticles = 0;
	if (bStopSpawning)
	{
		bHaltSpawning = true;
	}
}

void FParticleEmitterInstance::HandleReceivedEvent(const FParticleEventDataBase& Event)
{
	UParticleLODLevel* LOD = GetCurrentLOD();
	if (!LOD || !LOD->bEnabled)
	{
		return;
	}

	for (UParticleModule* Module : LOD->Modules)
	{
		if (!Module || !Module->IsEnabled())
		{
			continue;
		}

		if (const auto* ReceiverSpawn = Cast<UParticleModuleEventReceiverSpawn>(Module))
		{
			ReceiverSpawn->ReceiveEvent(this, Event);
			continue;
		}

		if (const auto* ReceiverKillAll = Cast<UParticleModuleEventReceiverKillAll>(Module))
		{
			ReceiverKillAll->ReceiveEvent(this, Event);
			continue;
		}
	}
}

void FParticleEmitterInstance::HandleReceivedEvents(
	const TArray<FParticleEventSpawnData>& InSpawnEvents,
	const TArray<FParticleEventDeathData>& InDeathEvents,
	const TArray<FParticleEventCollideData>& InCollisionEvents,
	const TArray<FParticleEventBurstData>& InBurstEvents)
{
	for (const FParticleEventSpawnData& Event : InSpawnEvents)
	{
		HandleReceivedEvent(Event);
	}
	for (const FParticleEventDeathData& Event : InDeathEvents)
	{
		HandleReceivedEvent(Event);
	}
	for (const FParticleEventCollideData& Event : InCollisionEvents)
	{
		HandleReceivedEvent(Event);
	}
	for (const FParticleEventBurstData& Event : InBurstEvents)
	{
		HandleReceivedEvent(Event);
	}
}

void FParticleEmitterInstance::SpawnParticles(float DeltaTime)
{
	UParticleLODLevel* LOD = GetCurrentLOD();
	if (!LOD || !LOD->bEnabled) return;
	if (!LOD->SpawnModule) return;
	if (!IsSpawningAllowed()) return;

	UParticleModuleSpawn* SpawnModule = LOD->SpawnModule;

	float SpawnAmount = 0.0f;

	// SpawnTime은 particle relative time이 아니라 현재 emitter loop 안의 시간이다.
	// SpawnModule은 Rate/RateScale 계산만 맡고, BurstList의 실제 firing은 emitter instance가 처리한다.
	SpawnModule->GetRateSpawnAmount(
		this,
		DeltaTime,
		CurrentLoopTimeSeconds,
		SpawnAmount);

	SpawnAmount = std::max(0.0f, SpawnAmount);

	const float TotalSpawnFloat = SpawnFraction + SpawnAmount;
	const int32 RateSpawnCount = static_cast<int32>(std::floor(TotalSpawnFloat));
	SpawnFraction = TotalSpawnFloat - static_cast<float>(RateSpawnCount);

	int32 SpawnBudget = static_cast<int32>(
		ParticleConstants::MaxParticlesPerEmitter > ActiveParticles
		? ParticleConstants::MaxParticlesPerEmitter - ActiveParticles
		: 0);

	if (SpawnBudget <= 0) return;

	SpawnBudget = std::min(
		SpawnBudget,
		static_cast<int32>(ParticleConstants::MaxBurstCountPerFrame));

	// Burst는 정해진 loop time에 터지는 이벤트성이 강하므로 Rate보다 먼저 처리한다.
	SpawnBurstParticles(SpawnModule, DeltaTime, SpawnBudget);

	if (RateSpawnCount > 0 && SpawnBudget > 0)
	{
		const int32 Count = std::min(RateSpawnCount, SpawnBudget);

		const float Increment =
			Count > 0
			? DeltaTime / static_cast<float>(Count)
			: 0.0f;

		// 각 spawn 구간의 중앙점에 배치한다.
		// 예: Count=4, Delta=0.016 -> 0.002, 0.006, 0.010, 0.014초 지점.
		const float StartTime = CurrentLoopTimeSeconds + Increment * 0.5f;

		SpawnInternal(Count, StartTime, Increment, DeltaTime);
	}
}

int32 FParticleEmitterInstance::SpawnBurstParticles(UParticleModuleSpawn* SpawnModule, float DeltaTime, int32& InOutSpawnBudget)
{
	if (!SpawnModule || InOutSpawnBudget <= 0)
	{
		return 0;
	}

	int32 TotalSpawned = 0;

	const float SafeDeltaTime = std::max(0.0f, DeltaTime);
	const float CurrentTime = CurrentLoopTimeSeconds + SafeDeltaTime;
	float PreviousTime = CurrentLoopTimeSeconds;

	if (UParticleModuleSpawn::FSpawnModuleInstancePayload* Payload =
		GetModuleInstancePayload<UParticleModuleSpawn::FSpawnModuleInstancePayload>(SpawnModule))
	{
		PreviousTime = Payload->LastProcessedBurstTime;
		if (CurrentTime < PreviousTime)
		{
			PreviousTime = CurrentLoopTimeSeconds;
		}

		Payload->LastProcessedBurstTime = CurrentTime;
	}

	for (const FBurstEntry& Entry : SpawnModule->BurstList)
	{
		if (InOutSpawnBudget <= 0) break;

		if (Entry.Count <= 0)
		{
			continue;
		}

		if (Entry.Time < PreviousTime || Entry.Time >= CurrentTime)
		{
			continue;
		}

		const int32 Count = std::min(Entry.Count, InOutSpawnBudget);
		if (Count <= 0) continue;

		const int32 SpawnedCount = SpawnInternal(Count, Entry.Time, 0.0f, SafeDeltaTime);
		InOutSpawnBudget -= SpawnedCount;
		TotalSpawned += SpawnedCount;

		if (SpawnedCount > 0)
		{
			const float BurstOffsetSeconds = std::clamp(
				Entry.Time - CurrentLoopTimeSeconds,
				0.0f,
				SafeDeltaTime);

			FParticleEventBurstData BurstEvent;
			BurstEvent.Type = EParticleEventType::Burst;
			BurstEvent.TimeSeconds = EmitterTimeSeconds + BurstOffsetSeconds;
			BurstEvent.ParticleCount = SpawnedCount;

			if (Component)
			{
				BurstEvent.Location = Component->GetWorldLocation();
			}

			EmitBurstEvent(BurstEvent);
		}
	}

	return TotalSpawned;
}

int32 FParticleEmitterInstance::SpawnInternal(
	int32 Count,
	float StartTime,
	float Increment,
	float StepDeltaTime,
	const FParticleEventSpawnOverride* SpawnOverride)
{
	if (Count <= 0) return 0;

	UParticleLODLevel* LOD = GetCurrentLOD();
	if (!LOD || !LOD->bEnabled) return 0;

	const uint32 RequiredActiveCount = ActiveParticles + static_cast<uint32>(Count);
	if (RequiredActiveCount > MaxActiveParticles)
	{
		const uint32 NewCapacity = GrowParticleCapacity(MaxActiveParticles, RequiredActiveCount);
		if (NewCapacity <= MaxActiveParticles) return 0;

		ResizeParticleData(NewCapacity);
	}

	const uint32 ActiveFlag = static_cast<uint32>(EParticleStateFlags::Active);
	const uint32 SpawnedFlag = static_cast<uint32>(EParticleStateFlags::Spawned);

	int32 SpawnedCount = 0;

	for (int32 SpawnIndex = 0; SpawnIndex < Count; ++SpawnIndex)
	{
		if (ActiveParticles >= MaxActiveParticles) break;

		const float SpawnTime = StartTime + Increment * static_cast<float>(SpawnIndex);
		const float SpawnOffsetSeconds = std::clamp(
			SpawnTime - CurrentLoopTimeSeconds,
			0.0f,
			StepDeltaTime);
		const float AbsoluteSpawnTime = EmitterTimeSeconds + SpawnOffsetSeconds;

		const uint32 Slot = ActiveParticles;
		uint8* ParticleBytes = RuntimeStorage.ParticleData + Slot * ParticleStride;

		std::memset(ParticleBytes, 0, ParticleStride);

		FBaseParticle* Particle = reinterpret_cast<FBaseParticle*>(ParticleBytes);
		*Particle = FBaseParticle{};

		Particle->Flags = ActiveFlag | SpawnedFlag;
		// CurrentLODIndex still chooses the LOD contract for new particles, but
		// already-live particles keep their spawn-time simulation LOD afterward.
		Particle->SimulationLODIndex = static_cast<uint8>(std::max(0, CurrentLODIndex));

		bool bUseLocalSpace = false;

		if (LOD && LOD->RequiredModule)
		{
			bUseLocalSpace = LOD->RequiredModule->bUseLocalSpace;
		}

		if (Component)
		{
			if (bUseLocalSpace) Particle->Location = FVector{ 0, 0, 0 };
			else Particle->Location = Component->GetWorldLocation();

			Particle->OldLocation = Particle->Location;
		}

		RuntimeStorage.ParticleIndices[ActiveParticles] = static_cast<uint16>(Slot);
		++ActiveParticles;
		++SpawnedCount;

		for (UParticleModule* Module : LOD->Modules)
		{
			if (!Module || !Module->IsEnabled()) continue;

			const uint32 ModuleOffset = GetModuleDataOffset(Module);
			// SpawnTime은 emitter-loop 기준 seconds이다.
			// Initial Distribution은 이 값을 쓰고, Over-Life 모듈은 Particle->RelativeTime을 쓴다.
			Module->Spawn(this, ModuleOffset, SpawnTime, Particle);
		}

		if (SpawnOverride)
		{
			ApplyEventSpawnOverride(*Particle, *SpawnOverride);
		}

		FParticleEventSpawnData SpawnEvent;
		SpawnEvent.Type = EParticleEventType::Spawn;
		SpawnEvent.TimeSeconds = AbsoluteSpawnTime;
		SpawnEvent.Location = ConvertPositionFromSimulation(Particle->Location, EParticleValueSpace::World);
		SpawnEvent.Velocity = ConvertVectorFromSimulation(Particle->Velocity, EParticleValueSpace::World);
		SpawnEvent.ParticleCount = 1;
		EmitSpawnEvent(SpawnEvent);

		// 현재 tick 순서는 Spawn -> Update다.
		// sub-frame spawn을 흉내 내기 위해 spawn 이전 시간만큼 위치를 뒤로 보정한다.
		if (SpawnOffsetSeconds > 0.0f)
		{
			Particle->Location = Particle->Location - Particle->Velocity * SpawnOffsetSeconds;
			Particle->OldLocation = Particle->Location;
		}
	}

	PARTICLE_STATS_ADD_SPAWN(SpawnedCount);
	return SpawnedCount;
}

void FParticleEmitterInstance::UpdateParticles(float DeltaTime)
{
	if (!Emitter)
	{
		return;
	}

	for (uint32 i = 0; i < ActiveParticles; ++i)
	{
		FBaseParticle* Particle = GetParticleAt(i);
		if (!Particle) continue;

		Particle->OldLocation = Particle->Location;
		Particle->Location = Particle->Location + Particle->Velocity * DeltaTime;
		Particle->RelativeTime += DeltaTime * Particle->OneOverMaxLifetime;
	}

	TArray<TArray<uint32>> ActiveParticleBuckets;
	BuildActiveParticleSimulationLODBuckets(ActiveParticleBuckets);

	for (int32 SimulationLODIndex = 0;
		SimulationLODIndex < static_cast<int32>(ActiveParticleBuckets.size());
		++SimulationLODIndex)
	{
		const TArray<uint32>& ParticleIndices = ActiveParticleBuckets[SimulationLODIndex];
		if (ParticleIndices.empty())
		{
			continue;
		}

		UParticleLODLevel* SimulationLOD = Emitter->GetCurrentLODLevel(SimulationLODIndex);
		if (!SimulationLOD || !SimulationLOD->bEnabled)
		{
			continue;
		}

		for (UParticleModule* Module : SimulationLOD->Modules)
		{
			if (!Module || !Module->IsEnabled())
			{
				continue;
			}

			const uint32 ModuleOffset = GetModuleDataOffset(Module);
			Module->UpdateParticleSubset(this, SimulationLOD, ModuleOffset, DeltaTime, ParticleIndices);
		}
	}

	ResolveParticleCollisions(DeltaTime, ActiveParticleBuckets);

#if STATS
	// collision 처리 후 캡처 → kill 카운트에 collision 사망 입자까지 포함.
	const uint32 PreUpdateActiveParticles = ActiveParticles;
#endif
	uint32 WriteIndex = 0;

	for (uint32 ReadIndex = 0; ReadIndex < ActiveParticles; ++ReadIndex)
	{
		FBaseParticle* Particle = GetParticleAt(ReadIndex);
		if (!Particle) continue;

		if (IsParticleKilled(Particle))
		{
			FParticleEventDeathData DeathEvent;
			DeathEvent.Type = EParticleEventType::Death;
			DeathEvent.TimeSeconds = EmitterTimeSeconds;
			DeathEvent.Location = ConvertPositionFromSimulation(Particle->Location, EParticleValueSpace::World);
			DeathEvent.Velocity = ConvertVectorFromSimulation(Particle->Velocity, EParticleValueSpace::World);
			DeathEvent.ParticleAge = Particle->RelativeTime;
			EmitDeathEvent(DeathEvent);

			continue;
		}

		ClearSpawnedFlag(Particle);

		if (WriteIndex != ReadIndex)
		{
			FBaseParticle* WriteParticle =
				reinterpret_cast<FBaseParticle*>(RuntimeStorage.ParticleData + WriteIndex * ParticleStride);

			std::memmove(WriteParticle, Particle, ParticleStride);
		}

		RuntimeStorage.ParticleIndices[WriteIndex] = static_cast<uint16>(WriteIndex);
		++WriteIndex;
	}

	ActiveParticles = WriteIndex;

#if STATS
	// 이번 Update에서 사라진 입자 = 압축 전 활성 수 - 생존 수.
	PARTICLE_STATS_ADD_KILL(PreUpdateActiveParticles - WriteIndex);
#endif
}

void FParticleEmitterInstance::ResolveParticleCollisions(
	float DeltaTime,
	const TArray<TArray<uint32>>& ActiveParticleBuckets)
{
	// Collision has two layers that intentionally coexist:
	//   1) outer policy        : current-emitter-LOD workload decisions
	//                            (budget / pruning / nearby context / event gating)
	//   2) simulation contract : particle spawn-time collision module identity,
	//                            payload offset, accepted-hit response/completion
	//
	// This function first resolves the outer policy for "how much collision work
	// should this emitter spend this tick?", then uses each particle's simulation
	// LOD contract to decide what a collision means when that work is actually done.
	UWorld* World = Component ? Component->GetWorld() : nullptr;
	if (!World || ActiveParticles == 0)
	{
		return;
	}

	if (!ShouldProcessCollisionsForCurrentLOD())
	{
		FParticleCollisionDebugStats DebugStats;
		InitializeCollisionOuterPolicyDebugStats(DebugStats, /*CollisionBudget*/ 0);
		DebugStats.bCollisionFullyDisabledForLOD = true;
		DebugLogParticleCollisionStats(DebugStats);
		return;
	}

	if (!ModuleOffsetMap)
	{
		return;
	}

	const int32 CollisionBudget = GetCollisionCheckBudgetForCurrentLOD();
	if (CollisionBudget <= 0)
	{
		FParticleCollisionDebugStats DebugStats;
		InitializeCollisionOuterPolicyDebugStats(DebugStats, CollisionBudget);
		DebugLogParticleCollisionStats(DebugStats);
		return;
	}

	FParticleCollisionDebugStats DebugStats;
	InitializeCollisionOuterPolicyDebugStats(DebugStats, CollisionBudget);

	// Outer policy needs some collision settings even before it starts spending
	// per-particle query budget. We prefer the current-emitter-LOD module, but
	// can fall back to an active simulation-LOD module when the current policy LOD
	// itself does not carry collision authoring.
	const UParticleModuleCollision* OuterCollisionModule =
		ResolveCollisionOuterPolicyModule(ActiveParticleBuckets);

	if (!OuterCollisionModule)
	{
		DebugLogParticleCollisionStats(DebugStats);
		return;
	}

	// Emitter-level pruning is a coarse "should we spend collision work on this
	// emitter at all this tick?" gate. It sits outside per-particle budget
	// prioritization, repeated-hit suppression, and event policy.
	if (ShouldPruneEmitterCollisionByBounds(*OuterCollisionModule, &DebugStats))
	{
		DebugLogParticleCollisionStats(DebugStats);
		return;
	}

	// Nearby-collider preselection is a short-lived context snapshot, not final
	// hit authority. It runs after emitter-level pruning and before per-particle
	// candidate selection so the emitter has a conservative sense of nearby scene
	// collision relevance without redefining query/event/response semantics.
	RefreshEmitterNearbyCollisionCache(*OuterCollisionModule, &DebugStats);

	TArray<uint32> HighPriorityCandidates;
	TArray<uint32> FallbackCandidates;
	HighPriorityCandidates.reserve(ActiveParticles);
	FallbackCandidates.reserve(ActiveParticles);

	for (int32 SimulationLODIndex = 0;
		SimulationLODIndex < static_cast<int32>(ActiveParticleBuckets.size());
		++SimulationLODIndex)
	{
		const TArray<uint32>& ParticleIndices = ActiveParticleBuckets[SimulationLODIndex];
		if (ParticleIndices.empty())
		{
			continue;
		}

		UParticleLODLevel* SimulationLOD = Emitter ? Emitter->GetCurrentLODLevel(SimulationLODIndex) : nullptr;
		const UParticleModuleCollision* CollisionModule = GetCollisionModule(SimulationLOD);
		if (!CollisionModule)
		{
			continue;
		}

		// From this point on we are no longer deciding "should this emitter spend
		// collision work at all?" That policy step is already done above. Here we
		// are interpreting each particle under its simulation-LOD collision module
		// identity and payload layout.
		const auto OffsetIt = ModuleOffsetMap->find(CollisionModule);
		if (OffsetIt == ModuleOffsetMap->end())
		{
			continue;
		}

		const uint32 CollisionModuleOffset = OffsetIt->second;
		ForEachParticleSubset(ParticleIndices, [this, CollisionModule, CollisionModuleOffset, DeltaTime, &HighPriorityCandidates, &FallbackCandidates, &DebugStats](uint32 ActiveIndex, FBaseParticle& Particle)
		{
			// Budget is no longer "first N particles win". We first remove obvious
			// no-query cases, then spend the limited collision work on the more
			// meaningful movers before falling back to lower-value candidates.
			if (FinalizeParticleCollisionWithoutQuery(Particle, *CollisionModule, CollisionModuleOffset))
			{
				++DebugStats.SkippedByState;
				return;
			}

			if (ShouldSkipParticleCollisionForBudget(Particle, *CollisionModule, CollisionModuleOffset, DeltaTime))
			{
				++DebugStats.SkippedByEarlyOut;
				return;
			}

			const float PriorityScore =
				GetParticleCollisionPriorityScore(Particle, *CollisionModule, CollisionModuleOffset, DeltaTime);

			if (IsHighPriorityCollisionCandidate(PriorityScore))
			{
				HighPriorityCandidates.push_back(ActiveIndex);
				++DebugStats.HighPriorityCandidateCount;
				return;
			}

			FallbackCandidates.push_back(ActiveIndex);
			++DebugStats.FallbackCandidateCount;
		});
	}

	int32 CollisionChecksRemaining = CollisionBudget;
	auto ProcessCollisionCandidates = [this, DeltaTime, &CollisionChecksRemaining, &DebugStats](const TArray<uint32>& CandidateIndices)
		{
			for (uint32 ActiveIndex : CandidateIndices)
			{
				if (CollisionChecksRemaining <= 0)
				{
					return;
				}

				FBaseParticle* Particle = GetParticleAt(ActiveIndex);
				if (!Particle)
				{
					continue;
				}

				UParticleLODLevel* SimulationLOD = GetParticleSimulationLOD(*Particle);
				const UParticleModuleCollision* CollisionModule = GetCollisionModule(SimulationLOD);
				if (!CollisionModule)
				{
					continue;
				}

				if (!ModuleOffsetMap)
				{
					continue;
				}

				const auto OffsetIt = ModuleOffsetMap->find(CollisionModule);
				if (OffsetIt == ModuleOffsetMap->end())
				{
					continue;
				}

				const uint32 CollisionModuleOffset = OffsetIt->second;
				if (FinalizeParticleCollisionWithoutQuery(*Particle, *CollisionModule, CollisionModuleOffset))
				{
					++DebugStats.SkippedByState;
					continue;
				}

				--CollisionChecksRemaining;
				++DebugStats.QueriedCount;
				ResolveSingleParticleCollision(
					*Particle,
					*CollisionModule,
					CollisionModuleOffset,
					DeltaTime,
					&DebugStats);
			}
		};

	ProcessCollisionCandidates(HighPriorityCandidates);
	// Any remaining candidates beyond the current budget are "not processed this
	// tick" because of budget policy. That is distinct from processing a hit and
	// then suppressing its event as repeated-contact noise or lower-LOD gating.
	ProcessCollisionCandidates(FallbackCandidates);

	DebugStats.CandidateCount =
		DebugStats.HighPriorityCandidateCount + DebugStats.FallbackCandidateCount;
	DebugStats.SkippedByBudget =
		std::max(0, DebugStats.CandidateCount - DebugStats.QueriedCount);

	DebugLogParticleCollisionStats(DebugStats);
}

const UParticleModuleCollision* FParticleEmitterInstance::GetCollisionModule() const
{
	return GetCollisionModule(GetCurrentLOD());
}

const UParticleModuleCollision* FParticleEmitterInstance::GetCollisionModule(const UParticleLODLevel* LOD) const
{
	if (!LOD)
	{
		return nullptr;
	}

	for (UParticleModule* Module : LOD->Modules)
	{
		if (!Module || !Module->IsEnabled())
		{
			continue;
		}

		if (const auto* CollisionModule = Cast<UParticleModuleCollision>(Module))
		{
			return CollisionModule;
		}
	}

	return nullptr;
}

const UParticleModuleCollision* FParticleEmitterInstance::ResolveCollisionOuterPolicyModule(
	const TArray<TArray<uint32>>& ActiveParticleBuckets) const
{
	// Outer collision policy is keyed off the emitter's current LOD view first,
	// because budget/pruning/event gating are current-emitter-level concerns.
	if (const UParticleModuleCollision* CurrentLODModule = GetCollisionModule())
	{
		return CurrentLODModule;
	}

	// If the current LOD does not define collision authoring, policy still needs a
	// conservative collision settings source for emitter-level pruning/nearby-cache
	// decisions. Fall back to the first active simulation-LOD module rather than
	// pretending collision policy has no module context at all.
	for (int32 SimulationLODIndex = 0;
		SimulationLODIndex < static_cast<int32>(ActiveParticleBuckets.size());
		++SimulationLODIndex)
	{
		if (ActiveParticleBuckets[SimulationLODIndex].empty())
		{
			continue;
		}

		UParticleLODLevel* SimulationLOD = Emitter ? Emitter->GetCurrentLODLevel(SimulationLODIndex) : nullptr;
		if (const UParticleModuleCollision* SimulationModule = GetCollisionModule(SimulationLOD))
		{
			return SimulationModule;
		}
	}

	return nullptr;
}

FParticleEmitterInstance::FResolvedCollisionOuterPolicy
FParticleEmitterInstance::ResolveCollisionOuterPolicyForCurrentLOD() const
{
	FResolvedCollisionOuterPolicy ResolvedPolicy;
	ResolvedPolicy.bCollisionFullyDisabled =
		ResolveDefaultCollisionDisablePolicyForLOD(CurrentLODIndex);
	ResolvedPolicy.bEmitCollisionEvents =
		ResolveDefaultCollisionEventPolicyForLOD(CurrentLODIndex);
	ResolvedPolicy.CollisionQueryBudget =
		ResolveDefaultCollisionBudgetForLOD(CurrentLODIndex);

	// Current-emitter-LOD remains authoritative for outer collision workload
	// policy. Simulation-LOD collision modules still define per-particle payload
	// and hit meaning, but they do not override this emitter-level policy layer.
	const UParticleModuleCollision* CurrentLODModule = GetCollisionModule();
	if (!CurrentLODModule)
	{
		return ResolvedPolicy;
	}

	const FParticleCollisionLODPolicyOverride& PolicyOverride =
		CurrentLODModule->LODPolicyOverride;
	if (!PolicyOverride.bEnabled)
	{
		return ResolvedPolicy;
	}

	ResolvedPolicy.bUsingAuthoringOverride = true;
	ResolvedPolicy.CollisionQueryBudget = std::max(0, PolicyOverride.CollisionQueryBudget);

	if (PolicyOverride.bOverrideDisablePolicy)
	{
		ResolvedPolicy.bCollisionFullyDisabled = PolicyOverride.bDisableCollisionQueries;
	}

	if (PolicyOverride.bOverrideCollisionEventPolicy)
	{
		ResolvedPolicy.bEmitCollisionEvents = PolicyOverride.bAllowCollisionEvents;
	}

	return ResolvedPolicy;
}

void FParticleEmitterInstance::InitializeCollisionOuterPolicyDebugStats(
	FParticleCollisionDebugStats& OutStats,
	int32 CollisionBudget) const
{
	const FResolvedCollisionOuterPolicy ResolvedPolicy =
		ResolveCollisionOuterPolicyForCurrentLOD();
	OutStats.ActiveParticles = static_cast<int32>(ActiveParticles);
	OutStats.CurrentLODIndex = CurrentLODIndex;
	OutStats.EffectiveBudget = CollisionBudget;
	OutStats.bCollisionFullyDisabledForLOD = ResolvedPolicy.bCollisionFullyDisabled;
	OutStats.bCollisionEventGatedForLOD = !ResolvedPolicy.bEmitCollisionEvents;
	OutStats.bUsingCollisionOuterPolicyOverride = ResolvedPolicy.bUsingAuthoringOverride;
}

bool FParticleEmitterInstance::FinalizeParticleCollisionWithoutQuery(
	FBaseParticle& Particle,
	const UParticleModuleCollision& CollisionModule,
	uint32 ModuleOffset) const
{
	const uint32 KilledFlag = static_cast<uint32>(EParticleStateFlags::Killed);
	if ((Particle.Flags & KilledFlag) != 0)
	{
		return true;
	}

	auto* Payload =
		PARTICLE_PAYLOAD(&Particle, ModuleOffset, UParticleModuleCollision::FCollisionParticlePayload);
	if (!Payload)
	{
		return true;
	}

	Payload->NumCollisions = std::max(0, Payload->NumCollisions);
	if (Payload->bFrozenAfterLimit)
	{
		// Freeze is a long-lived completion state. We keep restoring it without
		// spending query budget so low-value settled particles do not crowd out
		// faster, still-meaningful collision candidates.
		Particle.Location = Particle.OldLocation;
		Particle.Velocity = FVector::ZeroVector;
		return true;
	}

	if (Payload->bIgnoreFurtherCollisions)
	{
		return true;
	}

	if (HasCollisionCountLimit(CollisionModule) && Payload->NumCollisions >= CollisionModule.MaxCollisions)
	{
		ApplyCollisionCompletionBehavior(Particle, *Payload, CollisionModule);
		return true;
	}

	return false;
}

bool FParticleEmitterInstance::ShouldSkipParticleCollisionForBudget(
	const FBaseParticle& Particle,
	const UParticleModuleCollision& CollisionModule,
	uint32 ModuleOffset,
	float DeltaTime) const
{
	const auto* Payload =
		PARTICLE_PAYLOAD_CONST(&Particle, ModuleOffset, UParticleModuleCollision::FCollisionParticlePayload);
	if (!Payload)
	{
		return true;
	}

	const FVector StartWorld = ConvertPositionFromSimulation(Particle.OldLocation, EParticleValueSpace::World);
	const FVector EndWorld = ConvertPositionFromSimulation(Particle.Location, EParticleValueSpace::World);
	const float TravelDistance = (EndWorld - StartWorld).Length();
	if (TravelDistance <= ParticleCollisionMinTravelDistance)
	{
		return true;
	}

	const float CollisionSpeed = GetCollisionSpeed(
		ConvertVectorFromSimulation(Particle.Velocity, EParticleValueSpace::World));
	if (!IsMeaningfulCollisionSpeed(CollisionSpeed) &&
		WasCollisionHandledRecently(*Payload, EmitterTimeSeconds + DeltaTime))
	{
		// Very low-speed particles that just interacted recently are likely to hit
		// the repeated-contact stabilizer again. Skip spending scarce query budget
		// on them so faster movers get first claim.
		return true;
	}

	if (HasCollisionCountLimit(CollisionModule))
	{
		const int32 RemainingCollisions = CollisionModule.MaxCollisions - Payload->NumCollisions;
		if (RemainingCollisions <= 0)
		{
			return true;
		}
	}

	return false;
}

float FParticleEmitterInstance::GetParticleCollisionPriorityScore(
	const FBaseParticle& Particle,
	const UParticleModuleCollision& CollisionModule,
	uint32 ModuleOffset,
	float DeltaTime) const
{
	const auto* Payload =
		PARTICLE_PAYLOAD_CONST(&Particle, ModuleOffset, UParticleModuleCollision::FCollisionParticlePayload);
	if (!Payload)
	{
		return 0.0f;
	}

	const FVector StartWorld = ConvertPositionFromSimulation(Particle.OldLocation, EParticleValueSpace::World);
	const FVector EndWorld = ConvertPositionFromSimulation(Particle.Location, EParticleValueSpace::World);
	const float TravelDistance = (EndWorld - StartWorld).Length();
	const float CollisionSpeed = GetCollisionSpeed(
		ConvertVectorFromSimulation(Particle.Velocity, EParticleValueSpace::World));

	float PriorityScore =
		TravelDistance + (CollisionSpeed * ParticleCollisionBudgetPrioritySpeedWeight);
	// Current LOD still decides the outer collision budget envelope. This score
	// only decides how to spend that remaining budget more selectively inside the
	// explicit CPU collision pass.

	if (WasCollisionHandledRecently(*Payload, EmitterTimeSeconds + DeltaTime))
	{
		PriorityScore *= ParticleCollisionBudgetRecentHitPenalty;
	}

	if (!IsMeaningfulCollisionSpeed(CollisionSpeed))
	{
		PriorityScore *= ParticleCollisionBudgetLowSpeedPenalty;
	}

	if (HasCollisionCountLimit(CollisionModule))
	{
		const int32 RemainingCollisions = CollisionModule.MaxCollisions - Payload->NumCollisions;
		if (RemainingCollisions <= 1)
		{
			PriorityScore *= ParticleCollisionBudgetNearLimitPenalty;
		}
	}

	return PriorityScore;
}

bool FParticleEmitterInstance::IsHighPriorityCollisionCandidate(float PriorityScore) const
{
	return PriorityScore >= ParticleCollisionHighPriorityScoreThreshold;
}

bool FParticleEmitterInstance::ShouldEmitCollisionEventForAcceptedHit(
	const UParticleModuleCollision& CollisionModule) const
{
	// Event eligibility is a reporting policy layer on top of accepted collision
	// processing. A collision can still be processed even when the event report is
	// gated off by module settings or lower-LOD event fidelity policy.
	return CollisionModule.bGenerateCollisionEvents &&
		ShouldEmitCollisionEventsForCurrentLOD();
}

void FParticleEmitterInstance::EmitCollisionEventForAcceptedHit(
	const FBaseParticle& Particle,
	const FVector& CollisionNormal,
	const FVector& ImpactVelocityWorld,
	const FHitResult& Hit,
	float CollisionTimeSeconds)
{
	FParticleEventCollideData CollisionEvent;
	CollisionEvent.Type = EParticleEventType::Collision;
	CollisionEvent.TimeSeconds = CollisionTimeSeconds;
	CollisionEvent.Location = Hit.WorldHitLocation;
	CollisionEvent.Velocity = ConvertVectorFromSimulation(Particle.Velocity, EParticleValueSpace::World);
	CollisionEvent.Normal = CollisionNormal;
	CollisionEvent.ImpactVelocity = ImpactVelocityWorld;
	CollisionEvent.Item = Hit.FaceIndex;
	EmitCollisionEvent(CollisionEvent);
}

bool FParticleEmitterInstance::BuildParticleCollisionQuerySegment(
	const FBaseParticle& Particle,
	FVector& OutStartWorld,
	FVector& OutTravelDirection,
	float& OutTravelDistance) const
{
	// This is the current point-like particle query strategy: a world-space
	// segment from OldLocation to Location. Future thicker/sweep-style queries
	// should replace or extend this helper instead of changing response policy.
	OutStartWorld = ConvertPositionFromSimulation(Particle.OldLocation, EParticleValueSpace::World);
	const FVector EndWorld = ConvertPositionFromSimulation(Particle.Location, EParticleValueSpace::World);

	const FVector Travel = EndWorld - OutStartWorld;
	OutTravelDistance = Travel.Length();
	if (OutTravelDistance <= ParticleCollisionMinTravelDistance)
	{
		return false;
	}

	OutTravelDirection = Travel / OutTravelDistance;
	return !OutTravelDirection.IsNearlyZero();
}

ECollisionChannel FParticleEmitterInstance::GetParticleCollisionQueryChannel(
	const UParticleModuleCollision& CollisionModule) const
{
	// Current authoring is still channel-driven. A future object-type query path
	// can branch from this policy selection layer without disturbing response,
	// event, budget, or repeated-hit handling.
	return CollisionModule.CollisionChannel;
}

const AActor* FParticleEmitterInstance::GetParticleCollisionQueryIgnoreActor() const
{
	// Current filter policy is intentionally lightweight: use the module's
	// collision channel and ignore the owning actor. Future static/dynamic or
	// richer object filtering should slot in here, not in response consumption.
	return Component ? Component->GetOwner() : nullptr;
}

bool FParticleEmitterInstance::PerformParticleCollisionQuery(
	const FVector& StartWorld,
	const FVector& TravelDirection,
	float TravelDistance,
	const UParticleModuleCollision& CollisionModule,
	FHitResult& OutHit) const
{
	UWorld* World = Component ? Component->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	// Query/filter policy is separate from budget, repeated-hit suppression, LOD
	// reduction, and event gating. This helper only answers "what do we ask the
	// world/physics system to test right now?"
	return World->PhysicsRaycast(
		StartWorld,
		TravelDirection,
		TravelDistance,
		OutHit,
		GetParticleCollisionQueryChannel(CollisionModule),
		GetParticleCollisionQueryIgnoreActor());
}

bool FParticleEmitterInstance::ShouldPruneEmitterCollisionByBounds(
	const UParticleModuleCollision& CollisionModule,
	FParticleCollisionDebugStats* DebugStats)
{
	FVector BoundsMin = FVector::ZeroVector;
	FVector BoundsMax = FVector::ZeroVector;
	if (!ComputeDynamicBounds(BoundsMin, BoundsMax))
	{
		bHasLastCollisionPruneBoundsCenter = false;
		return false;
	}

	const FVector BoundsCenter = (BoundsMin + BoundsMax) * 0.5f;
	const FVector BoundsExtent = (BoundsMax - BoundsMin) * 0.5f;
	const bool bCanReuseNearbyCollisionCache =
		NearbyCollisionCache.bValid &&
		NearbyCollisionCache.CachedLODIndex == CurrentLODIndex &&
		(EmitterTimeSeconds - NearbyCollisionCache.LastRefreshTimeSeconds) <= ParticleCollisionNearbyCacheRefreshSeconds &&
		(BoundsCenter - NearbyCollisionCache.CachedBoundsCenter).Length() <= ParticleCollisionNearbyCacheMinCenterDelta &&
		(BoundsExtent - NearbyCollisionCache.CachedBoundsExtent).Length() <= ParticleCollisionNearbyCacheMinExtentDelta;
	if (bCanReuseNearbyCollisionCache && NearbyCollisionCache.bHasNearbyCollisionEvidence)
	{
		if (DebugStats)
		{
			DebugStats->bNearbyCollisionCacheValid = true;
			DebugStats->bNearbyCollisionEvidence = true;
			++DebugStats->NearbyCacheReuseCount;
			DebugStats->NearbyEvidenceProbeCount += NearbyCollisionCache.EvidenceProbeCount;
		}
		LastCollisionPruneBoundsCenter = BoundsCenter;
		bHasLastCollisionPruneBoundsCenter = true;
		return false;
	}

	const bool bHasNearbyCollision =
		HasNearbyCollisionForEmitterBounds(
			CollisionModule,
			BoundsMin,
			BoundsMax,
			DebugStats);

	LastCollisionPruneBoundsCenter = BoundsCenter;
	bHasLastCollisionPruneBoundsCenter = true;
	if (bHasNearbyCollision)
	{
		return false;
	}

	if (DebugStats)
	{
		++DebugStats->EmitterPrunedCount;
	}
	return true;
}

void FParticleEmitterInstance::RefreshEmitterNearbyCollisionCache(
	const UParticleModuleCollision& CollisionModule,
	FParticleCollisionDebugStats* DebugStats)
{
	FVector BoundsMin = FVector::ZeroVector;
	FVector BoundsMax = FVector::ZeroVector;
	if (!ComputeDynamicBounds(BoundsMin, BoundsMax))
	{
		NearbyCollisionCache = FEmitterNearbyCollisionCache{};
		if (DebugStats)
		{
			DebugStats->bNearbyCollisionCacheValid = false;
			DebugStats->bNearbyCollisionEvidence = false;
		}
		return;
	}

	const FVector BoundsCenter = (BoundsMin + BoundsMax) * 0.5f;
	const FVector BoundsExtent = (BoundsMax - BoundsMin) * 0.5f;
	const float CenterDelta = (BoundsCenter - NearbyCollisionCache.CachedBoundsCenter).Length();
	const float ExtentDelta = (BoundsExtent - NearbyCollisionCache.CachedBoundsExtent).Length();
	const bool bShouldRefresh =
		!NearbyCollisionCache.bValid ||
		NearbyCollisionCache.CachedLODIndex != CurrentLODIndex ||
		(EmitterTimeSeconds - NearbyCollisionCache.LastRefreshTimeSeconds) >= ParticleCollisionNearbyCacheRefreshSeconds ||
		CenterDelta >= ParticleCollisionNearbyCacheMinCenterDelta ||
		ExtentDelta >= ParticleCollisionNearbyCacheMinExtentDelta;

	if (!bShouldRefresh)
	{
		if (DebugStats)
		{
			++DebugStats->NearbyCacheReuseCount;
			DebugStats->bNearbyCollisionCacheValid = NearbyCollisionCache.bValid;
			DebugStats->bNearbyCollisionEvidence = NearbyCollisionCache.bHasNearbyCollisionEvidence;
			DebugStats->NearbyEvidenceProbeCount += NearbyCollisionCache.EvidenceProbeCount;
			if (NearbyCollisionCache.bHasNearbyCollisionEvidence)
			{
				++DebugStats->NearbyEvidenceHitCount;
			}
			else
			{
				++DebugStats->NearbyEvidenceMissCount;
			}
		}
		return;
	}

	int32 EvidenceProbeCount = 0;
	const bool bHasNearbyCollisionEvidence =
		GatherEmitterNearbyCollisionEvidence(
			CollisionModule,
			BoundsCenter,
			BoundsExtent,
			EvidenceProbeCount);

	NearbyCollisionCache.bValid = true;
	NearbyCollisionCache.bHasNearbyCollisionEvidence = bHasNearbyCollisionEvidence;
	NearbyCollisionCache.CachedBoundsCenter = BoundsCenter;
	NearbyCollisionCache.CachedBoundsExtent = BoundsExtent;
	NearbyCollisionCache.LastRefreshTimeSeconds = EmitterTimeSeconds;
	NearbyCollisionCache.CachedLODIndex = CurrentLODIndex;
	NearbyCollisionCache.EvidenceProbeCount = EvidenceProbeCount;

	if (DebugStats)
	{
		++DebugStats->NearbyCacheRefreshCount;
		DebugStats->bNearbyCollisionCacheValid = true;
		DebugStats->bNearbyCollisionEvidence = bHasNearbyCollisionEvidence;
		DebugStats->NearbyEvidenceProbeCount += EvidenceProbeCount;
		if (bHasNearbyCollisionEvidence)
		{
			++DebugStats->NearbyEvidenceHitCount;
		}
		else
		{
			++DebugStats->NearbyEvidenceMissCount;
		}
	}
}

bool FParticleEmitterInstance::GatherEmitterNearbyCollisionEvidence(
	const UParticleModuleCollision& CollisionModule,
	const FVector& BoundsCenter,
	const FVector& BoundsExtent,
	int32& OutEvidenceProbeCount)
{
	OutEvidenceProbeCount = 0;
	const float BoundsRadius = BoundsExtent.Length();
	const FVector MovementDelta = NearbyCollisionCache.bValid
		? (BoundsCenter - NearbyCollisionCache.CachedBoundsCenter)
		: FVector::ZeroVector;
	const float MovementDistance = MovementDelta.Length();

	auto TryNearbyProbe =
		[this, &CollisionModule, &OutEvidenceProbeCount](
			const FVector& StartWorld,
			const FVector& Direction,
			float Distance,
			FHitResult& OutHit) -> bool
		{
			if (Distance <= ParticleCollisionMinTravelDistance)
			{
				return false;
			}

			FVector ProbeDirection = Direction;
			if (ProbeDirection.IsNearlyZero())
			{
				return false;
			}
			ProbeDirection.Normalize();

			const FVector ProbeEnd = StartWorld + ProbeDirection * Distance;
			++OutEvidenceProbeCount;
			const bool bHit = PerformParticleCollisionQuery(
				StartWorld,
				ProbeDirection,
				Distance,
				CollisionModule,
				OutHit);
			DebugDrawEmitterNearbyCollisionProbe(
				StartWorld,
				ProbeEnd,
				bHit ? FColor(0, 220, 170) : FColor(200, 120, 255));
			return bHit;
		};

	FHitResult Hit;
	const float DownwardDistance = std::max(
		BoundsExtent.Z + BoundsRadius + ParticleCollisionEmitterPruneProbePadding,
		ParticleCollisionEmitterPruneMinProbeDistance);
	if (TryNearbyProbe(BoundsCenter, FVector::UpVector * -1.0f, DownwardDistance, Hit))
	{
		DebugDrawParticleCollisionHit(
			Hit,
			Hit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : Hit.ImpactNormal.Normalized(),
			FColor(0, 220, 170),
			FColor(0, 220, 170));
		return true;
	}

	if (NearbyCollisionCache.bValid &&
		MovementDistance >= ParticleCollisionEmitterPruneMinMovementDistance)
	{
		const float MovementProbeDistance = std::max(
			MovementDistance + BoundsRadius + ParticleCollisionEmitterPruneProbePadding,
			ParticleCollisionEmitterPruneMinProbeDistance);
		if (TryNearbyProbe(BoundsCenter, MovementDelta, MovementProbeDistance, Hit))
		{
			DebugDrawParticleCollisionHit(
				Hit,
				Hit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : Hit.ImpactNormal.Normalized(),
				FColor(0, 220, 170),
				FColor(0, 220, 170));
			return true;
		}
	}

	const float DominantHorizontalExtent = std::max(std::abs(BoundsExtent.X), std::abs(BoundsExtent.Y));
	if (DominantHorizontalExtent >= ParticleCollisionNearbyCacheLateralProbeMinExtent)
	{
		const FVector LateralDirection =
			(std::abs(BoundsExtent.X) >= std::abs(BoundsExtent.Y))
			? FVector::RightVector
			: FVector::ForwardVector;
		const float LateralDistance = std::max(
			DominantHorizontalExtent + BoundsRadius + ParticleCollisionEmitterPruneProbePadding,
			ParticleCollisionEmitterPruneMinProbeDistance);
		if (TryNearbyProbe(BoundsCenter, LateralDirection, LateralDistance, Hit))
		{
			DebugDrawParticleCollisionHit(
				Hit,
				Hit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : Hit.ImpactNormal.Normalized(),
				FColor(0, 220, 170),
				FColor(0, 220, 170));
			return true;
		}
	}

	// Nearby preselection is coarse context only. A miss here means "no strong
	// nearby evidence right now", not "future per-particle collision is impossible".
	return false;
}

bool FParticleEmitterInstance::HasNearbyCollisionForEmitterBounds(
	const UParticleModuleCollision& CollisionModule,
	const FVector& BoundsMin,
	const FVector& BoundsMax,
	FParticleCollisionDebugStats* DebugStats)
{
	const FVector BoundsCenter = (BoundsMin + BoundsMax) * 0.5f;
	const FVector BoundsExtent = (BoundsMax - BoundsMin) * 0.5f;
	const float BoundsRadius = BoundsExtent.Length();

	if (!bHasLastCollisionPruneBoundsCenter)
	{
		return true;
	}

	const FVector MovementDelta = BoundsCenter - LastCollisionPruneBoundsCenter;
	const float MovementDistance = MovementDelta.Length();
	if (MovementDistance < ParticleCollisionEmitterPruneMinMovementDistance)
	{
		return true;
	}

	const FVector DownwardDirection = FVector::UpVector * -1.0f;
	const float DownwardDistance = std::max(
		BoundsExtent.Z + BoundsRadius + ParticleCollisionEmitterPruneProbePadding,
		ParticleCollisionEmitterPruneMinProbeDistance);
	const FVector DownwardEnd = BoundsCenter + DownwardDirection * DownwardDistance;

	FHitResult DownwardHit;
	// Reuse the same channel/ignore semantics as the real collision path so the
	// coarse prune decision stays aligned with what per-particle queries would ask.
	const bool bDownwardHit = PerformParticleCollisionQuery(
		BoundsCenter,
		DownwardDirection,
		DownwardDistance,
		CollisionModule,
		DownwardHit);
	DebugDrawEmitterCollisionPruneProbe(
		BoundsCenter,
		DownwardEnd,
		bDownwardHit ? FColor(80, 180, 255) : FColor(160, 120, 220));
	if (DebugStats)
	{
		if (bDownwardHit)
		{
			++DebugStats->EmitterPruneProbeHitCount;
		}
		else
		{
			++DebugStats->EmitterPruneProbeMissCount;
		}
	}
	if (bDownwardHit)
	{
		DebugDrawParticleCollisionHit(
			DownwardHit,
			DownwardHit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : DownwardHit.ImpactNormal.Normalized(),
			FColor(80, 180, 255),
			FColor(80, 180, 255));
		return true;
	}

	const FVector MovementDirection = MovementDelta / MovementDistance;
	const float MovementProbeDistance = std::max(
		MovementDistance + BoundsRadius + ParticleCollisionEmitterPruneProbePadding,
		ParticleCollisionEmitterPruneMinProbeDistance);
	const FVector MovementEnd = BoundsCenter + MovementDirection * MovementProbeDistance;

	FHitResult MovementHit;
	const bool bMovementHit = PerformParticleCollisionQuery(
		BoundsCenter,
		MovementDirection,
		MovementProbeDistance,
		CollisionModule,
		MovementHit);
	DebugDrawEmitterCollisionPruneProbe(
		BoundsCenter,
		MovementEnd,
		bMovementHit ? FColor(80, 180, 255) : FColor(160, 120, 220));
	if (DebugStats)
	{
		if (bMovementHit)
		{
			++DebugStats->EmitterPruneProbeHitCount;
		}
		else
		{
			++DebugStats->EmitterPruneProbeMissCount;
		}
	}
	if (bMovementHit)
	{
		DebugDrawParticleCollisionHit(
			MovementHit,
			MovementHit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : MovementHit.ImpactNormal.Normalized(),
			FColor(80, 180, 255),
			FColor(80, 180, 255));
		return true;
	}

	// Both coarse probes missed, so the emitter looks unlikely to spend useful
	// collision work this tick. This is intentionally conservative: any
	// ambiguous case above keeps collision processing enabled instead.
	return false;
}

bool FParticleEmitterInstance::ShouldDebugParticleCollisions() const
{
	return GParticleCollisionDebugEnabled && Component && Component->GetWorld();
}

void FParticleEmitterInstance::DebugDrawParticleCollisionQuery(
	const FVector& StartWorld,
	const FVector& EndWorld,
	const FColor& Color) const
{
	if (!ShouldDebugParticleCollisions())
	{
		return;
	}

	DrawDebugLine(
		Component->GetWorld(),
		StartWorld,
		EndWorld,
		Color,
		ParticleCollisionDebugDrawDuration);
}

void FParticleEmitterInstance::DebugDrawEmitterCollisionPruneProbe(
	const FVector& StartWorld,
	const FVector& EndWorld,
	const FColor& Color) const
{
	if (!ShouldDebugParticleCollisions())
	{
		return;
	}

	DrawDebugLine(
		Component->GetWorld(),
		StartWorld,
		EndWorld,
		Color,
		ParticleCollisionDebugDrawDuration);
}

void FParticleEmitterInstance::DebugDrawEmitterNearbyCollisionProbe(
	const FVector& StartWorld,
	const FVector& EndWorld,
	const FColor& Color) const
{
	if (!ShouldDebugParticleCollisions())
	{
		return;
	}

	DrawDebugLine(
		Component->GetWorld(),
		StartWorld,
		EndWorld,
		Color,
		ParticleCollisionDebugDrawDuration);
}

void FParticleEmitterInstance::DebugDrawParticleCollisionHit(
	const FHitResult& Hit,
	const FVector& CollisionNormal,
	const FColor& PointColor,
	const FColor& NormalColor) const
{
	if (!ShouldDebugParticleCollisions())
	{
		return;
	}

	UWorld* World = Component->GetWorld();
	DrawDebugPoint(
		World,
		Hit.WorldHitLocation,
		ParticleCollisionDebugPointSize,
		PointColor,
		ParticleCollisionDebugDrawDuration);
	DrawDebugLine(
		World,
		Hit.WorldHitLocation,
		Hit.WorldHitLocation + CollisionNormal * ParticleCollisionDebugNormalLength,
		NormalColor,
		ParticleCollisionDebugDrawDuration);
}

void FParticleEmitterInstance::DebugLogParticleCollisionStats(
	const FParticleCollisionDebugStats& Stats) const
{
	if (!ShouldDebugParticleCollisions())
	{
		return;
	}

	const std::string EmitterName = Emitter ? Emitter->GetName() : "<null>";
	UE_LOG(
		"[ParticleCollisionDebug] emitter=%s lod=%d active=%d budget=%d disabled=%d eventGated=%d policyOverride=%d pruned=%d pruneProbeHit=%d pruneProbeMiss=%d nearbyCacheValid=%d nearbyCacheRefreshed=%d nearbyCacheReused=%d nearbyEvidence=%d nearbyEvidenceHit=%d nearbyEvidenceMiss=%d nearbyProbeCount=%d candidates=%d(high=%d fallback=%d) queried=%d skippedState=%d skippedEarly=%d skippedBudget=%d noHit=%d accepted=%d suppressed=%d emitted=%d eventSuppressed=%d lowSpeedStop=%d killedNow=%d frozen=%d ignored=%d",
		EmitterName.c_str(),
		Stats.CurrentLODIndex,
		Stats.ActiveParticles,
		Stats.EffectiveBudget,
		Stats.bCollisionFullyDisabledForLOD ? 1 : 0,
		Stats.bCollisionEventGatedForLOD ? 1 : 0,
		Stats.bUsingCollisionOuterPolicyOverride ? 1 : 0,
		Stats.EmitterPrunedCount,
		Stats.EmitterPruneProbeHitCount,
		Stats.EmitterPruneProbeMissCount,
		Stats.bNearbyCollisionCacheValid ? 1 : 0,
		Stats.NearbyCacheRefreshCount,
		Stats.NearbyCacheReuseCount,
		Stats.bNearbyCollisionEvidence ? 1 : 0,
		Stats.NearbyEvidenceHitCount,
		Stats.NearbyEvidenceMissCount,
		Stats.NearbyEvidenceProbeCount,
		Stats.CandidateCount,
		Stats.HighPriorityCandidateCount,
		Stats.FallbackCandidateCount,
		Stats.QueriedCount,
		Stats.SkippedByState,
		Stats.SkippedByEarlyOut,
		Stats.SkippedByBudget,
		Stats.NoHitCount,
		Stats.AcceptedHitCount,
		Stats.SuppressedAsNoiseCount,
		Stats.EmittedEventCount,
		Stats.EventGatedCount,
		Stats.StopLikeLowSpeedCount,
		Stats.KilledImmediateCount,
		Stats.FrozenAfterLimitCount,
		Stats.IgnoredFurtherCollisionsCount);
}

bool FParticleEmitterInstance::ResolveSingleParticleCollision(
	FBaseParticle& Particle,
	const UParticleModuleCollision& CollisionModule,
	uint32 ModuleOffset,
	float DeltaTime,
	FParticleCollisionDebugStats* DebugStats)
{
	if (FinalizeParticleCollisionWithoutQuery(Particle, CollisionModule, ModuleOffset))
	{
		return false;
	}

	auto* Payload =
		PARTICLE_PAYLOAD(&Particle, ModuleOffset, UParticleModuleCollision::FCollisionParticlePayload);
	if (!Payload)
	{
		return false;
	}

	FVector StartWorld = FVector::ZeroVector;
	FVector TravelDirection = FVector::ZeroVector;
	float TravelDistance = 0.0f;
	if (!BuildParticleCollisionQuerySegment(
		Particle,
		StartWorld,
		TravelDirection,
		TravelDistance))
	{
		return false;
	}

	FHitResult Hit;
	const FVector EndWorld =
		ConvertPositionFromSimulation(Particle.Location, EParticleValueSpace::World);
	if (!PerformParticleCollisionQuery(
		StartWorld,
		TravelDirection,
		TravelDistance,
		CollisionModule,
		Hit))
	{
		if (DebugStats)
		{
			++DebugStats->NoHitCount;
		}
		DebugDrawParticleCollisionQuery(StartWorld, EndWorld, FColor::Gray());
		return false;
	}

	// Response currently depends on these FHitResult fields:
	// - WorldHitLocation: resolved contact position
	// - ImpactNormal: accepted collision normal for response math
	// - FaceIndex: optional diagnostic/event detail
	const FVector ImpactVelocityWorld =
		ConvertVectorFromSimulation(Particle.Velocity, EParticleValueSpace::World);
	const float CollisionTimeSeconds = EmitterTimeSeconds + DeltaTime;
	FVector CollisionNormal = Hit.ImpactNormal;
	if (CollisionNormal.IsNearlyZero())
	{
		CollisionNormal = FVector::UpVector;
	}
	else
	{
		CollisionNormal.Normalize();
	}

	if (ShouldSuppressRepeatedCollisionHit(*Payload, CollisionNormal, CollisionTimeSeconds))
	{
		// This looks like repeated contact with the same surface within a very short
		// time window. Calm the contact without incrementing collision count or
		// emitting a normal collision event. This is "noise suppressed", not an
		// accepted collision report.
		if (DebugStats)
		{
			++DebugStats->SuppressedAsNoiseCount;
		}
		DebugDrawParticleCollisionQuery(StartWorld, EndWorld, FColor::Yellow());
		DebugDrawParticleCollisionHit(Hit, CollisionNormal, FColor::Yellow(), FColor(255, 180, 0));
		const FVector AdjustedWorldPosition =
			Hit.WorldHitLocation + CollisionNormal * ParticleCollisionSurfaceOffset;
		const float NormalSpeed = ImpactVelocityWorld.Dot(CollisionNormal);
		FVector StabilizedVelocityWorld =
			ImpactVelocityWorld - CollisionNormal * std::min(NormalSpeed, 0.0f);

		if (!IsMeaningfulCollisionSpeed(GetCollisionSpeed(StabilizedVelocityWorld)))
		{
			StabilizedVelocityWorld = FVector::ZeroVector;
		}

		Particle.Location =
			ConvertPositionToSimulation(AdjustedWorldPosition, EParticleValueSpace::World);
		Particle.OldLocation = Particle.Location;
		Particle.Velocity =
			ConvertVectorToSimulation(StabilizedVelocityWorld, EParticleValueSpace::World);
		UpdateRecentCollisionState(*Payload, CollisionTimeSeconds, CollisionNormal);
		return false;
	}

	const bool bKillImmediately =
		ApplyImmediateParticleCollisionResponse(
			Particle,
			CollisionModule,
			Hit,
			ImpactVelocityWorld,
			DebugStats);
	UpdateRecentCollisionState(*Payload, CollisionTimeSeconds, CollisionNormal);

	// From this point on the hit is treated as an accepted / meaningful collision:
	// it passed raw hit detection, budget processing, and repeated-contact noise
	// suppression. Reporting remains a separate policy decision.
	++Payload->NumCollisions;
	if (DebugStats)
	{
		++DebugStats->AcceptedHitCount;
	}

	if (ShouldEmitCollisionEventForAcceptedHit(CollisionModule))
	{
		// Base collision events represent accepted collision reports, not just any
		// raw raycast hit. They are emitted before final kill/freeze/ignore
		// completion consequences so response-mode timing stays predictable.
		if (DebugStats)
		{
			++DebugStats->EmittedEventCount;
		}
		DebugDrawParticleCollisionQuery(StartWorld, EndWorld, FColor::Green());
		DebugDrawParticleCollisionHit(Hit, CollisionNormal, FColor::Green(), FColor::Green());
		EmitCollisionEventForAcceptedHit(
			Particle,
			CollisionNormal,
			ImpactVelocityWorld,
			Hit,
			CollisionTimeSeconds);
	}
	else
	{
		if (DebugStats)
		{
			++DebugStats->EventGatedCount;
		}
		DebugDrawParticleCollisionQuery(StartWorld, EndWorld, FColor(0, 180, 255));
		DebugDrawParticleCollisionHit(Hit, CollisionNormal, FColor(0, 180, 255), FColor(0, 180, 255));
	}

	if (bKillImmediately)
	{
		if (DebugStats)
		{
			++DebugStats->KilledImmediateCount;
		}
		Particle.Flags |= static_cast<uint32>(EParticleStateFlags::Killed);
		return true;
	}

	if (HasCollisionCountLimit(CollisionModule) && Payload->NumCollisions >= CollisionModule.MaxCollisions)
	{
		// Completion semantics are separate from the immediate response. A particle
		// may bounce/stop on this hit, then transition into Kill/Freeze/Ignore once
		// the configured collision count has been exhausted.
		ApplyCollisionCompletionBehavior(Particle, *Payload, CollisionModule);
		if (DebugStats)
		{
			if (Payload->bFrozenAfterLimit)
			{
				++DebugStats->FrozenAfterLimitCount;
			}
			if (Payload->bIgnoreFurtherCollisions)
			{
				++DebugStats->IgnoredFurtherCollisionsCount;
			}
		}
	}

	return true;
}

bool FParticleEmitterInstance::ApplyImmediateParticleCollisionResponse(
	FBaseParticle& Particle,
	const UParticleModuleCollision& CollisionModule,
	const FHitResult& Hit,
	const FVector& ImpactVelocity,
	FParticleCollisionDebugStats* DebugStats) const
{
	FVector CollisionNormal = Hit.ImpactNormal;
	if (CollisionNormal.IsNearlyZero())
	{
		CollisionNormal = FVector::UpVector;
	}
	else
	{
		CollisionNormal.Normalize();
	}

	const FVector AdjustedWorldPosition =
		Hit.WorldHitLocation + CollisionNormal * ParticleCollisionSurfaceOffset;

	const bool bLowSpeedImpact =
		!IsMeaningfulCollisionSpeed(GetCollisionSpeed(ImpactVelocity));

	// Immediate response answers "what happens on this hit?" Completion semantics
	// answer what to do only after the configured collision-count limit is reached.
	switch (ResolveImmediateCollisionResponseMode(CollisionModule))
	{
	case UParticleModuleCollision::ECollisionResponseMode::Kill:
		Particle.Location =
			ConvertPositionToSimulation(AdjustedWorldPosition, EParticleValueSpace::World);
		Particle.OldLocation = Particle.Location;
		Particle.Velocity = FVector::ZeroVector;
		return true;
	case UParticleModuleCollision::ECollisionResponseMode::Stop:
		Particle.Location =
			ConvertPositionToSimulation(AdjustedWorldPosition, EParticleValueSpace::World);
		Particle.OldLocation = Particle.Location;
		Particle.Velocity = FVector::ZeroVector;
		return false;
	case UParticleModuleCollision::ECollisionResponseMode::Bounce:
	default:
	{
		FVector NormalVelocityWorld = FVector::ZeroVector;
		FVector TangentialVelocityWorld = FVector::ZeroVector;
		DecomposeVelocityAgainstSurfaceNormal(
			ImpactVelocity,
			CollisionNormal,
			NormalVelocityWorld,
			TangentialVelocityWorld);

		const float NormalBounceRetention =
			std::max(0.0f, CollisionModule.DampingFactor);
		const float TangentialRetention =
			std::clamp(CollisionModule.TangentialDamping, 0.0f, 1.0f);

		const FVector ReflectedNormalVelocityWorld =
			NormalVelocityWorld * (-NormalBounceRetention);
		const FVector DampedTangentialVelocityWorld =
			TangentialVelocityWorld * TangentialRetention;
		const FVector BouncedVelocityWorld =
			ReflectedNormalVelocityWorld + DampedTangentialVelocityWorld;

		Particle.Location =
			ConvertPositionToSimulation(AdjustedWorldPosition, EParticleValueSpace::World);
		Particle.OldLocation = Particle.Location;
		// Bounce now separates the two main response axes:
		// - DampingFactor: retained normal / bounce energy
		// - TangentialDamping: retained surface-parallel sliding energy
		// Very low-speed contacts are still calmed into a stop-like response so the
		// richer tangential model does not reintroduce jitter.
		if (bLowSpeedImpact && DebugStats)
		{
			++DebugStats->StopLikeLowSpeedCount;
		}
		Particle.Velocity = bLowSpeedImpact
			? FVector::ZeroVector
			: ConvertVectorToSimulation(BouncedVelocityWorld, EParticleValueSpace::World);
		return false;
	}
	}
}

bool FParticleEmitterInstance::ShouldProcessCollisionsForCurrentLOD() const
{
	// Current-emitter-LOD outer policy gate: should this emitter spend any
	// collision work this tick before per-particle simulation contracts matter?
	return !IsCollisionFullyDisabledForCurrentLOD();
}

bool FParticleEmitterInstance::IsCollisionFullyDisabledForCurrentLOD() const
{
	// Full collision disable is the strongest LOD reduction axis. It is kept
	// separate from reduced budgets or event gating so those policies can evolve
	// independently. Current-emitter-LOD authoring may now override this outer
	// policy; otherwise we keep the legacy hardcoded fallback.
	return ResolveCollisionOuterPolicyForCurrentLOD().bCollisionFullyDisabled;
}

bool FParticleEmitterInstance::ShouldEmitCollisionEventsForCurrentLOD() const
{
	// Current-emitter-LOD outer policy: collision queries and collision-event
	// fidelity are distinct reduction axes.
	// Lower LODs may keep some collision behavior while choosing not to preserve
	// gameplay/event-facing collision reporting. Authoring may override this
	// policy without redefining collision response/completion semantics.
	return ResolveCollisionOuterPolicyForCurrentLOD().bEmitCollisionEvents;
}

int32 FParticleEmitterInstance::GetCollisionCheckBudgetForCurrentLOD() const
{
	// Current-emitter-LOD outer policy: budget answers "how many collision queries
	// may this emitter spend?", not "what collision payload/module meaning applies?"
	// Budget scaling is one reduction axis; full disable and event gating are
	// handled separately. Current-LOD collision authoring may override this, but
	// fallback stays the legacy hardcoded LOD policy for existing assets.
	return ResolveCollisionOuterPolicyForCurrentLOD().CollisionQueryBudget;
}

void FParticleEmitterInstance::ResizeParticleData(uint32 NewMax)
{
	NewMax = std::min<uint32>(NewMax, ParticleConstants::MaxParticlesPerEmitter);

	FParticleStorage OldStorage = std::move(RuntimeStorage);
	const uint32 OldActiveParticles = ActiveParticles;

	MaxActiveParticles = NewMax;
	const uint32 InstancePayloadBytes = Emitter ? Emitter->GetReqInstanceBytes() : 0;

	RuntimeStorage.Allocate(
		MaxActiveParticles * ParticleStride,
		MaxActiveParticles,
		InstancePayloadBytes);

	if (OldStorage.ParticleData && RuntimeStorage.ParticleData)
	{
		const uint32 PreservedActiveParticles = std::min(OldActiveParticles, MaxActiveParticles);
		const uint32 BytesToCopy = std::min(
			OldStorage.ParticleDataBytes,
			PreservedActiveParticles * ParticleStride);

		if (BytesToCopy > 0)
		{
			std::memcpy(RuntimeStorage.ParticleData, OldStorage.ParticleData, BytesToCopy);
		}
	}

	if (OldStorage.InstanceData && RuntimeStorage.InstanceData)
	{
		const uint32 InstanceBytesToCopy = std::min(
			OldStorage.InstanceDataBytes,
			RuntimeStorage.InstanceDataBytes);

		if (InstanceBytesToCopy > 0)
		{
			std::memcpy(RuntimeStorage.InstanceData, OldStorage.InstanceData, InstanceBytesToCopy);
		}
	}

	const uint32 PreservedIndexCount = std::min(
		ActiveParticles,
		std::min(OldStorage.ParticleIndexCount, RuntimeStorage.ParticleIndexCount));

	for (uint32 i = 0; i < PreservedIndexCount; ++i)
	{
		RuntimeStorage.ParticleIndices[i] = OldStorage.ParticleIndices[i];
	}

	for (uint32 i = PreservedIndexCount; i < MaxActiveParticles; ++i)
	{
		RuntimeStorage.ParticleIndices[i] = static_cast<uint16>(i);
	}

	if (ActiveParticles > MaxActiveParticles)
	{
		ActiveParticles = MaxActiveParticles;
	}
}

void FParticleEmitterInstance::FillReplayData(FDynamicEmitterReplayDataBase& OutData) const
{
	// Shared replay build flow:
	//   1) copy active particle snapshot
	//   2) resolve current render replay LOD
	//   3) fill RequiredModule-based base render contract
	// This boundary is where GT simulation state stops being "live particle data"
	// and starts becoming "this frame's emitter-level RT replay contract."
	CopyParticleSnapshotToReplay(*this, OutData);

	UParticleLODLevel* RenderReplayLOD = GetRenderReplayLODLevel();
	if (!RenderReplayLOD || !RenderReplayLOD->RequiredModule)
	{
		return;
	}

	FillBaseReplayMetadataFromRequiredModule(*this, *RenderReplayLOD, OutData);
}

uint32 FParticleEmitterInstance::GetInitialParticleCapacity() const
{
	return 64;
}

uint32 FParticleEmitterInstance::GrowParticleCapacity(uint32 Current, uint32 Required) const
{
	uint32 NewCapacity = Current > 0 ? Current : GetInitialParticleCapacity();

	while (NewCapacity < Required)
	{
		NewCapacity *= 2;
	}

	return std::min(NewCapacity, ParticleConstants::MaxParticlesPerEmitter);
}

bool FParticleEmitterInstance::IsParticleKilled(const FBaseParticle* Particle) const
{
	if (!Particle) return true;

	const uint32 KilledFlag = static_cast<uint32>(EParticleStateFlags::Killed);
	return (Particle->Flags & KilledFlag) != 0 || Particle->RelativeTime >= 1.0f;
}

void FParticleEmitterInstance::ClearSpawnedFlag(FBaseParticle* Particle) const
{
	if (!Particle) return;

	const uint32 SpawnedFlag = static_cast<uint32>(EParticleStateFlags::Spawned);
	Particle->Flags &= ~SpawnedFlag;
}

const UParticleModuleRequired* FParticleEmitterInstance::GetRequiredModule() const
{
	UParticleLODLevel* LOD = GetCurrentLOD();
	if (!LOD)
	{
		return nullptr;
	}

	return LOD->RequiredModule;
}

bool FParticleEmitterInstance::IsSpawningAllowed() const
{
	return !bHaltSpawning && !IsSpawningComplete();
}

void FParticleEmitterInstance::AdvanceLoopState(float DeltaTime)
{
	EmitterTimeSeconds += DeltaTime;

	const UParticleModuleRequired* Required = GetRequiredModule();
	if (!Required || Required->EmitterDuration <= 0.0f)
	{
		CurrentLoopTimeSeconds += DeltaTime;
		return;
	}

	CurrentLoopTimeSeconds += DeltaTime;

	const float Duration = Required->EmitterDuration;
	const float Epsilon = 1.0e-4f;
	// 큰 DeltaTime이 들어와 한 프레임 안에 여러 loop를 넘길 수 있으므로 while로 처리.
	while (CurrentLoopTimeSeconds >= Duration - Epsilon)
	{
		CurrentLoopTimeSeconds =
			(CurrentLoopTimeSeconds > Duration) ? (CurrentLoopTimeSeconds - Duration) : 0.0f;
		SpawnFraction = 0.0f;
		++LoopCount;

		UParticleLODLevel* LOD = GetCurrentLOD();
		if (LOD && LOD->SpawnModule)
		{
			if (auto* Payload =
				GetModuleInstancePayload<UParticleModuleSpawn::FSpawnModuleInstancePayload>(LOD->SpawnModule))
			{
				Payload->LastProcessedBurstTime = 0.0f;
			}
		}

		if (Duration <= Epsilon)
		{
			break;
		}
	}
}

// -- Sprite ----
FDynamicEmitterDataBase* FParticleSpriteEmitterInstance::GetDynamicData()
{
	// Shared base replay build first, then Sprite-only shaping values from the
	// current render replay LOD. This matches the overall GT->RT pipeline split.
	FDynamicSpriteEmitterData* Data = new FDynamicSpriteEmitterData();

	FillReplayData(Data->Source);
	Data->Source.EmitterType = EDynamicEmitterType::Sprite;

	if (UParticleLODLevel* RenderReplayLOD = GetRenderReplayLODLevel())
	{
		FillSpriteReplayShapingFromRenderLOD(*RenderReplayLOD, Data->Source);
	}

	return Data;
}

// -- Mesh ----
FDynamicEmitterDataBase* FParticleMeshEmitterInstance::GetDynamicData()
{
	// Mesh replay keeps the shared/base render contract and then layers on the
	// type-data view that RT Mesh build may consume or merely preserve.
	FDynamicMeshEmitterData* Data = new FDynamicMeshEmitterData();
	FillReplayData(Data->Source);
	Data->Source.EmitterType = EDynamicEmitterType::Mesh;

	if (UParticleLODLevel* RenderReplayLOD = GetRenderReplayLODLevel())
	{
		FillMeshReplayShapingFromRenderLOD(*RenderReplayLOD, Data->Source);
	}

	// Mesh resolve가 실패해도 emitter type 자체는 Mesh로 유지한다.
	// RT는 nullptr mesh를 보고 기존 fallback mesh 경로를 그대로 사용할 수 있다.
	return Data;
}

// -- Beam ----
FDynamicEmitterDataBase* FParticleBeamEmitterInstance::GetDynamicData()
{
	// Beam replay still follows the same shared/base replay path, but its
	// type-specific phase resolves one emitter-level beam shape snapshot rather
	// than a per-particle independent beam list.
	FDynamicBeamEmitterData* Data = new FDynamicBeamEmitterData();

	FillReplayData(Data->Source);
	Data->Source.EmitterType = EDynamicEmitterType::Beam;

	FVector ResolvedSource = SourcePoint;
	FVector ResolvedTarget = TargetPoint;

	if (UParticleLODLevel* RenderReplayLOD = GetRenderReplayLODLevel())
	{
		FillBeamReplayShapingFromRenderLOD(
			*this,
			*RenderReplayLOD,
			Data->Source,
			ResolvedSource,
			ResolvedTarget,
			LockedSourcePoint,
			LockedTargetPoint,
			bHasLockedSourcePoint,
			bHasLockedTargetPoint,
			bHasExplicitEndpoints);
	}

	Data->Source.SourcePoint = ResolvedSource;
	Data->Source.TargetPoint = ResolvedTarget;
	return Data;
}
void FParticleBeamEmitterInstance::Reset()
{
	FParticleEmitterInstance::Reset();
	ResetEndpointLocks();
}

void FParticleBeamEmitterInstance::SetEndpoints(const FVector& InSource, const FVector& InTarget)
{
	SourcePoint = InSource;
	TargetPoint = InTarget;
	bHasExplicitEndpoints = true;
	ResetEndpointLocks();
}

void FParticleBeamEmitterInstance::ResetEndpointLocks()
{
	bHasLockedSourcePoint = false;
	bHasLockedTargetPoint = false;
}

// -- Ribbon ----
FDynamicEmitterDataBase* FParticleRibbonEmitterInstance::GetDynamicData()
{
	// Ribbon replay follows the shared/base replay path, then adds the single-trail
	// shaping contract that RT geometry build consumes for this emitter/frame.
	FDynamicRibbonEmitterData* Data = new FDynamicRibbonEmitterData();
	FillReplayData(Data->Source);
	Data->Source.EmitterType = EDynamicEmitterType::Ribbon;

	if (UParticleLODLevel* RenderReplayLOD = GetRenderReplayLODLevel())
	{
		FillRibbonReplayShapingFromRenderLOD(*RenderReplayLOD, Data->Source);
	}

	return Data;
}
