#include "ParticleSystemSceneProxy.h"
#include "Particle/ParticleHelper.h"
#include "Render/Types/FrameContext.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Particle/ParticleDynamicData.h"
#include "Component/ParticleSystemComponent.h"
#include "Materials/Material.h"
#include "Mesh/StaticMesh.h"
#include "Profiling/ParticleStats.h"
#include "Render/Shader/ShaderManager.h"

#include <algorithm>
#include <cmath>

namespace {
	bool ShouldSortEmitter(EBlendState BlendState) 
	{
		switch (BlendState)
		{
		case (EBlendState::Additive): 
		{
			return false;
		}
		case (EBlendState::AlphaBlend):
		case (EBlendState::Opaque):
		default:
			return true;
		}
	}

	FVector SafeNormalizeBeam(const FVector& Value, const FVector& Fallback)
	{
		const float LenSq = Value.Dot(Value);
		return (LenSq > 1e-6f) ? Value * (1.0f / std::sqrt(LenSq)) : Fallback;
	}

	FVector BuildStableRibbonSideAxis(const FVector& Candidate, const FVector& Tangent, const FVector& Fallback)
	{
		const FVector UnitTangent = SafeNormalizeBeam(Tangent, FVector::ForwardVector);
		auto ProjectPerpendicularToTangent = [&](const FVector& Axis)
		{
			return Axis - UnitTangent * Axis.Dot(UnitTangent);
		};

		FVector SideAxis = SafeNormalizeBeam(ProjectPerpendicularToTangent(Candidate), FVector::ZeroVector);
		if (SideAxis.Dot(SideAxis) > 1e-6f)
		{
			return SideAxis;
		}

		SideAxis = SafeNormalizeBeam(ProjectPerpendicularToTangent(Fallback), FVector::ZeroVector);
		if (SideAxis.Dot(SideAxis) > 1e-6f)
		{
			return SideAxis;
		}

		const FVector ReferenceAxis = std::abs(UnitTangent.Dot(FVector::UpVector)) < 0.95f
			? FVector::UpVector
			: FVector::RightVector;
		return SafeNormalizeBeam(UnitTangent.Cross(ReferenceAxis), FVector::RightVector);
	}

	FVector RotateAroundAxis(const FVector& Value, const FVector& UnitAxis, float Radians)
	{
		const float S = std::sin(Radians);
		const float C = std::cos(Radians);
		return Value * C
			+ UnitAxis.Cross(Value) * S
			+ UnitAxis * (UnitAxis.Dot(Value) * (1.0f - C));
	}

	FVector BuildRibbonSideAxis(const FFrameContext& Frame, ETrailsRenderAxisOption RenderAxis,
		const FVector& Position, const FVector& Tangent, const FVector& SourceUpVector, const FVector& Fallback)
	{
		if (RenderAxis == Trails_SourceUp)
		{
			return BuildStableRibbonSideAxis(SourceUpVector, Tangent, Fallback);
		}

		if (RenderAxis == Trails_WorldUp)
		{
			return BuildStableRibbonSideAxis(FVector::UpVector, Tangent, Fallback);
		}

		const FVector ToCamera = SafeNormalizeBeam(Frame.CameraPosition - Position, Frame.CameraForward * -1.0f);
		const FVector CameraUp = SafeNormalizeBeam(Frame.CameraUp, FVector::UpVector);
		return BuildStableRibbonSideAxis(ToCamera.Cross(Tangent), Tangent, CameraUp);
	}

	float ApplyBeamTaper(EBeamTaperMethod TaperMethod, float TaperFactor, float TaperScale, float Alpha)
	{
		Alpha = std::clamp(Alpha, 0.0f, 1.0f);
		TaperScale = std::max(0.0f, TaperScale);

		if (TaperMethod == PEBTM_Full)
		{
			return (1.0f - Alpha * (1.0f - TaperFactor)) * TaperScale;
		}

		if (TaperMethod == PEBTM_Partial)
		{
			if (Alpha <= TaperFactor)
			{
				return TaperScale;
			}
			const float Denom = std::max(1.0f - TaperFactor, 1e-6f);
			return (1.0f - (Alpha - TaperFactor) / Denom) * TaperScale;
		}

		return TaperScale;
	}

	uint64 EstimateBeamPackedBytes(const FDynamicBeamEmitterReplayData& Source)
	{
		constexpr int32 MaxSegmentsPerBeam = 256;
		constexpr int32 MaxSheetsPerBeam = 16;
		const int32 SegmentCount = std::clamp(std::max(1, Source.InterpolationPoints),
			1, MaxSegmentsPerBeam);
		const int32 PointCount = SegmentCount + 1;
		const int32 SheetCount = std::clamp(Source.Sheets,
			1, MaxSheetsPerBeam);
		const uint64 BeamCount = static_cast<uint64>(Source.Beams.size());
		const uint64 VertexCount = BeamCount * static_cast<uint64>(PointCount) * 2ull * static_cast<uint64>(SheetCount);
		const uint64 IndexCount = BeamCount * static_cast<uint64>(SegmentCount) * 6ull * static_cast<uint64>(SheetCount);
		return VertexCount * sizeof(FBeamParticleInstanceVertex) + IndexCount * sizeof(uint32);
	}

	uint64 EstimateRibbonPackedBytes(const FDynamicRibbonEmitterReplayData& Source)
	{
		constexpr int32 MaxSheetsPerTrail = 16;
		constexpr int32 MaxTessellationBetweenParticles = 32;
		const int32 SheetCount = std::clamp(Source.SheetsPerTrail,
			1, MaxSheetsPerTrail);
		const int32 MaxTessellation = std::clamp(Source.MaxTessellationBetweenParticles,
			0, MaxTessellationBetweenParticles);
		uint64 VertexCount = 0;
		uint64 IndexCount = 0;
		for (const FRibbonTrailSection& Trail : Source.Trails)
		{
			if (Trail.PointCount < 2)
			{
				continue;
			}
			const uint64 PointCount = 1ull + static_cast<uint64>(Trail.PointCount - 1)
				* static_cast<uint64>(MaxTessellation + 1);
			VertexCount += PointCount * 2ull * static_cast<uint64>(SheetCount);
			IndexCount += (PointCount - 1ull) * 6ull * static_cast<uint64>(SheetCount);
		}
		return VertexCount * sizeof(FRibbonParticleInstanceVertex) + IndexCount * sizeof(uint32);
	}

	FRibbonPointData InterpolateRibbonPoint(const FRibbonPointData& A, const FRibbonPointData& B, float Alpha)
	{
		const float T = std::clamp(Alpha, 0.0f, 1.0f);
		FRibbonPointData Result;
		Result.Position = A.Position + (B.Position - A.Position) * T;
		Result.Color = FLinearColor(
			A.Color.R + (B.Color.R - A.Color.R) * T,
			A.Color.G + (B.Color.G - A.Color.G) * T,
			A.Color.B + (B.Color.B - A.Color.B) * T,
			A.Color.A + (B.Color.A - A.Color.A) * T);
		Result.Width = A.Width + (B.Width - A.Width) * T;
		Result.DistanceFromStart = A.DistanceFromStart + (B.DistanceFromStart - A.DistanceFromStart) * T;
		Result.SpawnSequence = T < 0.5f ? A.SpawnSequence : B.SpawnSequence;
		return Result;
	}

