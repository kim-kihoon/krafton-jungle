#include "ParticleVertexFactory.h"

#include "Render/Resource/Buffer.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Types/VertexTypes.h"
#include "Mesh/Static/StaticMesh.h"
#include "Object/GarbageCollection.h"
#include "Mesh/MeshManager.h"
#include "Materials/Material.h"
#include "Profiling/Stats/ParticleStats.h"

#include <d3d11.h>
#include <vector>
#include <algorithm>
#include <cmath>

static bool ShouldSortParticleIndices(EParticleReplaySortMode SortMode,
                                      const FBaseParticle& A, const FVector& AWorldPos,
                                      const FBaseParticle& B, const FVector& BWorldPos,
                                      const FVector& CameraPosition)
{
	// Replay.SortMode가 지정한 "입자 내부 정렬 정책"을 함수 하나로 모은다.
	// SceneProxy는 정렬 필요 여부만 결정하고, 실제 comparator 선택은 factory가 맡는다.
	switch (SortMode)
	{
	case EParticleReplaySortMode::Age_OldestFirst:
		return A.RelativeTime > B.RelativeTime;
	case EParticleReplaySortMode::Age_NewestFirst:
		return A.RelativeTime < B.RelativeTime;
	case EParticleReplaySortMode::ViewProjDepth:
		// Approximate projected depth with camera distance until a camera-forward/depth input is added.
	case EParticleReplaySortMode::ViewDistance:
	{
		const FVector ToA = AWorldPos - CameraPosition;
		const FVector ToB = BWorldPos - CameraPosition;
		return ToA.Dot(ToA) > ToB.Dot(ToB);
	}
	case EParticleReplaySortMode::None:
	default:
		return false;
	}
}

namespace
{
	// Current Ribbon operating policy:
	// one emitter-level single-trail build may amplify control segments into many
	// sampled points through tessellation. This per-trail budget caps only that
	// tessellation-driven sample growth; it does not cap emitter count or rewrite
	// the underlying control-point chain/topology.
	constexpr uint32 RibbonRuntimeSamplePointBudgetPerTrail = 16384;

	struct FRibbonControlPoint
	{
		FVector Position;
		FVector Tangent;
		FVector4 Color;
		FVector Size;
	};

	struct FRibbonSamplePoint
	{
		FVector Position;
		FVector4 Color;
		FVector Size;
		float TrailAlpha = 0.0f;
	};

	struct FDefensivelySanitizedRibbonReplayShaping
	{
		int32 MaxTessellation = 8;
		float TangentTension = 0.5f;
		float TilesPerTrail = 1.0f;
	};

	FVector ScaleParticleSize(const FVector& Size, const FVector& Scale)
	{
		return FVector(Size.X * Scale.X, Size.Y * Scale.Y, Size.Z * Scale.Z);
	}

	float GetParticleWidthScale(const FVector& Scale)
	{
		return std::max(Scale.X, Scale.Y);
	}

	FVector GetRibbonParticleWorldPosition(
		const uint8* RawBase,
		uint32 Stride,
		bool bLocal,
		const FMatrix& LocalToWorld,
		uint32 ParticleIndex)
	{
		const auto& Particle = *reinterpret_cast<const FBaseParticle*>(RawBase + ParticleIndex * Stride);
		FVector WorldPosition = Particle.Location;
		if (bLocal)
		{
			WorldPosition = LocalToWorld.TransformPositionWithW(WorldPosition);
		}

		return WorldPosition;
	}

	std::vector<uint32> BuildRibbonTrailOrderFromRelativeTime(
		const uint8* RawBase,
		uint32 Stride,
		uint32 ParticleCount)
	{
		// Current Ribbon single-trail rule:
		// one emitter snapshot becomes one ordered chain, so the RT path rebuilds
		// trail continuity from RelativeTime rather than from generic replay sort
		// metadata or implicit particle grouping.
		std::vector<uint32> Order(ParticleCount);
		for (uint32 i = 0; i < ParticleCount; ++i)
		{
			Order[i] = i;
		}

		std::sort(Order.begin(), Order.end(), [RawBase, Stride](uint32 A, uint32 B)
		{
			const auto& ParticleA = *reinterpret_cast<const FBaseParticle*>(RawBase + A * Stride);
			const auto& ParticleB = *reinterpret_cast<const FBaseParticle*>(RawBase + B * Stride);
			return ParticleA.RelativeTime > ParticleB.RelativeTime;
		});

		return Order;
	}

	void BuildRibbonControlPoints(
		const uint8* RawBase,
		uint32 Stride,
		bool bLocal,
		const FMatrix& LocalToWorld,
		const FVector& SizeScale,
		const std::vector<uint32>& OrderedParticleIndices,
		std::vector<FRibbonControlPoint>& OutControlPoints)
	{
		OutControlPoints.clear();
		OutControlPoints.reserve(OrderedParticleIndices.size());

		for (uint32 ParticleIndex : OrderedParticleIndices)
		{
			const FBaseParticle& Particle =
				*reinterpret_cast<const FBaseParticle*>(RawBase + ParticleIndex * Stride);

			FRibbonControlPoint ControlPoint;
			ControlPoint.Position =
				GetRibbonParticleWorldPosition(RawBase, Stride, bLocal, LocalToWorld, ParticleIndex);
			ControlPoint.Tangent = FVector::ZeroVector;
			ControlPoint.Color = Particle.Color;
			ControlPoint.Size = ScaleParticleSize(Particle.Size, SizeScale);
			OutControlPoints.push_back(ControlPoint);
		}
	}

	void ComputeRibbonTangents(std::vector<FRibbonControlPoint>& ControlPoints, float TangentTension)
	{
		for (size_t i = 0; i < ControlPoints.size(); ++i)
		{
			const FVector Prev =
				(i > 0) ? ControlPoints[i - 1].Position : ControlPoints[i].Position;
			const FVector Next =
				(i + 1 < ControlPoints.size()) ? ControlPoints[i + 1].Position : ControlPoints[i].Position;
			ControlPoints[i].Tangent = (Next - Prev) * (0.5f * TangentTension);
		}
	}

