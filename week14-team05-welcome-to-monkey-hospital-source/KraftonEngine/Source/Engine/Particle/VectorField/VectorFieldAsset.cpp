#include "Particle/VectorField/VectorFieldAsset.h"

#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

UVectorFieldAsset::~UVectorFieldAsset()
{
}

void UVectorFieldAsset::Serialize(FArchive& Ar)
{
	uint32 Version = 1;
	Ar << Version;

	Ar << SizeX;
	Ar << SizeY;
	Ar << SizeZ;
	Ar << BoundsMin;
	Ar << BoundsMax;
	Ar << Vectors;

	if (Ar.IsLoading() && !IsValidGrid())
	{
		Reset();
	}
}

void UVectorFieldAsset::SetGridData(int32 InSizeX, int32 InSizeY, int32 InSizeZ,
	const FVector& InBoundsMin, const FVector& InBoundsMax, const TArray<FVector>& InVectors)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	SizeZ = InSizeZ;
	BoundsMin = InBoundsMin;
	BoundsMax = InBoundsMax;
	Vectors = InVectors;

	if (!IsValidGrid())
	{
		Reset();
	}
}

void UVectorFieldAsset::Reset()
{
	SizeX = 0;
	SizeY = 0;
	SizeZ = 0;
	BoundsMin = FVector(-1.0f, -1.0f, -1.0f);
	BoundsMax = FVector(1.0f, 1.0f, 1.0f);
	Vectors.clear();
}

bool UVectorFieldAsset::IsValidGrid() const
{
	if (SizeX <= 0 || SizeY <= 0 || SizeZ <= 0)
	{
		return false;
	}

	const int64 ExpectedCount = static_cast<int64>(SizeX) * static_cast<int64>(SizeY) * static_cast<int64>(SizeZ);
	return ExpectedCount > 0 && ExpectedCount == static_cast<int64>(Vectors.size());
}

int32 UVectorFieldAsset::GetIndex(int32 X, int32 Y, int32 Z) const
{
	// FGA import stores X as the fastest-moving axis:
	// index = x + y * SizeX + z * SizeX * SizeY.
	return X + Y * SizeX + Z * SizeX * SizeY;
}

const FVector* UVectorFieldAsset::GetVectorAt(int32 X, int32 Y, int32 Z) const
{
	if (!IsValidGrid())
	{
		return nullptr;
	}
	if (X < 0 || X >= SizeX || Y < 0 || Y >= SizeY || Z < 0 || Z >= SizeZ)
	{
		return nullptr;
	}

	const int32 Index = GetIndex(X, Y, Z);
	return (Index >= 0 && Index < static_cast<int32>(Vectors.size())) ? &Vectors[Index] : nullptr;
}


namespace
{
	bool ComputeGridCoordinate(float Value, float MinValue, float MaxValue, int32 Size, float& OutGrid)
	{
		if (Size <= 0)
		{
			return false;
		}

		const float Extent = MaxValue - MinValue;
		if (std::abs(Extent) <= 1.0e-6f)
		{
			return false;
		}

		if (Value < MinValue || Value > MaxValue)
		{
			return false;
		}

		OutGrid = (Value - MinValue) / Extent * static_cast<float>(std::max(0, Size - 1));
		return true;
	}

	int32 ClampIndex(int32 Value, int32 Size)
	{
		return std::min(std::max(Value, 0), std::max(0, Size - 1));
	}

	FVector LerpVector(const FVector& A, const FVector& B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}

	void ComputeLerpAxis(float Grid, int32 Size, int32& OutA, int32& OutB, float& OutAlpha)
	{
		if (Size <= 1)
		{
			OutA = 0;
			OutB = 0;
			OutAlpha = 0.0f;
			return;
		}

		const int32 A = ClampIndex(static_cast<int32>(std::floor(Grid)), Size);
		const int32 B = ClampIndex(A + 1, Size);
		OutA = A;
		OutB = B;
		OutAlpha = std::min(std::max(Grid - static_cast<float>(A), 0.0f), 1.0f);
	}
}

bool UVectorFieldAsset::SampleNearest(const FVector& LocalPosition, FVector& OutVector) const
{
	OutVector = FVector::ZeroVector;
	if (!IsValidGrid())
	{
		return false;
	}

	float GX = 0.0f;
	float GY = 0.0f;
	float GZ = 0.0f;
	if (!ComputeGridCoordinate(LocalPosition.X, BoundsMin.X, BoundsMax.X, SizeX, GX) ||
		!ComputeGridCoordinate(LocalPosition.Y, BoundsMin.Y, BoundsMax.Y, SizeY, GY) ||
		!ComputeGridCoordinate(LocalPosition.Z, BoundsMin.Z, BoundsMax.Z, SizeZ, GZ))
	{
		return false;
	}

	const int32 X = ClampIndex(static_cast<int32>(std::floor(GX + 0.5f)), SizeX);
	const int32 Y = ClampIndex(static_cast<int32>(std::floor(GY + 0.5f)), SizeY);
	const int32 Z = ClampIndex(static_cast<int32>(std::floor(GZ + 0.5f)), SizeZ);
	OutVector = GetVectorAtUnchecked(X, Y, Z);
	return true;
}

bool UVectorFieldAsset::SampleTrilinear(const FVector& LocalPosition, FVector& OutVector) const
{
	OutVector = FVector::ZeroVector;
	if (!IsValidGrid())
	{
		return false;
	}

	float GX = 0.0f;
	float GY = 0.0f;
	float GZ = 0.0f;
	if (!ComputeGridCoordinate(LocalPosition.X, BoundsMin.X, BoundsMax.X, SizeX, GX) ||
		!ComputeGridCoordinate(LocalPosition.Y, BoundsMin.Y, BoundsMax.Y, SizeY, GY) ||
		!ComputeGridCoordinate(LocalPosition.Z, BoundsMin.Z, BoundsMax.Z, SizeZ, GZ))
	{
		return false;
	}

	int32 X0 = 0;
	int32 X1 = 0;
	int32 Y0 = 0;
	int32 Y1 = 0;
	int32 Z0 = 0;
	int32 Z1 = 0;
	float FX = 0.0f;
	float FY = 0.0f;
	float FZ = 0.0f;
	ComputeLerpAxis(GX, SizeX, X0, X1, FX);
	ComputeLerpAxis(GY, SizeY, Y0, Y1, FY);
	ComputeLerpAxis(GZ, SizeZ, Z0, Z1, FZ);

	const FVector& V000 = GetVectorAtUnchecked(X0, Y0, Z0);
	const FVector& V100 = GetVectorAtUnchecked(X1, Y0, Z0);
	const FVector& V010 = GetVectorAtUnchecked(X0, Y1, Z0);
	const FVector& V110 = GetVectorAtUnchecked(X1, Y1, Z0);
	const FVector& V001 = GetVectorAtUnchecked(X0, Y0, Z1);
	const FVector& V101 = GetVectorAtUnchecked(X1, Y0, Z1);
	const FVector& V011 = GetVectorAtUnchecked(X0, Y1, Z1);
	const FVector& V111 = GetVectorAtUnchecked(X1, Y1, Z1);

	const FVector V00 = LerpVector(V000, V100, FX);
	const FVector V10 = LerpVector(V010, V110, FX);
	const FVector V01 = LerpVector(V001, V101, FX);
	const FVector V11 = LerpVector(V011, V111, FX);
	const FVector V0 = LerpVector(V00, V10, FY);
	const FVector V1 = LerpVector(V01, V11, FY);
	OutVector = LerpVector(V0, V1, FZ);
	return true;
}