	int32 GetRibbonSegmentStepCount(const FDynamicRibbonEmitterReplayData& Source,
		const FRibbonPointData& A, const FRibbonPointData& B)
	{
		constexpr int32 MaxTessellationBetweenParticles = 32;
		const int32 MaxTessellation = std::clamp(Source.MaxTessellationBetweenParticles,
			0, MaxTessellationBetweenParticles);
		const int32 MaxSteps = MaxTessellation + 1;
		int32 StepCount = MaxSteps;
		if (Source.DistanceTessellationStepSize > 1e-6f)
		{
			const float SegmentLength = (B.Position - A.Position).Length();
			StepCount = static_cast<int32>(std::ceil(SegmentLength / Source.DistanceTessellationStepSize));
			StepCount = std::clamp(StepCount, 1, MaxSteps);
		}
		return std::max(1, StepCount);
	}

	void BuildRibbonTessellatedPoints(const FDynamicRibbonEmitterReplayData& Source,
		const FRibbonTrailSection& Trail, TArray<FRibbonPointData>& OutPoints)
	{
		OutPoints.clear();
		if (Trail.PointCount < 2 || Trail.FirstPoint < 0)
		{
			return;
		}

		const int32 TrailEnd = Trail.FirstPoint + Trail.PointCount;
		if (TrailEnd > static_cast<int32>(Source.Points.size()))
		{
			return;
		}

		OutPoints.push_back(Source.Points[Trail.FirstPoint]);
		for (int32 PointIdx = 0; PointIdx < Trail.PointCount - 1; ++PointIdx)
		{
			const FRibbonPointData& A = Source.Points[Trail.FirstPoint + PointIdx];
			const FRibbonPointData& B = Source.Points[Trail.FirstPoint + PointIdx + 1];
			const int32 StepCount = GetRibbonSegmentStepCount(Source, A, B);
			for (int32 StepIdx = 1; StepIdx <= StepCount; ++StepIdx)
			{
				const float Alpha = static_cast<float>(StepIdx) / static_cast<float>(StepCount);
				OutPoints.push_back(InterpolateRibbonPoint(A, B, Alpha));
			}
		}
	}

	FVector EvaluateBeamCurve(const FBeamInstanceData& Beam, float Alpha)
	{
		const float T = std::clamp(Alpha, 0.0f, 1.0f);
		const float InvT = 1.0f - T;
		// Hermite convention: tangents are velocities along the curve, so the
		// second control point pulls *backward* from the target (negate sign).
		const FVector SourceControl = Beam.Source + Beam.SourceTangent * (std::max(0.0f, Beam.SourceStrength) / 3.0f);
		const FVector TargetControl = Beam.Target - Beam.TargetTangent * (std::max(0.0f, Beam.TargetStrength) / 3.0f);

		return Beam.Source * (InvT * InvT * InvT)
			+ SourceControl * (3.0f * InvT * InvT * T)
			+ TargetControl * (3.0f * InvT * T * T)
			+ Beam.Target * (T * T * T);
	}

}

FParticleSystemSceneProxy::FParticleSystemSceneProxy(UParticleSystemComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::Particle;

	// Per-frame CPU pack (sort + quad expansion) needs the FrameContext for
	// camera-dependent sort order. Required for RenderCollector to invoke
	// UpdatePerViewport on this proxy each frame.
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate;

	// Bounds for particle systems are owned by UParticleSystem and FParticleEmitterInstance (dynamic per-frame bounds).
	// Until that's wired up on the CPU side, opt out of frustum + occlusion culling
	ProxyFlags |= EPrimitiveProxyFlags::NeverCull;
	ProxyFlags &= ~EPrimitiveProxyFlags::ShowAABB;
}

FParticleSystemSceneProxy::~FParticleSystemSceneProxy()
{
	for (FDynamicEmitterDataBase* P : DynamicData)
	{
		delete P;
	}
	DynamicData.clear();
}

void FParticleSystemSceneProxy::UpdateDynamicData(TArray<FDynamicEmitterDataBase*>&& NewData)
{
	for (FDynamicEmitterDataBase* Old : DynamicData)
	{
		delete Old;
	}
	DynamicData = std::move(NewData);

	if (EmitterDraws.size() != DynamicData.size())
	{
		bIsEmitterOrderDirty = true;
		EmitterDraws.resize(DynamicData.size());
	}
}

void FParticleSystemSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	ComponentToWorld = GetOwner()->GetWorldMatrix();
	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(FMatrix::Identity);
	MarkPerObjectCBDirty();
}

void FParticleSystemSceneProxy::UpdateMaterial()
{
	for (uint32 i = 0; i < DynamicData.size(); i++)
	{
		const auto& Source = static_cast<const FDynamicRenderableEmitterReplayDataBase&>(
			DynamicData[i]->GetSource());
		const FDynamicRenderableEmitterReplayDataBase* RenderableSource =
			dynamic_cast<const FDynamicRenderableEmitterReplayDataBase*>(&Source);

		auto& Draw = EmitterDraws[i];
		Draw.Material = RenderableSource && RenderableSource->MaterialInterface
			? RenderableSource->MaterialInterface->GetMaterial()
			: nullptr;
		Draw.Type	 = Source.eEmitterType;
		Draw.EmitterIndex = DynamicData[i]->EmitterIndex;
		if (Draw.SortingPriority != Source.EmitterSortPriority)
		{
			Draw.SortingPriority = Source.EmitterSortPriority;
			bIsEmitterOrderDirty = true;
		}
		Draw.SetParticleBlendRoute(Source.BlendMode);
		UpdateCB(EmitterDraws[i], Source);
	}
}

void FParticleSystemSceneProxy::UpdateVisibility()
{
	FPrimitiveSceneProxy::UpdateVisibility();
	bCastShadow = false;
}

void FParticleSystemSceneProxy::UpdateMesh()
{
	UpdateMaterial();

	for (size_t i = 0; i < DynamicData.size(); ++i)
	{
		FEmitterDraw& Draw = EmitterDraws[i];

		switch(Draw.Type)
		{
		case (DET_Sprite):
		{
			Draw.FirstIndex = 0;
			Draw.IndexCount = 0;
			break;
		}
		case (DET_Beam2):
		{
			Draw.FirstIndex = 0;
			Draw.IndexCount = 0;
			break;
		}
		case (DET_Ribbon):
		{
			Draw.FirstIndex = 0;
			Draw.IndexCount = 0;
			break;
		}
		case (DET_Mesh):
		{
			const auto& Source = static_cast<const FDynamicMeshEmitterReplayData&>(
				DynamicData[i]->GetSource());
			const uint32 ParticleCount = static_cast<uint32>(Source.ActiveParticleCount);

			// Mesh path: section's index range is the static mesh's own IB.
			// FirstIndex/IndexCount come from MeshGeom, and InstanceCount = ParticleCount.
			Draw.FirstIndex = 0;
			Draw.InstanceCount = ParticleCount;
			Draw.MeshGeom = Source.StaticMesh ? Source.StaticMesh->GetLODMeshBuffer(Source.LODLevel) : nullptr;
			Draw.IndexCount = Draw.MeshGeom ? Draw.MeshGeom->GetIndexBuffer().GetIndexCount() : 0;
			break;
		}
		}
	}

	if (bIsEmitterOrderDirty)
	{
		SortEmitters();
	}
}