	FVector HermiteInterpolateRibbon(
		const FVector& P0,
		const FVector& T0,
		const FVector& P1,
		const FVector& T1,
		float T)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;

		return
			P0 * (2.0f * T3 - 3.0f * T2 + 1.0f) +
			T0 * (T3 - 2.0f * T2 + T) +
			P1 * (-2.0f * T3 + 3.0f * T2) +
			T1 * (T3 - T2);
	}

	void SampleRibbonCurve(
		const std::vector<FRibbonControlPoint>& ControlPoints,
		uint32 TessellationPerSegment,
		std::vector<FRibbonSamplePoint>& OutSamples)
	{
		OutSamples.clear();
		if (ControlPoints.size() < 2)
		{
			return;
		}

		const uint32 ControlSegmentCount = static_cast<uint32>(ControlPoints.size()) - 1;
		OutSamples.reserve(1 + ControlSegmentCount * TessellationPerSegment);

		for (uint32 Segment = 0; Segment < ControlSegmentCount; ++Segment)
		{
			const FRibbonControlPoint& A = ControlPoints[Segment];
			const FRibbonControlPoint& B = ControlPoints[Segment + 1];

			for (uint32 Step = 0; Step < TessellationPerSegment; ++Step)
			{
				const float LocalT = static_cast<float>(Step) / static_cast<float>(TessellationPerSegment);
				const float GlobalT = static_cast<float>(Segment) + LocalT;

				FRibbonSamplePoint Sample;
				Sample.Position =
					HermiteInterpolateRibbon(A.Position, A.Tangent, B.Position, B.Tangent, LocalT);
				Sample.Color = A.Color + (B.Color - A.Color) * LocalT;
				Sample.Size = A.Size + (B.Size - A.Size) * LocalT;
				Sample.TrailAlpha = GlobalT / static_cast<float>(ControlSegmentCount);
				OutSamples.push_back(Sample);
			}
		}

		const FRibbonControlPoint& Last = ControlPoints.back();
		FRibbonSamplePoint LastSample;
		LastSample.Position = Last.Position;
		LastSample.Color = Last.Color;
		LastSample.Size = Last.Size;
		LastSample.TrailAlpha = 1.0f;
		OutSamples.push_back(LastSample);
	}

	void BuildRibbonVertices(
		const std::vector<FRibbonSamplePoint>& Samples,
		float TilesPerTrail,
		const FVector& CameraPosition,
		std::vector<FParticleBeamTrailVertex>& OutVertices)
	{
		const uint32 NumPoints = static_cast<uint32>(Samples.size());
		OutVertices.clear();
		OutVertices.resize(NumPoints * 2);

		for (uint32 i = 0; i < NumPoints; ++i)
		{
			const FRibbonSamplePoint& Sample = Samples[i];
			const FVector Position = Sample.Position;

			FVector SegmentDirection = (i + 1 < NumPoints)
				? (Samples[i + 1].Position - Position)
				: (Position - Samples[i - 1].Position);
			const float DirectionLength = SegmentDirection.Length();
			if (DirectionLength > 1e-4f)
			{
				SegmentDirection = SegmentDirection * (1.0f / DirectionLength);
			}
			else
			{
				SegmentDirection = FVector::ForwardVector;
			}

			FVector Side = SegmentDirection.Cross(CameraPosition - Position);
			const float SideLength = Side.Length();
			const float HalfWidth = Sample.Size.X * 0.5f;
			Side = (SideLength > 1e-4f) ? (Side * (HalfWidth / SideLength)) : FVector{ 0, HalfWidth, 0 };

			const float U = Sample.TrailAlpha * TilesPerTrail;
			FParticleBeamTrailVertex& LeftVertex = OutVertices[i * 2 + 0];
			FParticleBeamTrailVertex& RightVertex = OutVertices[i * 2 + 1];
			LeftVertex.Position = Position - Side;
			LeftVertex.Color = Sample.Color;
			LeftVertex.UV = { U, 0.0f };
			RightVertex.Position = Position + Side;
			RightVertex.Color = Sample.Color;
			RightVertex.UV = { U, 1.0f };
		}
	}

	void BuildRibbonIndices(uint32 SegmentCount, std::vector<uint32>& OutIndices)
	{
		OutIndices.clear();
		OutIndices.resize(SegmentCount * 6);

		for (uint32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
		{
			const uint32 BaseVertex = SegmentIndex * 2;
			uint32* IndexDst = OutIndices.data() + SegmentIndex * 6;
			IndexDst[0] = BaseVertex + 0;
			IndexDst[1] = BaseVertex + 1;
			IndexDst[2] = BaseVertex + 2;
			IndexDst[3] = BaseVertex + 1;
			IndexDst[4] = BaseVertex + 3;
			IndexDst[5] = BaseVertex + 2;
		}
	}

	uint32 ComputeRibbonRequestedSamplePointCount(
		uint32 ControlSegmentCount,
		uint32 TessellationPerSegment)
	{
		return 1u + ControlSegmentCount * TessellationPerSegment;
	}

	uint32 ResolveRibbonEffectiveTessellationForSampleBudget(
		uint32 RequestedTessellation,
		uint32 ControlSegmentCount,
		bool& bOutRuntimeCapped)
	{
		bOutRuntimeCapped = false;
		if (RequestedTessellation <= 1 || ControlSegmentCount == 0)
		{
			return std::max(1u, RequestedTessellation);
		}

		// This guard intentionally caps tessellation-driven amplification, not the
		// base control-point chain itself. Very large active-particle chains can
		// still be expensive, but authoring tessellation will not multiply them
		// without bound on the RT path.
		const uint32 RequestedSamplePoints =
			ComputeRibbonRequestedSamplePointCount(ControlSegmentCount, RequestedTessellation);
		if (RequestedSamplePoints <= RibbonRuntimeSamplePointBudgetPerTrail)
		{
			return RequestedTessellation;
		}

		// We reserve one trailing sample point, then distribute the remaining sample
		// budget across control segments as evenly as possible.
		const uint32 BudgetDrivenTessellation =
			std::max(1u, (RibbonRuntimeSamplePointBudgetPerTrail - 1u) / ControlSegmentCount);
		const uint32 EffectiveTessellation = std::min(RequestedTessellation, BudgetDrivenTessellation);
		bOutRuntimeCapped = EffectiveTessellation < RequestedTessellation;
		return std::max(1u, EffectiveTessellation);
	}

	FDefensivelySanitizedRibbonReplayShaping ResolveDefensivelySanitizedRibbonReplayShapingOnRT(
		const FDynamicRibbonEmitterReplayData& RibbonReplay)
	{
		FDefensivelySanitizedRibbonReplayShaping Shaping;
		// GT replay build is authoritative for normal authoring-domain sanitize.
		// RT keeps this lightweight revalidation as a defensive safety net so bad or
		// incomplete replay data cannot explode ribbon geometry assumptions here.
		Shaping.MaxTessellation = std::clamp(RibbonReplay.MaxTessellation, 1, 64);
		Shaping.TangentTension = std::clamp(RibbonReplay.TangentTension, 0.0f, 1.0f);
		Shaping.TilesPerTrail = std::max(0.0f, RibbonReplay.TilesPerTrail);
		return Shaping;
	}
}

