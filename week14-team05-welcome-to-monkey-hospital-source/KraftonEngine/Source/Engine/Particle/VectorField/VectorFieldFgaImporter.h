#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"

class UVectorFieldAsset;

struct FVectorFieldFgaImportResult
{
	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 SizeZ = 0;
	FVector BoundsMin = FVector(-1.0f, -1.0f, -1.0f);
	FVector BoundsMax = FVector(1.0f, 1.0f, 1.0f);
	TArray<FVector> Vectors;
};

class FVectorFieldFgaImporter
{
public:
	static bool LoadFgaFile(const FString& SourcePath, FVectorFieldFgaImportResult& OutResult, FString* OutError = nullptr);
	static bool FillAssetFromResult(UVectorFieldAsset* Asset, const FVectorFieldFgaImportResult& Result, FString* OutError = nullptr);
};