// UpdatePerViewport: per-frame CPU work
// Runs in RenderCollector::Collect, gated by EPrimitiveProxyFlags::PerViewportUpdate.
void FParticleSystemSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	// Reset scratch arrays. Sprite scratch is shared, mesh scratch is per-emitter.
	SpritePacker.ResetFrame();
	MeshPacker.ResetFrame(EmitterDraws);
	BeamPacker.ResetFrame();
	RibbonPacker.ResetFrame();

	if (!bIsOwnerVisible) return;

	if (DynamicData.empty())
	{
		bVisible = false;
		return;
	}

	// CPU pack: sort, quad expansion, and mesh instance payloads.
	PackParticles(Frame);

	bVisible = bInstancePacked;
	bInstancePacked = false;
	if (!bVisible) return;

	SpritePacker.MarkGpuBuffersDirty();
}

// PrepareDrawBuffer: GPU upload + bind hand-off
bool FParticleSystemSceneProxy::PrepareDrawBuffer(
	ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, FDrawCommandBuffer& Out) const
{
	FDrawCommandBuffer SpriteBuffer;
	const bool bHasSpriteBuffer = SpritePacker.HasPackedSprites()
		&& SpritePacker.PrepareDrawBuffer(InDevice, InDeviceContext, SpriteBuffer);

	FDrawCommandBuffer BeamBuffer;
	const bool bHasBeamBuffer = BeamPacker.PrepareDrawBuffer(InDevice, InDeviceContext, BeamBuffer);

	FDrawCommandBuffer RibbonBuffer;
	const bool bHasRibbonBuffer = RibbonPacker.PrepareDrawBuffer(InDevice, InDeviceContext, RibbonBuffer);

	if (SpritePacker.HasPackedSprites())
	{
		if (bHasSpriteBuffer)
		{
			Out = SpriteBuffer;
			return true;
		}
	}

	if (bHasBeamBuffer)
	{
		Out = BeamBuffer;
		return true;
	}

	if (bHasRibbonBuffer)
	{
		Out = RibbonBuffer;
		return true;
	}

	for (const FEmitterDraw& Draw : EmitterDraws)
	{
		if (Draw.Type == DET_Mesh && Draw.MeshGeom && Draw.InstanceCount > 0)
		{
			Out.VB         = Draw.MeshGeom->GetVertexBuffer().GetBuffer();
			Out.VBStride   = Draw.MeshGeom->GetVertexBuffer().GetStride();
			Out.IB         = Draw.MeshGeom->GetIndexBuffer().GetBuffer();
			Out.BaseVertex = 0;
			break;
		}
	}

	return Out.VB != nullptr && Out.IB != nullptr;
}

void FParticleSystemSceneProxy::SetEmitterSortingPriority(uint16 EmitterIndex, uint16 InPriority)
{
	if (EmitterIndex >= EmitterDraws.size()) return;
	EmitterDraws[EmitterIndex].SortingPriority = InPriority;
	bIsEmitterOrderDirty = true;
}

void FParticleSystemSceneProxy::SortEmitters()
{
	PARTICLE_SCOPE_STAT(EParticleStatTimer::SortEmitters);
	const uint16 Count = static_cast<uint16>(EmitterDraws.size());
	SectionToEmitterDrawIndex.resize(Count);
	for (uint16 i = 0; i < Count; ++i)
	{
		SectionToEmitterDrawIndex[i] = i;
	}

	// Ascending: lower SortingPriority draws first, higher draws on top.
	std::stable_sort(SectionToEmitterDrawIndex.begin(), SectionToEmitterDrawIndex.end(),
		[this](uint16 A, uint16 B)
		{
			return EmitterDraws[A].SortingPriority < EmitterDraws[B].SortingPriority;
		});

	bIsEmitterOrderDirty = false;
}

void FParticleSystemSceneProxy::RebuildSectionDraws()
{
	PARTICLE_SCOPE_STAT(EParticleStatTimer::RebuildSections);
	if (SectionToEmitterDrawIndex.size() != EmitterDraws.size())
	{
		SortEmitters();
	}

	SectionDraws.clear();
	SectionDraws.reserve(SectionToEmitterDrawIndex.size());

	for (uint16 DrawIndex : SectionToEmitterDrawIndex)
	{
		if (DrawIndex >= EmitterDraws.size())
		{
			continue;
		}

		const FEmitterDraw& Draw = EmitterDraws[DrawIndex];
		FMeshSectionDraw SectionDraw;
		SectionDraw.Material		= Draw.Material;
		SectionDraw.FirstIndex		= Draw.FirstIndex;
		SectionDraw.IndexCount		= Draw.IndexCount;
		SectionDraw.PassOverride	= Draw.ParticleBlendState.RenderPass;
		SectionDraw.BlendOverride	= Draw.ParticleBlendState.BlendState;
		SectionDraws.push_back(SectionDraw);
	}
}

void FParticleSystemSceneProxy::PackParticles(const FFrameContext& Frame)
{
	// Rebuild Emitter order before packing vertices
	if (bIsEmitterOrderDirty) {
		SortEmitters();
	}

	uint32 IndexCursor = 0;
	for (size_t i = 0; i < SectionToEmitterDrawIndex.size(); ++i)
	{
		const uint16 DrawIndex = SectionToEmitterDrawIndex[i];
		if (DrawIndex >= EmitterDraws.size() || DrawIndex >= DynamicData.size()) continue;
		FEmitterDraw& Draw = EmitterDraws[DrawIndex];

		switch (Draw.Type)
		{
		case DET_Sprite:
		{
			const uint32 IndexBefore = IndexCursor;
			FDynamicSpriteEmitterData& SpriteData = static_cast<FDynamicSpriteEmitterData&>(*DynamicData[DrawIndex]);
			{
				PARTICLE_SCOPE_STAT(EParticleStatTimer::PackSprites);
				SpritePacker.PackEmitter(Frame, SpriteData, Draw, IndexCursor);
			}

			// Refresh section range so DrawCommandBuilder sees the right slice
			// even if ActiveParticleCount shrank since UpdateMesh.
			Draw.FirstIndex = IndexBefore;
			Draw.IndexCount = IndexCursor - IndexBefore;
			const uint32 PackedParticleCount = Draw.IndexCount / 6u;
			const uint64 PackedBytes = static_cast<uint64>(PackedParticleCount)
				* (4ull * sizeof(FParticleSpriteVertex) + 6ull * sizeof(uint32));
			FParticleStats::Get().RecordPacking(DET_Sprite, PackedParticleCount, PackedBytes);
			break;
		}
		case DET_Mesh:
		{
			FDynamicMeshEmitterData& MeshData = static_cast<FDynamicMeshEmitterData&>(*DynamicData[DrawIndex]);
			{
				PARTICLE_SCOPE_STAT(EParticleStatTimer::PackMeshes);
				MeshPacker.PackEmitter(Frame, MeshData, Draw);
			}
			FParticleStats::Get().RecordPacking(
				DET_Mesh,
				Draw.InstanceCount,
				static_cast<uint64>(Draw.PackedInstances.size()) * sizeof(FMeshParticleInstanceVertex));
			// Mesh section range stays as UpdateMesh set it (static-mesh IB range
			// is fixed; InstanceCount is the per-frame variable).
			break;
		}
		case DET_Beam2:
		{
			FDynamicBeamEmitterData& BeamData = static_cast<FDynamicBeamEmitterData&>(*DynamicData[DrawIndex]);
			{
				PARTICLE_SCOPE_STAT(EParticleStatTimer::PackBeams);
				BeamPacker.PackEmitter(Frame, BeamData, Draw);
			}
			FParticleStats::Get().RecordPacking(
				DET_Beam2,
				static_cast<uint32>(std::max(0, BeamData.BeamSource.ActiveParticleCount)),
				Draw.IndexCount > 0 ? EstimateBeamPackedBytes(BeamData.BeamSource) : 0);
			break;
		}
		case DET_Ribbon:
		{
			FDynamicRibbonEmitterData& RibbonData = static_cast<FDynamicRibbonEmitterData&>(*DynamicData[DrawIndex]);
			{
				PARTICLE_SCOPE_STAT(EParticleStatTimer::PackRibbons);
				RibbonPacker.PackEmitter(Frame, RibbonData, Draw);
			}
			FParticleStats::Get().RecordPacking(
				DET_Ribbon,
				static_cast<uint32>(std::max(0, RibbonData.RibbonSource.ActiveParticleCount)),
				Draw.IndexCount > 0 ? EstimateRibbonPackedBytes(RibbonData.RibbonSource) : 0);
			break;
		}
		default:
			break;
		}

		if (!Draw.PackedInstances.empty()
			|| SpritePacker.HasPackedSprites()
			|| (Draw.Type == DET_Beam2 && Draw.IndexCount > 0)
			|| (Draw.Type == DET_Ribbon && Draw.IndexCount > 0))
		{
			bInstancePacked = true;
		}
	}

	RebuildSectionDraws();
}