// =============================================================================
// Sprite CPU billboard expansion
// =============================================================================
void FParticleSpriteVertexFactory::InitResources(ID3D11Device* Device)
{
	Shader = FShaderManager::Get().GetOrCreate(EShaderPath::ParticleSprite);

	// 정적 unit quad — corner 부호(±0.5) + tile 내부 base UV. 모든 입자가 공유한다.
	if (Device && !QuadVB.GetBuffer())
	{
		const FParticleSpriteQuadVertex QuadVerts[4] = {
			{ { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f } },
			{ {  0.5f,  0.5f, 0.0f }, { 1.0f, 0.0f } },
			{ {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f } },
			{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f } },
		};
		const uint32 QuadIndices[6] = { 0, 1, 2, 0, 2, 3 };
		QuadVB.Create(Device, QuadVerts, 4, sizeof(QuadVerts), sizeof(FParticleSpriteQuadVertex));
		QuadIB.Create(Device, QuadIndices, 6, sizeof(QuadIndices));
	}
}

void FParticleSpriteVertexFactory::ReleaseResources()
{
	Shader = nullptr;
	QuadVB.Release();
	QuadIB.Release();
}

bool FParticleSpriteVertexFactory::BuildDraw(ID3D11Device* Device, ID3D11DeviceContext* Context,
                                             const FDynamicEmitterReplayDataBase& Replay,
                                             const FVector& CameraRight, const FVector& CameraUp,
                                             const FVector& CameraPosition,
                                             bool bRequiresSort,
                                             EParticleReplaySortMode SortMode,
                                             FDynamicVertexBuffer& InOutVB,
                                             FDynamicIndexBuffer& /*InOutIB*/,
                                             FDrawSpec& OutDraw)
{
	OutDraw = {};
	// VertexFactory::BuildDraw is the last RT step before command submission:
	// it consumes the emitter-level replay contract and emits draw-ready geometry
	// buffers/spec for exactly one replay emitter section.
	const FParticleDataView View = Replay.GetParticleView();
	const uint32 N = View.ActiveParticleCount;
	if (N == 0 || View.ParticleStride == 0 || !View.ParticleData) return false;
	if (!QuadVB.GetBuffer() || !QuadIB.GetBuffer()) return false; // unit quad 미초기화

	const auto& SpriteReplay = static_cast<const FDynamicSpriteEmitterReplayData&>(Replay);
	const int32 SubH = SpriteReplay.SubImagesHorizontal > 0 ? SpriteReplay.SubImagesHorizontal : 1;
	const int32 SubV = SpriteReplay.SubImagesVertical > 0 ? SpriteReplay.SubImagesVertical : 1;
	const int32 FrameCount = SubH * SubV;
	const int32 Alignment = static_cast<int32>(SpriteReplay.Alignment);

	const uint8* RawBase = View.ParticleData;
	const uint32 Stride = View.ParticleStride;
	const bool bLocal = Replay.bUseLocalSpace;
	const FMatrix& L2W = Replay.LocalToWorld;
	const FVector SizeScale = L2W.GetScale();

	auto GetWorldPos = [RawBase, Stride, bLocal, &L2W](uint32 i) -> FVector
	{
		const auto& P = *reinterpret_cast<const FBaseParticle*>(RawBase + i * Stride);
		FVector W = P.Location;
		if (bLocal) W = L2W.TransformPositionWithW(W);
		return W;
	};

	// Sort: instance 적재 순서가 곧 draw 순서이므로 정렬된 index 순으로 채운다.
	std::vector<uint32> SortedIdx(N);
	for (uint32 i = 0; i < N; ++i) SortedIdx[i] = i;
	if (bRequiresSort)
	{
		std::sort(SortedIdx.begin(), SortedIdx.end(),
			[&GetWorldPos, &CameraPosition, RawBase, Stride, SortMode](uint32 a, uint32 b)
			{
				const FBaseParticle& ParticleA =
					*reinterpret_cast<const FBaseParticle*>(RawBase + a * Stride);
				const FBaseParticle& ParticleB =
					*reinterpret_cast<const FBaseParticle*>(RawBase + b * Stride);
				return ShouldSortParticleIndices(
					SortMode,
					ParticleA, GetWorldPos(a),
					ParticleB, GetWorldPos(b),
					CameraPosition);
			});
	}

	// 입자 1개 = 인스턴스 1개. 빌보드 corner expansion / atlas UV / alignment는 VS가 GPU에서 처리.
	std::vector<FParticleSpriteInstanceVertex> Instances(N);
	for (uint32 sortI = 0; sortI < N; ++sortI)
	{
		const uint32 i = SortedIdx[sortI];
		const FBaseParticle& P = *reinterpret_cast<const FBaseParticle*>(RawBase + i * Stride);

		FParticleSpriteInstanceVertex& V = Instances[sortI];
        V.Center                         = GetWorldPos(i);
        V.Velocity                       = bLocal ? L2W.TransformVector(P.Velocity) : P.Velocity;
        V.Size                           = FVector2 { P.Size.X * SizeScale.X, P.Size.Y * SizeScale.Y };
        V.Rotation                       = P.Rotation;
        V.Color                          = P.Color;
        V.DynamicParam                   = P.DynamicParameter;

		// SubUV: SubImageIndex -1 is unset. Frame 0 is a valid module-selected frame.
		int32 RawIdx = P.SubImageIndex;
		if (RawIdx < 0 && FrameCount > 1)
		{
			RawIdx = static_cast<int32>(P.RelativeTime * static_cast<float>(FrameCount));
			if (RawIdx < 0) RawIdx = 0;
		}
		RawIdx = std::clamp(RawIdx, 0, FrameCount - 1);
		V.SubImageIndex = RawIdx;
		V.Alignment = Alignment;
	}

	// 슬롯 VB가 다른 type stride로 오염돼 있으면 강제 재생성 (mesh BuildDraw와 동일 이유 — stride 안전).
	if (InOutVB.GetStride() != sizeof(FParticleSpriteInstanceVertex) || !InOutVB.GetBuffer())
	{
		InOutVB.Create(Device, N, sizeof(FParticleSpriteInstanceVertex));
	}
	else
	{
		InOutVB.EnsureCapacity(Device, N);
	}
	if (!InOutVB.Update(Context, Instances.data(), N)) return false;

	// 섹션 depth 정렬용 대표 위치 = 입자 월드 위치 평균.
	FVector SortSum{ 0, 0, 0 };
	for (uint32 k = 0; k < N; ++k) SortSum += GetWorldPos(k);
	OutDraw.SortWorldPos = SortSum * (1.0f / static_cast<float>(N));

	// slot 0 = unit quad(정적), slot 1 = per-instance(InOutVB). DrawIndexedInstanced(6, N).
	OutDraw.StaticVB       = QuadVB.GetBuffer();
	OutDraw.StaticVBStride = QuadVB.GetStride();
	OutDraw.StaticIB       = QuadIB.GetBuffer();
	OutDraw.VertexCount    = QuadVB.GetVertexCount();
	OutDraw.IndexCount     = QuadIB.GetIndexCount();
	OutDraw.InstanceCount  = N;
#if STATS
	// Sprite RT geometry is instance-driven: 1 particle -> 1 emitted instance,
	// while the shared static quad contributes the per-instance vertex/index shape.
	PARTICLE_STATS_ADD_SPRITE_RT_GEOMETRY(N, OutDraw.VertexCount * N, OutDraw.IndexCount * N);
#endif
	return true;
}

