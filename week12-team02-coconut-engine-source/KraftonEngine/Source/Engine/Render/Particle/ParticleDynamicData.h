#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Particle/ParticleHelper.h"
#include "Particle/ParticleModule.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam2.h"
#include "Particle/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Render/Types/VertexTypes.h"

struct FParticleDataContainer
{
	int32 MemBlockSize = 0;
	int32 ParticleDataNumBytes = 0;
	int32 ParticleIndicesNumShorts = 0;
	uint8* ParticleData = nullptr;		// this is also the memory block we allocated
	uint16* ParticleIndices = nullptr;	// not allocated, this is at the end of the memory block

	FParticleDataContainer() = default;
	~FParticleDataContainer();

	FParticleDataContainer(const FParticleDataContainer&) = delete;
	FParticleDataContainer& operator=(const FParticleDataContainer&) = delete;

	FParticleDataContainer(FParticleDataContainer&& Other) noexcept;
	FParticleDataContainer& operator=(FParticleDataContainer&& Other) noexcept;

	void Alloc(int32 InParticleDataNumBytes, int32 InParticleIndicesNumShorts);
	void Free();
};

struct FDynamicEmitterReplayDataBase
{
	/** The type of emitter. */
	EDynamicEmitterType eEmitterType = DET_Unknown;

	/** The number of particles currently active in this emitter. */
	int32 ActiveParticleCount = 0;

	int32 ParticleStride = 0;
	FParticleDataContainer DataContainer;

	FVector Scale = FVector::OneVector;

	EParticleSortMode SortMode = EParticleSortMode::PSORTMODE_None;

	// Cross-Emitter sorting priority
	uint16 EmitterSortPriority = 0;

	virtual ~FDynamicEmitterReplayDataBase() = default;
};

struct FDynamicRenderableEmitterReplayDataBase : public FDynamicEmitterReplayDataBase
{
	UMaterialInterface* MaterialInterface = nullptr;

	EBlendState BlendMode = EBlendState::AlphaBlend;
};

struct FDynamicSpriteEmitterReplayData : public FDynamicRenderableEmitterReplayDataBase
{
	int32 SubImages_Horizontal = 1;
	int32 SubImages_Vertical = 1;
	uint8 ScreenAlignment = 0;
	FVector EmitterOrigin = FVector::ZeroVector;
	uint32 AlphaSource = 0;
	float AlphaThreshold = 0.0f;
	float AlphaPower = 1.0f;
	float ColorIntensity = 1.0f;

	FDynamicSpriteEmitterReplayData()
	{
		eEmitterType = DET_Sprite;
	}
};

struct FDynamicMeshEmitterReplayData : public FDynamicRenderableEmitterReplayDataBase
{
	uint8 LODLevel = 0;
	UStaticMesh* StaticMesh = nullptr;

	FDynamicMeshEmitterReplayData()
	{
		eEmitterType = DET_Mesh;
	}
};

// Per-beam state inside a beam emitter. Cascade-style: every active beam carries
// its own physical form; the packer tessellates each into the shared per-proxy VB/IB.
struct FBeamInstanceData
{
	FVector Source = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	FVector Color = FVector::OneVector;
	float Alpha = 1.0f;
	float Width = 8.0f;
	EBeamTaperMethod TaperMethod = PEBTM_None;
	float TaperFactor = 1.0f;
	float TaperScale = 1.0f;
	float BeamProgress = 1.0f;				// 0 = at source, 1 = full target
	FVector SourceTangent = FVector::ZeroVector;
	FVector TargetTangent = FVector::ZeroVector;
	float SourceStrength = 1.0f;
	float TargetStrength = 1.0f;

	// Optional intermediate points (world space) between Source and Target.
	// Empty = tangent curve; non-empty = polyline Source -> NoisePoints -> Target.
	TArray<FVector> NoisePoints;
};

struct FDynamicBeamEmitterReplayData : public FDynamicRenderableEmitterReplayDataBase
{
	// Per-beam state. PackEmitter iterates this and appends each beam's
	// tessellation into the proxy's shared VB/IB slice.
	TArray<FBeamInstanceData> Beams;

	// Per-emitter (shared by every beam in Beams).
	int32 InterpolationPoints = 8;			// Subdivisions along the beam
	int32 Sheets = 1;						// Crossed quad sheets per beam
	int32 MaxBeamCount = 1;					// Capacity hint from type data
	int32 UpVectorStepSize = 0;				// UE-compatible up-vector step hint
	int32 TextureTile = 1;					// Tile count along the beam length
	float TextureTileDistance = 0.0f;		// Per-tile distance (overrides TextureTile when non-zero)