bool FParticleSystemSceneProxy::PrepareDrawCommandBindings(ID3D11Device* InDevice,
	ID3D11DeviceContext* InDeviceContext,
	const FPrimitiveDrawOptions&, FDrawCommand& Cmd, int32 SectionIndex) const
{
	if (!bIsOwnerVisible || SectionIndex < 0 || SectionIndex >= static_cast<int32>(SectionToEmitterDrawIndex.size()))
	{
		return false;
	}

	const uint16 DrawIndex = SectionToEmitterDrawIndex[SectionIndex];
	if (DrawIndex >= EmitterDraws.size())
	{
		return false;
	}

	const FEmitterDraw& Hit = EmitterDraws[DrawIndex];

	// Upload CB if dirty
	if (Hit.bParticleParamCBDirty)
	{
		if (!Hit.ParticleParamCB.GetBuffer())
		{
			Hit.ParticleParamCB.Create(
				InDevice,
				sizeof(FParticleParamConstants),
				"ParticleParamCB");
		}

		Hit.ParticleParamCB.Update(
			InDeviceContext,
			&Hit.ParticleParams,
			sizeof(FParticleParamConstants));

		Hit.bParticleParamCBDirty = false;
	}
	Cmd.Bindings.PerShaderCB[0] = &Hit.ParticleParamCB;

	FVector4 MaterialColor(1.0f, 1.0f, 1.0f, 1.0f);
	if (Hit.Material)
	{
		Hit.Material->GetVector4Parameter("SectionColor", MaterialColor);
	}
	const uint32 HasDiffuseTexture = Hit.Material && Hit.Material->GetCachedSRVs()[(int)EMaterialTextureSlot::Diffuse] ? 1u : 0u;
	if (Hit.ParticleMaterialParams.MaterialColor.X != MaterialColor.X
		|| Hit.ParticleMaterialParams.MaterialColor.Y != MaterialColor.Y
		|| Hit.ParticleMaterialParams.MaterialColor.Z != MaterialColor.Z
		|| Hit.ParticleMaterialParams.MaterialColor.W != MaterialColor.W
		|| Hit.ParticleMaterialParams.HasDiffuseTexture != HasDiffuseTexture)
	{
		Hit.ParticleMaterialParams.MaterialColor = MaterialColor;
		Hit.ParticleMaterialParams.HasDiffuseTexture = HasDiffuseTexture;
		Hit.bParticleMaterialCBDirty = true;
	}

	if (Hit.bParticleMaterialCBDirty)
	{
		if (!Hit.ParticleMaterialCB.GetBuffer())
		{
			Hit.ParticleMaterialCB.Create(
				InDevice,
				sizeof(FParticleMaterialConstants),
				"ParticleMaterialCB");
		}

		Hit.ParticleMaterialCB.Update(
			InDeviceContext,
			&Hit.ParticleMaterialParams,
			sizeof(FParticleMaterialConstants));

		Hit.bParticleMaterialCBDirty = false;
	}
	Cmd.Bindings.PerShaderCB[1] = &Hit.ParticleMaterialCB;

	if (Hit.Type == DET_Mesh && Hit.MeshGeom && Hit.InstanceCount > 0)
	{
		Cmd.Shader = FShaderManager::Get().GetOrCreate(EShaderPath::ParticleMesh);

		if (Hit.bInstanceVBDirty && !Hit.PackedInstances.empty())
		{
			PARTICLE_SCOPE_STAT(EParticleStatTimer::UploadMeshInstances);
			const uint32 Count = static_cast<uint32>(Hit.PackedInstances.size());
			if (Hit.InstanceVB.GetMaxCount() == 0)
			{
				Hit.InstanceVB.Create(InDevice, Count, sizeof(FMeshParticleInstanceVertex));
			}
			Hit.InstanceVB.EnsureCapacity(InDevice, Count);
			Hit.InstanceVB.Update(InDeviceContext, Hit.PackedInstances.data(), Count);
			Hit.bInstanceVBDirty = false;
		}

		Cmd.Buffer.VB                = Hit.MeshGeom->GetVertexBuffer().GetBuffer();
		Cmd.Buffer.VBStride          = Hit.MeshGeom->GetVertexBuffer().GetStride();
		Cmd.Buffer.IB                = Hit.MeshGeom->GetIndexBuffer().GetBuffer();
		Cmd.Buffer.FirstIndex        = 0;
		Cmd.Buffer.IndexCount        = Hit.IndexCount;
		Cmd.Buffer.BaseVertex        = 0;

		Cmd.Buffer.InstanceVB        = Hit.InstanceVB.GetBuffer();
		Cmd.Buffer.InstanceVBStride  = sizeof(FMeshParticleInstanceVertex);
		Cmd.Buffer.InstancedCount    = Hit.InstanceCount;
		Cmd.Buffer.InstanceStart     = 0;
	}
	else if (Hit.Type == DET_Beam2 && Hit.IndexCount > 0)
	{
		Cmd.Buffer.VB         = BeamPacker.GetVertexBuffer();
		Cmd.Buffer.VBStride   = sizeof(FBeamParticleInstanceVertex);
		Cmd.Buffer.IB         = BeamPacker.GetIndexBuffer();
		Cmd.Buffer.FirstIndex = Hit.FirstIndex;
		Cmd.Buffer.IndexCount = Hit.IndexCount;
		Cmd.Buffer.BaseVertex = 0;
		if (!Cmd.Buffer.VB || !Cmd.Buffer.IB)
		{
			return false;
		}
	}
	else if (Hit.Type == DET_Ribbon && Hit.IndexCount > 0)
	{
		// TODO: Remove once material domain compatibility is enforced by the editor/material system.
		Cmd.Shader = FShaderManager::Get().GetOrCreate(EShaderPath::ParticleRibbon);
		Cmd.RenderState.Rasterizer = ERasterizerState::SolidNoCull;

		Cmd.Buffer.VB         = RibbonPacker.GetVertexBuffer();
		Cmd.Buffer.VBStride   = sizeof(FRibbonParticleInstanceVertex);
		Cmd.Buffer.IB         = RibbonPacker.GetIndexBuffer();
		Cmd.Buffer.FirstIndex = Hit.FirstIndex;
		Cmd.Buffer.IndexCount = Hit.IndexCount;
		Cmd.Buffer.BaseVertex = 0;
		if (!Cmd.Buffer.VB || !Cmd.Buffer.IB)
		{
			return false;
		}
	}
	return true;
}