// =============================================================================
// Mesh instanced path
// =============================================================================
void FParticleMeshVertexFactory::InitResources(ID3D11Device* Device)
{
	Shader = FShaderManager::Get().GetOrCreate(EShaderPath::ParticleMesh);

	if (!CachedCubeFallback && Device)
	{
		CachedCubeFallback = FMeshManager::LoadStaticMesh("Content/Data/BasicShape/Cube.OBJ", Device);
	}
}

void FParticleMeshVertexFactory::ReleaseResources()
{
	Shader = nullptr;
	CachedCubeFallback = nullptr;
}

void FParticleMeshVertexFactory::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(CachedCubeFallback, "FParticleMeshVertexFactory::CachedCubeFallback");
}

bool FParticleMeshVertexFactory::BuildDraw(ID3D11Device* Device, ID3D11DeviceContext* Context,
                                           const FDynamicEmitterReplayDataBase& Replay,
                                           const FVector& /*CameraRight*/, const FVector& /*CameraUp*/,
                                           const FVector& CameraPosition,
                                           bool bRequiresSort,
                                           EParticleReplaySortMode SortMode,
                                           FDynamicVertexBuffer& InOutVB,
                                           FDynamicIndexBuffer& /*InOutIB*/,
                                           FDrawSpec& OutDraw)
{
	OutDraw = {};
	// Mesh RT build consumes the shared replay header plus the narrower Mesh
	// shaping contract and turns them into one instanced draw-ready section spec.
	const FParticleDataView View = Replay.GetParticleView();
	const uint32 N = View.ActiveParticleCount;
	if (N == 0 || View.ParticleStride == 0 || !View.ParticleData) return false;

	const auto& MeshReplay = static_cast<const FDynamicMeshEmitterReplayData&>(Replay);
	// Current RT Mesh consumption is intentionally narrower than the full replay struct:
	//   - MeshReplay.Mesh      : actively selects the static mesh (with cube fallback)
	//   - MeshReplay.Material  : actively affects section material and SubImagesH/V lookup
	//   - MeshReplay.Alignment : not yet interpreted here for instance orientation
	//   - MeshReplay.bOverrideMaterial : not yet used to alter RT material resolution;
	//                                    the shared replay-first material authority chain
	//                                    is already resolved before this BuildDraw path
	UStaticMesh* Mesh = MeshReplay.Mesh ? MeshReplay.Mesh : CachedCubeFallback;
	if (!Mesh) return false;
	FMeshBuffer* MB = Mesh->GetLODMeshBuffer(0);
	if (!MB || !MB->IsValid()) return false;

	const uint8* RawBase = View.ParticleData;
	const uint32 Stride = View.ParticleStride;
	const FVector SizeScale = Replay.LocalToWorld.GetScale();

	int32 SubH = 1;
	int32 SubV = 1;
	if (MeshReplay.Material)
	{
		float fH = 1.0f;
		float fV = 1.0f;
		MeshReplay.Material->GetScalarParameter("SubImagesH", fH);
		MeshReplay.Material->GetScalarParameter("SubImagesV", fV);
		if (fH >= 1.0f) SubH = static_cast<int32>(fH);
		if (fV >= 1.0f) SubV = static_cast<int32>(fV);
	}
	const int32 FrameCount = SubH * SubV;

	auto GetWorldPos = [RawBase, Stride, &Replay](uint32 i) -> FVector
	{
		const auto& P = *reinterpret_cast<const FBaseParticle*>(RawBase + i * Stride);
		FVector W = P.Location;
		if (Replay.bUseLocalSpace)
		{
			W = Replay.LocalToWorld.TransformPositionWithW(W);
		}
		return W;
	};

	std::vector<uint32> SortedIdx(N);
	for (uint32 i = 0; i < N; ++i) SortedIdx[i] = i;
	if (bRequiresSort)
	{
		std::sort(SortedIdx.begin(), SortedIdx.end(),
			[&GetWorldPos, &CameraPosition, RawBase, Stride, SortMode](uint32 a, uint32 b)
			{
				const FBaseParticle& ParticleA =
					*reinterpret_cast<const FBaseParticle*>(RawBase + a * Stride);
				const FBaseParticle& ParticleB =
					*reinterpret_cast<const FBaseParticle*>(RawBase + b * Stride);
				return ShouldSortParticleIndices(
					SortMode,
					ParticleA, GetWorldPos(a),
					ParticleB, GetWorldPos(b),
					CameraPosition);
			});
	}

	std::vector<FParticleMeshInstanceVertex> Instances(N);
	for (uint32 sortI = 0; sortI < N; ++sortI)
	{
		const uint32 i = SortedIdx[sortI];
		const FBaseParticle& P = *reinterpret_cast<const FBaseParticle*>(RawBase + i * Stride);

		// Instance orientation currently comes from the particle's own scale/rotation/
		// translation only. Replay Alignment is carried through from GT for contract
		// visibility, but is not yet consumed as an extra RT orientation mode.
		const FVector RenderSize = Replay.bUseLocalSpace
			? P.Size
			: ScaleParticleSize(P.Size, SizeScale);
		FMatrix M = FMatrix::MakeScaleMatrix(RenderSize)
			* FMatrix::MakeRotationZ(P.Rotation)
			* FMatrix::MakeTranslationMatrix(P.Location);
		if (Replay.bUseLocalSpace)
		{
			M = M * Replay.LocalToWorld;
		}

		FParticleMeshInstanceVertex& V = Instances[sortI];
        V.Transform0                   = FVector4 { M.M[0][0], M.M[0][1], M.M[0][2], M.M[0][3] };
        V.Transform1                   = FVector4 { M.M[1][0], M.M[1][1], M.M[1][2], M.M[1][3] };
        V.Transform2                   = FVector4 { M.M[2][0], M.M[2][1], M.M[2][2], M.M[2][3] };
        V.Transform3                   = FVector4 { M.M[3][0], M.M[3][1], M.M[3][2], M.M[3][3] };
        V.Color                        = P.Color;
        V.DynamicParam                 = P.DynamicParameter;

		int32 SubIdx = P.SubImageIndex;
		if (SubIdx < 0 && FrameCount > 1)
		{
			SubIdx = static_cast<int32>(P.RelativeTime * static_cast<float>(FrameCount));
			if (SubIdx < 0) SubIdx = 0;
		}
		SubIdx = std::clamp(SubIdx, 0, FrameCount - 1);
		V.SubImageIndex = SubIdx;
	}

	// 이 emitter 전용 슬롯 VB(SceneProxy 소유, EmitterIdx로 인덱싱)는 type을 모르고 stride를 첫
	// Create 때 고정한다. 슬롯이 다른 type(sprite 등) stride로 잡혀 있으면 EnsureCapacity 는 그
	// stride 를 그대로 유지하므로, mesh instance stride 와 다르면 강제 재생성해야 한다. 안 그러면
	// instance transform 이 어긋난 stride 로 읽혀 이물질/offset/랜덤 크기로 그려진다.
	if (InOutVB.GetStride() != sizeof(FParticleMeshInstanceVertex) || !InOutVB.GetBuffer())
	{
		InOutVB.Create(Device, N, sizeof(FParticleMeshInstanceVertex));
	}
	else
	{
		InOutVB.EnsureCapacity(Device, N);
	}
	if (!InOutVB.Update(Context, Instances.data(), N)) return false;

	// 섹션 depth 정렬용 대표 위치 = 입자 월드 위치 평균.
	FVector SortSum{ 0, 0, 0 };
	for (uint32 k = 0; k < N; ++k) SortSum += GetWorldPos(k);
	OutDraw.SortWorldPos = SortSum * (1.0f / static_cast<float>(N));

	OutDraw.StaticVB = MB->GetVertexBuffer().GetBuffer();
	OutDraw.StaticVBStride = MB->GetVertexBuffer().GetStride();
	OutDraw.StaticIB = MB->GetIndexBuffer().GetBuffer();
	OutDraw.VertexCount = MB->GetVertexBuffer().GetVertexCount();
	OutDraw.IndexCount = MB->GetIndexBuffer().GetIndexCount();
	OutDraw.InstanceCount = N;
#if STATS
	// Mesh RT geometry is also instance-driven: the mesh VB/IB is reused, while
	// emitted instance count reflects how many particle instances hit the RT path.
	PARTICLE_STATS_ADD_MESH_RT_GEOMETRY(N, OutDraw.VertexCount * N, OutDraw.IndexCount * N);
#endif
	return true;
}