	bool bRenderGeometry = true;
	bool bRenderDirectLine = false;
	bool bRenderLines = false;
	bool bRenderTessellation = false;
	FName BranchParentName;
	TArray<FBeamTargetData> TargetData;

	// Cascade accounting: LogicalBeamCount * Sheets. Mirrors Beams.size() *
	// Sheets at steady state but the producer is allowed to set it before the
	// per-beam list is populated.
	int32 LogicalBeamCount = 1;

	FDynamicBeamEmitterReplayData()
	{
		eEmitterType = DET_Beam2;
		SortMode = PSORTMODE_None;
	}
};

struct FRibbonPointData
{
	FVector Position;
	FLinearColor Color;
	float Width = 1.0f;
	float DistanceFromStart = 0.0f;
	uint32 SpawnSequence = 0;
};

struct FRibbonTrailSection
{
	int32 FirstPoint = 0;
	int32 PointCount = 0;
};

struct FDynamicRibbonEmitterReplayData : public FDynamicRenderableEmitterReplayDataBase
{
	TArray<FRibbonPointData> Points;
	TArray<FRibbonTrailSection> Trails;

	int32 SheetsPerTrail = 1;
	int32 MaxTessellationBetweenParticles = 1;
	ETrailsRenderAxisOption RenderAxisOption = Trails_CameraUp;
	FVector SourceUpVector = FVector::UpVector;
	float TilingDistance = 0.0f;
	float DistanceTessellationStepSize = 0.0f;
	bool bRenderGeometry = true;
	bool bRenderSpawnPoints = false;
	bool bRenderTangents = false;
	bool bRenderTessellation = false;

	FDynamicRibbonEmitterReplayData()
	{
		eEmitterType = DET_Ribbon;
		SortMode = PSORTMODE_None;
	}
};


// Render-side wrapper
struct FDynamicEmitterDataBase
{
	int32 EmitterIndex = -1;
	virtual ~FDynamicEmitterDataBase() = default;
	virtual const FDynamicEmitterReplayDataBase& GetSource() const = 0;
	virtual int32 GetDynamicVertexStride(/*ERHIFeatureLevel::Type InFeatureLevel*/) const = 0;
	virtual void SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
							const FMatrix& LocalToWorld,
							uint16* InOutIndices, int32 Count,
							const uint8* ParticleData, int32 Stride);
};

struct FDynamicSpriteEmitterDataBase : public FDynamicEmitterDataBase
{
	void SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
		const FMatrix& LocalToWorld,
		uint16* InOutIndices, int32 Count,
		const uint8* ParticleData, int32 Stride) override;
};

struct FDynamicMeshEmitterDataBase : public FDynamicEmitterDataBase
{
	void SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
		const FMatrix& LocalToWorld,
		uint16* InOutIndices, int32 Count,
		const uint8* ParticleData, int32 Stride) override;
};

struct FDynamicBeamEmitterDataBase : public FDynamicEmitterDataBase
{
	void SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
		const FMatrix& LocalToWorld,
		uint16* InOutIndices, int32 Count,
		const uint8* ParticleData, int32 Stride) override;
};

struct FDynamicRibbonEmitterDataBase : public FDynamicEmitterDataBase
{
	void SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
		const FMatrix& LocalToWorld,
		uint16* InOutIndices, int32 Count,
		const uint8* ParticleData, int32 Stride) override;
};

struct FDynamicSpriteEmitterData : public FDynamicSpriteEmitterDataBase
{
	FDynamicSpriteEmitterReplayData Source;
	const FDynamicEmitterReplayDataBase& GetSource() const override { return Source; }
	int32 GetDynamicVertexStride() const override { return sizeof(FParticleSpriteVertex); }
};

struct FDynamicMeshEmitterData : public FDynamicMeshEmitterDataBase
{
	FDynamicMeshEmitterReplayData MeshSource;
	const FDynamicEmitterReplayDataBase& GetSource() const override { return MeshSource; }
	int32 GetDynamicVertexStride() const override { return sizeof(FMeshParticleInstanceVertex); }
};

struct FDynamicBeamEmitterData : public FDynamicBeamEmitterDataBase
{
	FDynamicBeamEmitterReplayData BeamSource;
	const FDynamicEmitterReplayDataBase& GetSource() const override { return BeamSource; }
	int32 GetDynamicVertexStride() const override { return sizeof(FBeamParticleInstanceVertex); }
};

struct FDynamicRibbonEmitterData : public FDynamicRibbonEmitterDataBase 
{
	FDynamicRibbonEmitterReplayData RibbonSource;
	const FDynamicEmitterReplayDataBase& GetSource() const override { return RibbonSource; }
	int32 GetDynamicVertexStride() const override { return sizeof(FRibbonParticleInstanceVertex); }
};