//============================================================================
//	Sprite Packer
//============================================================================
bool FParticleSystemSceneProxy::FSpriteParticlePacker::PrepareDrawBuffer(
	ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, FDrawCommandBuffer& Out) const
{
	PARTICLE_SCOPE_STAT(EParticleStatTimer::UploadSpriteBuffers);
	if (!HasPackedSprites())
	{
		return false;
	}

	const uint32 VertCount  = static_cast<uint32>(PackedVertices.size());
	const uint32 IndexCount = static_cast<uint32>(IndexPattern.size());

	if (VertexBuffer.GetMaxCount() == 0)
	{
		VertexBuffer.Create(InDevice, VertCount, sizeof(FParticleSpriteVertex));
	}
	else
	{
		VertexBuffer.EnsureCapacity(InDevice, VertCount);
	}

	// NOTE: FDynamicVertexBuffer::Update takes ELEMENT count, not byte count.
	if (bGpuBuffersDirty)
	{
		VertexBuffer.Update(InDeviceContext, PackedVertices.data(), VertCount);
		bGpuBuffersDirty = false;
	}

	if (bIndexBufferDirty)
	{
		if (IndexBuffer.GetMaxCount() == 0)
		{
			IndexBuffer.Create(InDevice, IndexCount);
		}
		else
		{
			IndexBuffer.EnsureCapacity(InDevice, IndexCount);
		}

		IndexBuffer.Update(InDeviceContext, IndexPattern.data(), IndexCount);
		bIndexBufferDirty = false;
	}

	Out.VB         = VertexBuffer.GetBuffer();
	Out.VBStride   = sizeof(FParticleSpriteVertex);
	Out.IB         = IndexBuffer.GetBuffer();
	Out.BaseVertex = 0;
	return Out.VB != nullptr && Out.IB != nullptr;
}

void FParticleSystemSceneProxy::FSpriteParticlePacker::PackEmitter(const FFrameContext& Frame, FDynamicSpriteEmitterData& Emitter, const FEmitterDraw& Draw, uint32& IndexCursor)
{
	const FDynamicSpriteEmitterReplayData& Source = Emitter.Source;
	const int32 Count = Source.ActiveParticleCount;
	if (Count <= 0 ||
		!Source.DataContainer.ParticleData ||
		!Source.DataContainer.ParticleIndices ||
		Source.ParticleStride < static_cast<int32>(sizeof(FBaseParticle)))
	{
		return;
	}

	TArray<uint16> SortedParticleIndices(Source.DataContainer.ParticleIndices, Source.DataContainer.ParticleIndices + Count);

	if (ShouldSortEmitter(Emitter.Source.BlendMode)) {
		Emitter.SortParticles(Source.SortMode, Frame.CameraPosition, Frame.CameraForward, FMatrix::Identity,
			SortedParticleIndices.data(), Count, Source.DataContainer.ParticleData, Source.ParticleStride);
	}

	const uint32 ParticleCount = static_cast<uint32>(Count);
	const uint32 FirstParticle = IndexCursor / 6;
	EnsureIndexPattern(FirstParticle + ParticleCount);

	const int32 SubImageColumns = std::max(1, static_cast<int32>(Draw.ParticleParams.SubUVCols));
	const int32 SubImageRows = std::max(1, static_cast<int32>(Draw.ParticleParams.SubUVRows));
	const int32 SubImageCount = std::max(1, SubImageColumns * SubImageRows);

	PackedVertices.reserve(PackedVertices.size() + Count * 4);
	for (int32 i = 0; i < Count; ++i)
	{
		const uint16 Idx = SortedParticleIndices[i];
		const uint8* Bytes = Source.DataContainer.ParticleData + Idx * Source.ParticleStride;
		const FBaseParticle& P = *reinterpret_cast<const FBaseParticle*>(Bytes);
		
		// 벚꽃처럼 각 파티클이 고정된 하나의 랜덤 꽃잎을 가지게 하기 위해
		// 나이(Age) 기반 애니메이션 대신 ParticleId를 이용한 고정 랜덤 인덱스를 사용합니다.
		const int32 SubImageIndex = P.ParticleId % SubImageCount;

		for (int corner = 0; corner < 4; ++corner)
		{
			FParticleSpriteVertex V;
			V.Position = P.Location;
			V.Size = FVector(P.Size.X, P.Size.Y, /*subImageLerp*/ 0.0f);
			V.UV = FVector2{ float(corner & 1), float((corner >> 1) & 1) };  // 0,0..1,1
			V.Color = FVector4(P.Color.R, P.Color.G, P.Color.B, P.Color.A);
			V.Rotation = P.Rotation.Z;
			V.SubImageIndex = static_cast<float>(SubImageIndex);
			V.Velocity = P.Velocity;
			PackedVertices.push_back(V);
		}
	}

	IndexCursor += ParticleCount * 6;
}

//============================================================================
//	Mesh Packer
//============================================================================
void FParticleSystemSceneProxy::FMeshParticlePacker::ResetFrame(TArray<FEmitterDraw>& EmitterDraws)
{
	for (FEmitterDraw& Draw : EmitterDraws)
	{
		if (Draw.Type == DET_Mesh)
		{
			Draw.PackedInstances.clear();
		}
	}
}

bool FParticleSystemSceneProxy::FMeshParticlePacker::HasPackedInstances(const TArray<FEmitterDraw>& EmitterDraws) const
{
	for (const FEmitterDraw& Draw : EmitterDraws)
	{
		if (Draw.Type == DET_Mesh && !Draw.PackedInstances.empty())
		{
			return true;
		}
	}
	return false;
}

void FParticleSystemSceneProxy::FMeshParticlePacker::PackEmitter(const FFrameContext& Frame,
	FDynamicMeshEmitterData& Emitter, FEmitterDraw& Draw)
{
	const FDynamicMeshEmitterReplayData& Source = Emitter.MeshSource;
	const int32 Count = Source.ActiveParticleCount;
	Draw.InstanceCount = static_cast<uint32>(Count > 0 ? Count : 0);

	if (Count <= 0 ||
		!Source.DataContainer.ParticleData ||
		!Source.DataContainer.ParticleIndices ||
		Source.ParticleStride < static_cast<int32>(sizeof(FBaseParticle)))
	{
		return;
	}

	Draw.PackedInstances.clear();
	Draw.PackedInstances.reserve(Count);

	TArray<uint16> SortedParticleIndices(Source.DataContainer.ParticleIndices, Source.DataContainer.ParticleIndices + Count);

	if (ShouldSortEmitter(Emitter.MeshSource.BlendMode)) {
		Emitter.SortParticles(Source.SortMode, Frame.CameraPosition, Frame.CameraForward, FMatrix::Identity,
			SortedParticleIndices.data(), Count, Source.DataContainer.ParticleData, Source.ParticleStride);
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const uint16 Idx = SortedParticleIndices[i];
		const uint8* Bytes = Source.DataContainer.ParticleData + Idx * Source.ParticleStride;
		const FBaseParticle& P = *reinterpret_cast<const FBaseParticle*>(Bytes);

		const FMatrix Model = FMatrix::MakeScaleMatrix(P.Size)
		                    * FMatrix::MakeRotationX(P.Rotation.X)
		                    * FMatrix::MakeRotationY(P.Rotation.Y)
		                    * FMatrix::MakeRotationZ(P.Rotation.Z)
		                    * FMatrix::MakeTranslationMatrix(P.Location);

		FMeshParticleInstanceVertex V;
		V.Transform    = Model;
		V.Color        = FVector4(P.Color.R, P.Color.G, P.Color.B, P.Color.A);
		V.DynamicParam = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
		Draw.PackedInstances.push_back(V);
	}

	Draw.bInstanceVBDirty = true;
}