// =============================================================================
// Beam — SourcePoint→TargetPoint 카메라facing quad strip
// =============================================================================
namespace
{
uint32 ResolveBeamStripMultiplicity(const FDynamicBeamEmitterReplayData& BeamReplay)
{
	const FParticleDataView View = BeamReplay.GetParticleView();

	// Current Beam contract note:
	//   Beam replay itself is emitter-level (one source/target/tangent/noise snapshot).
	//   RT does not consume per-particle independent endpoint sets here.
	//   ActiveParticleCount is therefore not "beam endpoint pair count", but a legacy/
	//   minimal multiplicity hint that can duplicate the same logical beam strip.
	return std::max(1u, View.ActiveParticleCount);
}
}

void FParticleBeamVertexFactory::InitResources(ID3D11Device* /*Device*/)
{
	Shader = FShaderManager::Get().GetOrCreate(EShaderPath::ParticleBeamTrail);
}

void FParticleBeamVertexFactory::ReleaseResources()
{
	Shader = nullptr;
}

bool FParticleBeamVertexFactory::BuildDraw(ID3D11Device* Device, ID3D11DeviceContext* Context,
                                           const FDynamicEmitterReplayDataBase& Replay,
                                           const FVector& /*CameraRight*/, const FVector& /*CameraUp*/,
                                           const FVector& CameraPosition,
                                           bool /*bRequiresSort*/,
                                           EParticleReplaySortMode /*SortMode*/,
                                           FDynamicVertexBuffer& InOutVB,
                                           FDynamicIndexBuffer& InOutIB,
                                           FDrawSpec& OutDraw)
{
	OutDraw = {};
	// Beam RT build does not reconstruct per-particle beam contracts; it consumes
	// one emitter-level beam shape snapshot and emits the strip geometry that
	// SceneProxy will submit as one section.
	const auto& BeamReplay = static_cast<const FDynamicBeamEmitterReplayData&>(Replay);

	FVector Source = BeamReplay.SourcePoint;
	FVector Target = BeamReplay.TargetPoint;
	FVector SourceTangent = BeamReplay.SourceTangent;
	FVector TargetTangent = BeamReplay.TargetTangent;
	FVector NoiseDirection = BeamReplay.NoiseDirection;
	if (Replay.bUseLocalSpace)
	{
		Source = Replay.LocalToWorld.TransformPositionWithW(Source);
		Target = Replay.LocalToWorld.TransformPositionWithW(Target);
		SourceTangent = Replay.LocalToWorld.TransformVector(SourceTangent);
		TargetTangent = Replay.LocalToWorld.TransformVector(TargetTangent);
		NoiseDirection = Replay.LocalToWorld.TransformVector(NoiseDirection);
	}

	if (!BeamReplay.bRenderGeometry) return false;
	const uint32 BeamStripCount = ResolveBeamStripMultiplicity(BeamReplay);

	const FVector Axis = Target - Source;
	const float BeamLen = Axis.Length();
	if (BeamLen < 1e-4f) return false;
	const FVector Dir = Axis * (1.0f / BeamLen);
	const bool bUseCurve = SourceTangent.Dot(SourceTangent) > 1e-8f || TargetTangent.Dot(TargetTangent) > 1e-8f;

	const int32 InterpPts = BeamReplay.InterpolationPoints > 0 ? BeamReplay.InterpolationPoints : 0;
	const int32 NoiseSegments = BeamReplay.NoiseTessellation > 0 ? BeamReplay.NoiseTessellation : 0;
	const int32 NumSegments = std::max(1, std::max(InterpPts + 1, NoiseSegments)); // source~target 분할 수
	const int32 NumPoints = NumSegments + 1;
	const float BeamWidthScale = GetParticleWidthScale(Replay.LocalToWorld.GetScale());
	const float BaseHalfWidth = BeamReplay.Width * BeamWidthScale * 0.5f;
	const FVector4 BeamColor = { 1, 1, 1, 1 };

	FVector NoiseAxis = NoiseDirection - Dir * NoiseDirection.Dot(Dir);
	if (NoiseAxis.Length() < 1e-4f)
	{
		const FVector WorldUp = FVector{0.0f, 0.0f, 1.0f};
		NoiseAxis = WorldUp - Dir * WorldUp.Dot(Dir);
	}
	if (NoiseAxis.Length() < 1e-4f)
	{
		const FVector WorldRight = FVector{1.0f, 0.0f, 0.0f};
		NoiseAxis = WorldRight - Dir * WorldRight.Dot(Dir);
	}
	NoiseAxis = NoiseAxis.Normalized();
	const float NoiseAmt   = BeamReplay.NoiseAmount;
	const float NoiseFreq  = BeamReplay.NoiseFrequency;
	const float NoiseSpeed = BeamReplay.NoiseSpeed;
	const float BeamTime   = BeamReplay.EmitterTime;
	constexpr float Pi = 3.14159265358979323846f;

	const uint32 VertCount = static_cast<uint32>(NumPoints * 2) * BeamStripCount;
	const uint32 IndexCount = static_cast<uint32>(NumSegments * 6) * BeamStripCount;

	std::vector<FParticleBeamTrailVertex> Vertices(VertCount);
	for (uint32 BeamIndex = 0; BeamIndex < BeamStripCount; ++BeamIndex)
	{
		// Current RT behavior duplicates the same logical beam shape with a phase offset
		// when multiplicity > 1. This is not a per-particle independent beam contract;
		// it is a legacy/minimal way to let generic emitter activity influence beam count.
		const float BeamPhaseOffset = static_cast<float>(BeamIndex) * 0.73f;
		for (int32 i = 0; i < NumPoints; ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(NumSegments);
			const float T2 = T * T;
			const float T3 = T2 * T;
			FVector CurveDir = Dir;
			FVector P = Source + Axis * T;
			if (bUseCurve)
			{
				P =
					Source * (2.0f * T3 - 3.0f * T2 + 1.0f) +
					SourceTangent * (T3 - 2.0f * T2 + T) +
					Target * (-2.0f * T3 + 3.0f * T2) +
					TargetTangent * (T3 - T2);

				CurveDir =
					Source * (6.0f * T2 - 6.0f * T) +
					SourceTangent * (3.0f * T2 - 4.0f * T + 1.0f) +
					Target * (-6.0f * T2 + 6.0f * T) +
					TargetTangent * (3.0f * T2 - 2.0f * T);
				const float CurveDirLen = CurveDir.Length();
				CurveDir = CurveDirLen > 1e-4f ? CurveDir * (1.0f / CurveDirLen) : Dir;
			}
			if (NoiseAmt > 0.0f)
			{
				// 양 끝(source/target)은 고정하고 중간만 sin파로 변위 — envelope sin(T·π)가 끝에서 0.
				const float Envelope = std::sin(T * Pi);
				// 시간 항(BeamTime*NoiseSpeed)을 phase에 더해 지그재그가 beam을 따라 흐르게 한다.
				float Wave = std::sin(T * NoiseFreq * 2.0f * Pi + BeamTime * NoiseSpeed + BeamPhaseOffset);
				if (!BeamReplay.bSmoothNoise)
				{
					Wave = Wave >= 0.0f ? 1.0f : -1.0f;
				}
				P += NoiseAxis * (Wave * NoiseAmt * Envelope);
			}
			// beam 축에 수직 + 시선 방향과 직교 → 카메라facing 띠 폭.
			const float TaperMultiplier = BeamReplay.bTaperFull
				? (1.0f + (BeamReplay.TaperFactor - 1.0f) * T)
				: 1.0f;
			const float HalfWidth = BaseHalfWidth * std::max(0.0f, TaperMultiplier);
			FVector Side = (CameraPosition - P).Cross(CurveDir);
			const float SideLen = Side.Length();
			Side = (SideLen > 1e-4f) ? (Side * (HalfWidth / SideLen)) : FVector{ 0, HalfWidth, 0 };

			const float U = BeamReplay.bTileUV ? (T * BeamLen) : T;
			const uint32 BaseVertex = (BeamIndex * static_cast<uint32>(NumPoints) + static_cast<uint32>(i)) * 2;
			FParticleBeamTrailVertex& L = Vertices[BaseVertex + 0];
			FParticleBeamTrailVertex& R = Vertices[BaseVertex + 1];
			L.Position = P - Side; L.Color = BeamColor; L.UV = { U, 0.0f };
			R.Position = P + Side; R.Color = BeamColor; R.UV = { U, 1.0f };
		}
	}

	std::vector<uint32> Indices(IndexCount);
	for (uint32 BeamIndex = 0; BeamIndex < BeamStripCount; ++BeamIndex)
	{
		for (int32 s = 0; s < NumSegments; ++s)
		{
			const uint32 Base = (BeamIndex * static_cast<uint32>(NumPoints) + static_cast<uint32>(s)) * 2;
			uint32* Dst = Indices.data() + (BeamIndex * static_cast<uint32>(NumSegments) + static_cast<uint32>(s)) * 6;
			Dst[0] = Base + 0; Dst[1] = Base + 1; Dst[2] = Base + 2;
			Dst[3] = Base + 1; Dst[4] = Base + 3; Dst[5] = Base + 2;
		}
	}

	// 슬롯 VB가 다른 type stride로 오염돼 있으면 강제 재생성 (stride 안전 — mesh/sprite와 동일 이유).
	if (InOutVB.GetStride() != sizeof(FParticleBeamTrailVertex) || !InOutVB.GetBuffer())
		InOutVB.Create(Device, VertCount, sizeof(FParticleBeamTrailVertex));
	else
		InOutVB.EnsureCapacity(Device, VertCount);
	if (!InOutVB.Update(Context, Vertices.data(), VertCount)) return false;

	InOutIB.EnsureCapacity(Device, IndexCount);
	InOutIB.Update(Context, Indices.data(), IndexCount);

	// 섹션 depth 정렬용 대표 위치 = source~target 중점.
	OutDraw.SortWorldPos = (Source + Target) * 0.5f;

	// 비인스턴싱 strip: VB/IB 모두 SceneProxy가 emitter별로 소유한 동적 버퍼.
	OutDraw.StaticIB     = InOutIB.GetBuffer();
	OutDraw.VertexCount  = VertCount;
	OutDraw.IndexCount   = IndexCount;
	OutDraw.InstanceCount = 0;
#if STATS
	// Beam RT geometry is strip-driven rather than instance-driven.
	PARTICLE_STATS_ADD_BEAM_RT_GEOMETRY(BeamStripCount, VertCount, IndexCount);
#endif
	return true;
}

