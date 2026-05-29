#include "ParticleDynamicData.h"
#include "Particle/ParticleHelper.h"

#include <algorithm>

namespace
{
void SortParticleIndices(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
	const FMatrix& LocalToWorld,
	uint16* InOutIndices, int32 Count,
	const uint8* ParticleData, int32 Stride)
{
	if (SortMode == PSORTMODE_None || Count <= 1 || !InOutIndices || !ParticleData || Stride < static_cast<int32>(sizeof(FBaseParticle)))
	{
		return;
	}

	struct FParticleSortKey
	{
		float SortValue = 0.0f;
		uint16 ParticleIndex = 0;
	};

	TArray<FParticleSortKey> SortKeys;
	SortKeys.reserve(Count);

	for (int32 i = 0; i < Count; ++i)
	{
		const uint16 ParticleIndex = InOutIndices[i];
		const uint8* ParticleBytes = ParticleData + static_cast<size_t>(ParticleIndex) * Stride;
		const FBaseParticle& Particle = *reinterpret_cast<const FBaseParticle*>(ParticleBytes);
		const FVector WorldLocation = LocalToWorld.TransformPositionWithW(Particle.Location);

		float SortValue = 0.0f;
		switch (SortMode)
		{
		case PSORTMODE_ViewProjDepth:
			SortValue = (WorldLocation - CameraOrigin).Dot(CameraForward);
			break;
		case PSORTMODE_DistanceToView:
			SortValue = FVector::DistSquared(WorldLocation, CameraOrigin);
			break;
		case PSORTMODE_Age_OldestFirst:
		case PSORTMODE_Age_NewestFirst:
			SortValue = Particle.RelativeTime;
			break;
		default:
			return;
		}

		SortKeys.push_back({ SortValue, ParticleIndex });
	}

	const bool bAscending = SortMode == PSORTMODE_Age_NewestFirst;
	std::stable_sort(SortKeys.begin(), SortKeys.end(),
		[bAscending](const FParticleSortKey& A, const FParticleSortKey& B)
		{
			return bAscending ? A.SortValue < B.SortValue : A.SortValue > B.SortValue;
		});

	for (int32 i = 0; i < Count; ++i)
	{
		InOutIndices[i] = SortKeys[i].ParticleIndex;
	}
}
}

FParticleDataContainer::~FParticleDataContainer()
{
	Free();
}

FParticleDataContainer::FParticleDataContainer(FParticleDataContainer&& Other) noexcept
{
	*this = std::move(Other);
}

FParticleDataContainer& FParticleDataContainer::operator=(FParticleDataContainer&& Other) noexcept
{
	if (this != &Other)
	{
		Free();

		MemBlockSize = Other.MemBlockSize;
		ParticleDataNumBytes = Other.ParticleDataNumBytes;
		ParticleIndicesNumShorts = Other.ParticleIndicesNumShorts;
		ParticleData = Other.ParticleData;
		ParticleIndices = Other.ParticleIndices;

		Other.MemBlockSize = 0;
		Other.ParticleDataNumBytes = 0;
		Other.ParticleIndicesNumShorts = 0;
		Other.ParticleData = nullptr;
		Other.ParticleIndices = nullptr;
	}
	return *this;
}

void FParticleDataContainer::Alloc(int32 InParticleDataNumBytes, int32 InParticleIndicesNumShorts)
{
	Free();

	ParticleDataNumBytes = AlignParticleDataSize(std::max(0, InParticleDataNumBytes), 16);
	ParticleIndicesNumShorts = std::max(0, InParticleIndicesNumShorts);
	MemBlockSize = ParticleDataNumBytes + ParticleIndicesNumShorts * static_cast<int32>(sizeof(uint16));

	if (MemBlockSize > 0)
	{
		ParticleData = static_cast<uint8*>(_aligned_malloc(MemBlockSize, 16));
		std::memset(ParticleData, 0, MemBlockSize);
		ParticleIndices = reinterpret_cast<uint16*>(ParticleData + ParticleDataNumBytes);
	}
}

void FParticleDataContainer::Free()
{
	if (ParticleData)
	{
		_aligned_free(ParticleData);
	}

	MemBlockSize = 0;
	ParticleDataNumBytes = 0;
	ParticleIndicesNumShorts = 0;
	ParticleData = nullptr;
	ParticleIndices = nullptr;
}

void FDynamicEmitterDataBase::SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
	const FMatrix& LocalToWorld,
	uint16* InOutIndices, int32 Count,
	const uint8* ParticleData, int32 Stride)
{
	(void)SortMode;
	(void)CameraOrigin;
	(void)CameraForward;
	(void)LocalToWorld;
	(void)InOutIndices;
	(void)Count;
	(void)ParticleData;
	(void)Stride;
}

// Sprite particle sorting logic
void FDynamicSpriteEmitterDataBase::SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
	const FMatrix& LocalToWorld,
	uint16* InOutIndices, int32 Count,
	const uint8* ParticleData, int32 Stride)
{
	SortParticleIndices(SortMode, CameraOrigin, CameraForward, LocalToWorld, InOutIndices, Count, ParticleData, Stride);
}

// Mesh Particle sorting logic
// NOTE: only reorders InOutIndices. The mesh instance-buffer builder must
// iterate via InOutIndices (instance[i] = data from particle[InOutIndices[i]])
// for the sort to actually affect draw order on the GPU.
void FDynamicMeshEmitterDataBase::SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
	const FMatrix& LocalToWorld,
	uint16* InOutIndices, int32 Count,
	const uint8* ParticleData, int32 Stride)
{
	SortParticleIndices(SortMode, CameraOrigin, CameraForward, LocalToWorld, InOutIndices, Count, ParticleData, Stride);
}

// There is no per-instance / per-segment sorting in striped particles. SortMode is silenced.
void FDynamicBeamEmitterDataBase::SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
	const FMatrix& LocalToWorld,
	uint16* InOutIndices, int32 Count,
	const uint8* ParticleData, int32 Stride)
{
	return;
	// FDynamicEmitterDataBase::SortParticles(SortMode, CameraOrigin, CameraForward, LocalToWorld, InOutIndices, Count, ParticleData, Stride);
}
void FDynamicRibbonEmitterDataBase::SortParticles(EParticleSortMode SortMode, const FVector& CameraOrigin, const FVector& CameraForward,
	const FMatrix& LocalToWorld,
	uint16* InOutIndices, int32 Count,
	const uint8* ParticleData, int32 Stride)
{
	return;
	//FDynamicEmitterDataBase::SortParticles(SortMode, CameraOrigin, CameraForward, LocalToWorld, InOutIndices, Count, ParticleData, Stride);
}