//============================================================================
//	Beam Packer — CPU-expanded dynamic geometry.
//============================================================================
void FParticleSystemSceneProxy::FBeamParticlePacker::ResetFrame()
{
	PackedVertices.clear();
	PackedIndices.clear();
	bGpuBuffersDirty = true;
}

bool FParticleSystemSceneProxy::FBeamParticlePacker::PrepareDrawBuffer(
	ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, FDrawCommandBuffer& Out) const
{
	PARTICLE_SCOPE_STAT(EParticleStatTimer::UploadBeamBuffers);
	if (!HasPackedBeams())
	{
		return false;
	}

	const uint32 VertCount = static_cast<uint32>(PackedVertices.size());
	const uint32 IndexCount = static_cast<uint32>(PackedIndices.size());

	if (VertexBuffer.GetMaxCount() == 0)
	{
		VertexBuffer.Create(InDevice, VertCount, sizeof(FBeamParticleInstanceVertex));
	}
	else
	{
		VertexBuffer.EnsureCapacity(InDevice, VertCount);
	}

	if (IndexBuffer.GetMaxCount() == 0)
	{
		IndexBuffer.Create(InDevice, IndexCount);
	}
	else
	{
		IndexBuffer.EnsureCapacity(InDevice, IndexCount);
	}

	if (bGpuBuffersDirty)
	{
		if (!VertexBuffer.Update(InDeviceContext, PackedVertices.data(), VertCount))
		{
			return false;
		}
		if (!IndexBuffer.Update(InDeviceContext, PackedIndices.data(), IndexCount))
		{
			return false;
		}
		bGpuBuffersDirty = false;
	}

	Out.VB = VertexBuffer.GetBuffer();
	Out.VBStride = sizeof(FBeamParticleInstanceVertex);
	Out.IB = IndexBuffer.GetBuffer();
	Out.BaseVertex = 0;
	return Out.VB != nullptr && Out.IB != nullptr;
}

void FParticleSystemSceneProxy::FBeamParticlePacker::PackEmitter(const FFrameContext& Frame, FDynamicBeamEmitterData& Emitter, FEmitterDraw& Draw)
{
	const FDynamicBeamEmitterReplayData& Source = Emitter.BeamSource;
	Draw.FirstIndex = static_cast<uint32>(PackedIndices.size());
	Draw.IndexCount = 0;

	if (!Source.bRenderGeometry || Source.Beams.empty())
		return;

	const int32 BaseSegmentCount = std::clamp(std::max(1, Source.InterpolationPoints),
		1, static_cast<int32>(MaxSegmentsPerBeam));
	const int32 BasePointCount = BaseSegmentCount + 1;
	const int32 SheetCount = std::clamp(Source.Sheets,
		1, static_cast<int32>(MaxSheetsPerBeam));

	// Reserve assuming non-noise beams; noise polylines may exceed this and
	// trigger a normal reallocation. Hint only.
	const uint32 VertsPerBeam   = static_cast<uint32>(BasePointCount) * 2u * static_cast<uint32>(SheetCount);
	const uint32 IndicesPerBeam = static_cast<uint32>(BaseSegmentCount) * 6u * static_cast<uint32>(SheetCount);
	PackedVertices.reserve(PackedVertices.size() + VertsPerBeam   * Source.Beams.size());
	PackedIndices.reserve (PackedIndices.size()  + IndicesPerBeam * Source.Beams.size());

	constexpr float Pi = 3.14159265358979323846f;
	uint32 IndicesEmitted = 0;

	for (size_t BeamIndex = 0; BeamIndex < Source.Beams.size(); ++BeamIndex)
	{
		const FBeamInstanceData& Beam = Source.Beams[BeamIndex];
		// Centerline: either noise-driven polyline, or Cascade-style tangent curve.
		TArray<FVector> Polyline;
		if (!Beam.NoisePoints.empty())
		{
			Polyline.reserve(Beam.NoisePoints.size() + 2);
			Polyline.push_back(Beam.Source);
			for (const FVector& NP : Beam.NoisePoints) Polyline.push_back(NP);
			Polyline.push_back(Beam.Target);
		}
		else
		{
			Polyline.reserve(BasePointCount);
			for (int32 PointIdx = 0; PointIdx < BasePointCount; ++PointIdx)
			{
				const float T = static_cast<float>(PointIdx) /
					static_cast<float>(std::max(BasePointCount - 1, 1));
				Polyline.push_back(EvaluateBeamCurve(Beam, T));
			}
		}

		const int32 PolylineCount = static_cast<int32>(Polyline.size());
		TArray<float> CumLen(PolylineCount, 0.0f);
		for (int32 i = 1; i < PolylineCount; ++i)
		{
			CumLen[i] = CumLen[i - 1] + (Polyline[i] - Polyline[i - 1]).Length();
		}
		const float TotalLen = CumLen[PolylineCount - 1];
		if (TotalLen <= 1e-6f)
			continue;

		const float  Progress   = std::clamp(Beam.BeamProgress, 0.0f, 1.0f);
		const float  VisibleLen = TotalLen * Progress;

		// Tessellation: InterpolationPoints is the requested step count.
		// Noise points shape the path, but this value decides how densely that
		// path is sampled for geometry.
		const int32 ThisPointCount = BasePointCount;
		const int32 ThisSegmentCount = ThisPointCount - 1;

		const FVector4 PackedColor(Beam.Color.X, Beam.Color.Y, Beam.Color.Z,
			std::clamp(Beam.Alpha, 0.0f, 1.0f));

		// Sample the polyline at the given arc length. Returns the interpolated
		// world-space center plus the local segment direction (un-normalized).
		auto SampleAt = [&](float Arc, FVector& OutCenter, FVector& OutEdgeDir)
		{
			int32 Idx = 0;
			while (Idx + 1 < PolylineCount && CumLen[Idx + 1] < Arc) ++Idx;
			if (Idx >= PolylineCount - 1) Idx = PolylineCount - 2;
			const float SegLen = CumLen[Idx + 1] - CumLen[Idx];
			const float Local  = (SegLen > 1e-6f) ? (Arc - CumLen[Idx]) / SegLen : 0.0f;
			OutCenter  = Polyline[Idx] + (Polyline[Idx + 1] - Polyline[Idx]) * Local;
			OutEdgeDir = Polyline[Idx + 1] - Polyline[Idx];
		};

		for (int32 SheetIdx = 0; SheetIdx < SheetCount; ++SheetIdx)
		{
			const uint32 SheetVertexBase = static_cast<uint32>(PackedVertices.size());
			for (int32 PointIdx = 0; PointIdx < ThisPointCount; ++PointIdx)
			{
				const float T = static_cast<float>(PointIdx) /
					static_cast<float>(std::max(ThisPointCount - 1, 1));
				const float ArcAt = VisibleLen * T;

				FVector Center, EdgeDir;
				SampleAt(ArcAt, Center, EdgeDir);
				const FVector LocalDir = SafeNormalizeBeam(EdgeDir, FVector::UpVector);

				const float Taper = ApplyBeamTaper(Beam.TaperMethod, Beam.TaperFactor, Beam.TaperScale, T);
				const float HalfWidth = std::max(0.0f, Beam.Width * Taper) * 0.5f;

				const FVector ToCamera = SafeNormalizeBeam(Frame.CameraPosition - Center, FVector::UpVector);
				FVector SideAxis = SafeNormalizeBeam(ToCamera.Cross(LocalDir), FVector::ForwardVector);
				if (SheetIdx > 0)
				{
					const float SheetAngle = Pi * static_cast<float>(SheetIdx) / static_cast<float>(SheetCount);
					SideAxis = SafeNormalizeBeam(RotateAroundAxis(SideAxis, LocalDir, SheetAngle), SideAxis);
				}

				const float U = (Source.TextureTileDistance > 0.0f)
					? ArcAt / Source.TextureTileDistance
					: T * static_cast<float>(std::max(1, Source.TextureTile));

				FBeamParticleInstanceVertex Left;
				Left.Position = Center - SideAxis * HalfWidth;
				Left.UV       = FVector2(U, 0.0f);
				Left.Color    = PackedColor;
				PackedVertices.push_back(Left);

				FBeamParticleInstanceVertex Right;
				Right.Position = Center + SideAxis * HalfWidth;
				Right.UV       = FVector2(U, 1.0f);
				Right.Color    = PackedColor;
				PackedVertices.push_back(Right);
			}

			for (int32 SegIdx = 0; SegIdx < ThisSegmentCount; ++SegIdx)
			{
				const uint32 P0Left  = SheetVertexBase + static_cast<uint32>(SegIdx) * 2u;
				const uint32 P0Right = P0Left + 1u;
				const uint32 P1Left  = P0Left + 2u;
				const uint32 P1Right = P0Left + 3u;

				PackedIndices.push_back(P0Left);
				PackedIndices.push_back(P1Left);
				PackedIndices.push_back(P0Right);
				PackedIndices.push_back(P1Left);
				PackedIndices.push_back(P1Right);
				PackedIndices.push_back(P0Right);
			}
		}

		IndicesEmitted += static_cast<uint32>(ThisSegmentCount) * 6u * static_cast<uint32>(SheetCount);
	}

	Draw.IndexCount = IndicesEmitted;
	if (IndicesEmitted > 0)
		bGpuBuffersDirty = true;
}