// =============================================================================
// Ribbon — 활성 입자를 age순으로 이은 카메라facing trail strip
// =============================================================================
void FParticleRibbonVertexFactory::InitResources(ID3D11Device* /*Device*/)
{
	Shader = FShaderManager::Get().GetOrCreate(EShaderPath::ParticleBeamTrail);
}

void FParticleRibbonVertexFactory::ReleaseResources()
{
	Shader = nullptr;
}

bool FParticleRibbonVertexFactory::BuildDraw(ID3D11Device* Device, ID3D11DeviceContext* Context,
                                             const FDynamicEmitterReplayDataBase& Replay,
                                             const FVector& /*CameraRight*/, const FVector& /*CameraUp*/,
                                             const FVector& CameraPosition,
                                             bool /*bRequiresSort*/,
                                             EParticleReplaySortMode /*SortMode*/,
                                             FDynamicVertexBuffer& InOutVB,
                                             FDynamicIndexBuffer& InOutIB,
                                             FDrawSpec& OutDraw)
{
	OutDraw = {};
	// Ribbon RT build consumes one emitter-level single-trail replay snapshot and
	// expands it into the actual sampled strip geometry that one RT section draws.
	if (Replay.EmitterType != EDynamicEmitterType::Ribbon)
	{
		return false;
	}

	const auto& RibbonReplay = static_cast<const FDynamicRibbonEmitterReplayData&>(Replay);
	const FDefensivelySanitizedRibbonReplayShaping Shaping =
		ResolveDefensivelySanitizedRibbonReplayShapingOnRT(RibbonReplay);
	const int32 MaxTessellation = Shaping.MaxTessellation;
	const float TangentTension = Shaping.TangentTension;
	const float TilesPerTrail = Shaping.TilesPerTrail;

	// Current Ribbon consumption contract:
	// - one replay snapshot == one emitter-level ribbon trail for this frame
	// - the active-particle snapshot is interpreted as a single ordered chain
	// - generic particle Transparent SortMode does not define ribbon topology
	//   ordering or split the trail into multiple chains; trail continuity does
	const FParticleDataView View = Replay.GetParticleView();
	const uint32 N = View.ActiveParticleCount;
	if (N < 2 || View.ParticleStride == 0 || !View.ParticleData) return false; // trail은 최소 2점

	const uint8* RawBase = View.ParticleData;
	const uint32 Stride = View.ParticleStride;
	const bool bLocal = Replay.bUseLocalSpace;
	const FMatrix& L2W = Replay.LocalToWorld;
	const FVector SizeScale = L2W.GetScale();

	// Ribbon은 일반 Transparent particle처럼 "그리기용 depth sort"를 우선하지 않고,
	// trail topology를 복원하기 위한 order를 우선한다. 따라서 shared replay SortMode가
	// 존재하더라도, current Ribbon RT는 그것을 chain topology나 multi-trail grouping
	// 규칙으로 해석하지 않는다. 현재 구현은 emitter의 active particle snapshot 전체를
	// 하나의 trail로 보고, RelativeTime이 큰(더 오래된) 입자부터 이어 붙인다.
	const std::vector<uint32> OrderedParticleIndices =
		BuildRibbonTrailOrderFromRelativeTime(RawBase, Stride, N);

	// Stage 1: ordered particle chain -> control points
	std::vector<FRibbonControlPoint> ControlPoints;
	BuildRibbonControlPoints(RawBase, Stride, bLocal, L2W, SizeScale, OrderedParticleIndices, ControlPoints);

	// Stage 2: control points -> curve tangents
	// TangentTension은 particle payload가 아니라 emitter-level ribbon shaping
	// rule이며, GT replay가 넘긴 authoring/type-data 계약을 RT가 소비하는 지점이다.
	ComputeRibbonTangents(ControlPoints, TangentTension);

	// Stage 3: control points/tangents -> sampled curve points
	const uint32 ControlSegmentCount = static_cast<uint32>(ControlPoints.size()) - 1u;
	bool bRuntimeCapped = false;
	// Authoring tessellation is a shaping hint, not an unbounded RT promise.
	// Effective tessellation is derived from:
	//   requested MaxTessellation
	//   -> control segment count
	//   -> per-trail sample-point budget
	// The guard preserves the same Hermite-based shaping model and single-trail
	// semantics; it only reduces tessellation-driven sample growth when needed.
	const uint32 TessellationPerSegment = ResolveRibbonEffectiveTessellationForSampleBudget(
		static_cast<uint32>(MaxTessellation),
		ControlSegmentCount,
		bRuntimeCapped);
	std::vector<FRibbonSamplePoint> Samples;
	SampleRibbonCurve(ControlPoints, TessellationPerSegment, Samples);

	const uint32 NumPoints = static_cast<uint32>(Samples.size());
	const uint32 NumSegments = NumPoints - 1;
	const uint32 VertCount = NumPoints * 2;
	const uint32 IndexCount = NumSegments * 6;

	// Stage 4: sampled curve -> renderable ribbon strip vertices/indices
	std::vector<FParticleBeamTrailVertex> Vertices(VertCount);
	BuildRibbonVertices(Samples, TilesPerTrail, CameraPosition, Vertices);

	std::vector<uint32> Indices(IndexCount);
	BuildRibbonIndices(NumSegments, Indices);

	PARTICLE_STATS_ADD_RIBBON_GEOMETRY(
		TessellationPerSegment,
		ControlSegmentCount,
		NumPoints,
		VertCount,
		IndexCount,
		bRuntimeCapped);

	// 슬롯 VB가 다른 type stride로 오염돼 있으면 강제 재생성 (stride 안전 — mesh/sprite와 동일 이유).
	if (InOutVB.GetStride() != sizeof(FParticleBeamTrailVertex) || !InOutVB.GetBuffer())
		InOutVB.Create(Device, VertCount, sizeof(FParticleBeamTrailVertex));
	else
		InOutVB.EnsureCapacity(Device, VertCount);
	if (!InOutVB.Update(Context, Vertices.data(), VertCount)) return false;

	InOutIB.EnsureCapacity(Device, IndexCount);
	InOutIB.Update(Context, Indices.data(), IndexCount);

	// 섹션 depth 정렬용 대표 위치 = 입자 월드 위치 평균.
	FVector SortSum{ 0, 0, 0 };
	for (uint32 k = 0; k < N; ++k)
	{
		SortSum += GetRibbonParticleWorldPosition(RawBase, Stride, bLocal, L2W, k);
	}
	OutDraw.SortWorldPos = SortSum * (1.0f / static_cast<float>(N));

	OutDraw.StaticIB     = InOutIB.GetBuffer();
	OutDraw.VertexCount  = VertCount;
	OutDraw.IndexCount   = IndexCount;
	OutDraw.InstanceCount = 0;
	return true;
}
