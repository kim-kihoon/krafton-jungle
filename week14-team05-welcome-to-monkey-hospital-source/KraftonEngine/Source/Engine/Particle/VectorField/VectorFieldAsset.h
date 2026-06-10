#pragma once

#include "Object/Object.h"
#include "Math/Vector.h"

#include "Source/Engine/Particle/VectorField/VectorFieldAsset.generated.h"

class FArchive;

// Runtime vector-field data. This class never parses .fga directly; importers fill
// this asset once, then the serialized .uasset is the source used by runtime/editor code.
UCLASS()
class UVectorFieldAsset : public UObject
{
public:
	GENERATED_BODY()

	UVectorFieldAsset() = default;
	~UVectorFieldAsset() override;

	void Serialize(FArchive& Ar) override;

	void SetSourcePath(const FString& InPath) { SourcePath = InPath; }
	const FString& GetSourcePath() const { return SourcePath; }

	void SetGridData(int32 InSizeX, int32 InSizeY, int32 InSizeZ,
		const FVector& InBoundsMin, const FVector& InBoundsMax, const TArray<FVector>& InVectors);
	void Reset();

	int32 GetSizeX() const { return SizeX; }
	int32 GetSizeY() const { return SizeY; }
	int32 GetSizeZ() const { return SizeZ; }
	int32 GetVectorCount() const { return static_cast<int32>(Vectors.size()); }

	const FVector& GetBoundsMin() const { return BoundsMin; }
	const FVector& GetBoundsMax() const { return BoundsMax; }
	const TArray<FVector>& GetVectors() const { return Vectors; }

	bool IsValidGrid() const;
	int32 GetIndex(int32 X, int32 Y, int32 Z) const;
	const FVector* GetVectorAt(int32 X, int32 Y, int32 Z) const;

	// Sample at a position in the vector field's local bounds. Returns false when
	// the position is outside the field or the grid is invalid.
	bool SampleNearest(const FVector& LocalPosition, FVector& OutVector) const;
	bool SampleTrilinear(const FVector& LocalPosition, FVector& OutVector) const;

private:
	inline int32 GetVectorIndexUnchecked(int32 X, int32 Y, int32 Z) const
	{
		return X + Y * SizeX + Z * SizeX * SizeY;
	}

	inline const FVector& GetVectorAtUnchecked(int32 X, int32 Y, int32 Z) const
	{
		return Vectors[GetVectorIndexUnchecked(X, Y, Z)];
	}

	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 SizeZ = 0;

	FVector BoundsMin = FVector(-1.0f, -1.0f, -1.0f);
	FVector BoundsMax = FVector(1.0f, 1.0f, 1.0f);

	TArray<FVector> Vectors;
	FString SourcePath;
};