//============================================================================
//	Ribbon Packer - CPU-expanded trail geometry.
//============================================================================
void FParticleSystemSceneProxy::FRibbonParticlePacker::ResetFrame()
{
	PackedVertices.clear();
	PackedIndices.clear();
	bGpuBuffersDirty = true;
}

bool FParticleSystemSceneProxy::FRibbonParticlePacker::PrepareDrawBuffer(
	ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, FDrawCommandBuffer& Out) const
{
	PARTICLE_SCOPE_STAT(EParticleStatTimer::UploadRibbonBuffers);
	if (!HasPackedRibbons())
	{
		return false;
	}

	const uint32 VertCount = static_cast<uint32>(PackedVertices.size());
	const uint32 IndexCount = static_cast<uint32>(PackedIndices.size());

	if (VertexBuffer.GetMaxCount() == 0)
	{
		VertexBuffer.Create(InDevice, VertCount, sizeof(FRibbonParticleInstanceVertex));
	}
	else
	{
		VertexBuffer.EnsureCapacity(InDevice, VertCount);
	}

	if (IndexBuffer.GetMaxCount() == 0)
	{
		IndexBuffer.Create(InDevice, IndexCount);
	}
	else
	{
		IndexBuffer.EnsureCapacity(InDevice, IndexCount);
	}

	if (bGpuBuffersDirty)
	{
		if (!VertexBuffer.Update(InDeviceContext, PackedVertices.data(), VertCount))
		{
			return false;
		}
		if (!IndexBuffer.Update(InDeviceContext, PackedIndices.data(), IndexCount))
		{
			return false;
		}
		bGpuBuffersDirty = false;
	}

	Out.VB = VertexBuffer.GetBuffer();
	Out.VBStride = sizeof(FRibbonParticleInstanceVertex);
	Out.IB = IndexBuffer.GetBuffer();
	Out.BaseVertex = 0;
	return Out.VB != nullptr && Out.IB != nullptr;
}

void FParticleSystemSceneProxy::FRibbonParticlePacker::PackEmitter(const FFrameContext& Frame, FDynamicRibbonEmitterData& Emitter, FEmitterDraw& Draw)
{
	const FDynamicRibbonEmitterReplayData& Source = Emitter.RibbonSource;
	Draw.FirstIndex = static_cast<uint32>(PackedIndices.size());
	Draw.IndexCount = 0;

	if (!Source.bRenderGeometry || Source.Points.empty() || Source.Trails.empty())
	{
		return;
	}

	const int32 SheetCount = std::clamp(Source.SheetsPerTrail,
		1, static_cast<int32>(MaxSheetsPerTrail));

	uint32 ReserveVertexCount = 0;
	uint32 ReserveIndexCount = 0;
	TArray<FRibbonPointData> TessellatedPoints;
	for (const FRibbonTrailSection& Trail : Source.Trails)
	{
		if (Trail.PointCount < 2)
		{
			continue;
		}
		BuildRibbonTessellatedPoints(Source, Trail, TessellatedPoints);
		if (TessellatedPoints.size() < 2)
		{
			continue;
		}
		const uint32 PointCount = static_cast<uint32>(TessellatedPoints.size());
		ReserveVertexCount += PointCount * 2u * static_cast<uint32>(SheetCount);
		ReserveIndexCount += (PointCount - 1u) * 6u * static_cast<uint32>(SheetCount);
	}

	PackedVertices.reserve(PackedVertices.size() + ReserveVertexCount);
	PackedIndices.reserve(PackedIndices.size() + ReserveIndexCount);

	constexpr float Pi = 3.14159265358979323846f;
	uint32 IndicesEmitted = 0;

	for (const FRibbonTrailSection& Trail : Source.Trails)
	{
		if (Trail.PointCount < 2 || Trail.FirstPoint < 0)
		{
			continue;
		}

		const int32 TrailEnd = Trail.FirstPoint + Trail.PointCount;
		if (TrailEnd > static_cast<int32>(Source.Points.size()))
		{
			continue;
		}

		BuildRibbonTessellatedPoints(Source, Trail, TessellatedPoints);
		if (TessellatedPoints.size() < 2)
		{
			continue;
		}

		for (int32 SheetIdx = 0; SheetIdx < SheetCount; ++SheetIdx)
		{
			const uint32 SheetVertexBase = static_cast<uint32>(PackedVertices.size());
			FVector PreviousTangent = FVector::ForwardVector;
			FVector PreviousSideAxis = FVector::RightVector;

			for (int32 PointIdx = 0; PointIdx < static_cast<int32>(TessellatedPoints.size()); ++PointIdx)
			{
				const FRibbonPointData& Point = TessellatedPoints[PointIdx];
				const FRibbonPointData& PrevPoint = TessellatedPoints[std::max(PointIdx - 1, 0)];
				const FRibbonPointData& NextPoint = TessellatedPoints[std::min(PointIdx + 1, static_cast<int32>(TessellatedPoints.size()) - 1)];

				FVector Tangent = SafeNormalizeBeam(NextPoint.Position - PrevPoint.Position, PreviousTangent);
				PreviousTangent = Tangent;

				FVector SideAxis = BuildRibbonSideAxis(Frame, Source.RenderAxisOption, Point.Position, Tangent, Source.SourceUpVector, PreviousSideAxis);
				if (SheetIdx > 0)
				{
					const float SheetAngle = Pi * static_cast<float>(SheetIdx) / static_cast<float>(SheetCount);
					SideAxis = SafeNormalizeBeam(RotateAroundAxis(SideAxis, Tangent, SheetAngle), SideAxis);
				}
				if (SideAxis.Dot(PreviousSideAxis) < 0.0f)
				{
					SideAxis = SideAxis * -1.0f;
				}
				PreviousSideAxis = SideAxis;

				const float HalfWidth = std::max(0.0f, Point.Width) * 0.5f;
				const float U = (Source.TilingDistance > 0.0f)
					? Point.DistanceFromStart / Source.TilingDistance
					: static_cast<float>(PointIdx) / static_cast<float>(std::max(static_cast<int32>(TessellatedPoints.size()) - 1, 1));
				const FVector4 PackedColor = Point.Color.ToVector4();

				FRibbonParticleInstanceVertex Left;
				Left.Position = Point.Position - SideAxis * HalfWidth;
				Left.UV = FVector2(U, 0.0f);
				Left.Color = PackedColor;
				PackedVertices.push_back(Left);

				FRibbonParticleInstanceVertex Right;
				Right.Position = Point.Position + SideAxis * HalfWidth;
				Right.UV = FVector2(U, 1.0f);
				Right.Color = PackedColor;
				PackedVertices.push_back(Right);
			}

			const int32 SegmentCount = static_cast<int32>(TessellatedPoints.size()) - 1;
			for (int32 SegIdx = 0; SegIdx < SegmentCount; ++SegIdx)
			{
				const uint32 P0Left = SheetVertexBase + static_cast<uint32>(SegIdx) * 2u;
				const uint32 P0Right = P0Left + 1u;
				const uint32 P1Left = P0Left + 2u;
				const uint32 P1Right = P0Left + 3u;

				PackedIndices.push_back(P0Left);
				PackedIndices.push_back(P1Left);
				PackedIndices.push_back(P0Right);
				PackedIndices.push_back(P1Left);
				PackedIndices.push_back(P1Right);
				PackedIndices.push_back(P0Right);
			}

			IndicesEmitted += static_cast<uint32>(SegmentCount) * 6u;
		}
	}

	Draw.IndexCount = IndicesEmitted;
	if (IndicesEmitted > 0)
	{
		bGpuBuffersDirty = true;
	}
}

//============================================================================
//	CB
//============================================================================
void FParticleSystemSceneProxy::UpdateCB(FEmitterDraw& EmitterDraw, const FDynamicEmitterReplayDataBase& Source)
{
	const FDynamicSpriteEmitterReplayData* SpriteSource = dynamic_cast<const FDynamicSpriteEmitterReplayData*>(&Source);
	uint32 SubUVCols = SpriteSource ? static_cast<uint32>(SpriteSource->SubImages_Horizontal) : 1;
	uint32 SubUVRows = SpriteSource ? static_cast<uint32>(SpriteSource->SubImages_Vertical) : 1;
	const uint32 ScreenAlignment = SpriteSource ? static_cast<uint32>(SpriteSource->ScreenAlignment) : 0;
	const FVector EmitterOrigin = SpriteSource ? SpriteSource->EmitterOrigin : FVector::ZeroVector;
	const uint32 AlphaSource = SpriteSource ? SpriteSource->AlphaSource : 0;
	const float AlphaThreshold = SpriteSource ? SpriteSource->AlphaThreshold : 0.0f;
	const float AlphaPower = SpriteSource ? SpriteSource->AlphaPower : 1.0f;
	const float ColorIntensity = SpriteSource ? SpriteSource->ColorIntensity : 1.0f;

	if (EmitterDraw.Material)
	{
		const FMaterialParticleSettings& ParticleSettings = EmitterDraw.Material->GetParticleSettings();
		if (ParticleSettings.bUseSubUV)
		{
			SubUVCols = std::max(1u, ParticleSettings.SubUVColumns);
			SubUVRows = std::max(1u, ParticleSettings.SubUVRows);
		}
	}

	if (EmitterDraw.ParticleParams.SubUVCols == SubUVCols &&
		EmitterDraw.ParticleParams.SubUVRows == SubUVRows &&
		EmitterDraw.ParticleParams.ScreenAlignment == ScreenAlignment &&
		EmitterDraw.ParticleParams.EmitterOrigin.X == EmitterOrigin.X &&
		EmitterDraw.ParticleParams.EmitterOrigin.Y == EmitterOrigin.Y &&
		EmitterDraw.ParticleParams.EmitterOrigin.Z == EmitterOrigin.Z &&
		EmitterDraw.ParticleParams.AlphaSource == AlphaSource &&
		EmitterDraw.ParticleParams.AlphaThreshold == AlphaThreshold &&
		EmitterDraw.ParticleParams.AlphaPower == AlphaPower &&
		EmitterDraw.ParticleParams.ColorIntensity == ColorIntensity)
	{
		return;
	}

	EmitterDraw.ParticleParams.SubUVCols = SubUVCols;
	EmitterDraw.ParticleParams.SubUVRows = SubUVRows;
	EmitterDraw.ParticleParams.ScreenAlignment = ScreenAlignment;
	EmitterDraw.ParticleParams.EmitterOrigin = EmitterOrigin;
	EmitterDraw.ParticleParams.AlphaSource = AlphaSource;
	EmitterDraw.ParticleParams.AlphaThreshold = AlphaThreshold;
	EmitterDraw.ParticleParams.AlphaPower = AlphaPower;
	EmitterDraw.ParticleParams.ColorIntensity = ColorIntensity;
	EmitterDraw.bParticleParamCBDirty = true;
}

// Lazily grows the persistent sprite quad index buffer to cover RequiredParticleCount quads; no-op if already large enough.
void FParticleSystemSceneProxy::FSpriteParticlePacker::EnsureIndexPattern(uint32 RequiredParticleCount)
{
	if (RequiredParticleCount <= IndexPatternParticleCapacity)
	{
		return;
	}

	/* Quad = 6 Indices */
	IndexPattern.reserve(RequiredParticleCount * 6);
	for (uint32 ParticleIndex = IndexPatternParticleCapacity; ParticleIndex < RequiredParticleCount; ++ParticleIndex)
	{
		const uint32 V0 = ParticleIndex * 4;
		IndexPattern.push_back(V0 + 0);
		IndexPattern.push_back(V0 + 2);
		IndexPattern.push_back(V0 + 1);
		IndexPattern.push_back(V0 + 2);
		IndexPattern.push_back(V0 + 3);
		IndexPattern.push_back(V0 + 1);
	}

	IndexPatternParticleCapacity = RequiredParticleCount;
	bIndexBufferDirty = true;
}

void FParticleSystemSceneProxy::FEmitterDraw::SetParticleBlendRoute(EBlendState Mode)
{
	switch (Mode)
	{
	case EBlendState::Opaque:        ParticleBlendState = { ERenderPass::Opaque,     EBlendState::Opaque };   break;
	case EBlendState::Additive:      ParticleBlendState = { ERenderPass::AlphaBlend, EBlendState::Additive }; break;
	case EBlendState::AlphaBlend:
	default:                         ParticleBlendState = { ERenderPass::AlphaBlend, EBlendState::AlphaBlend };
	}
}
