#include "Particle/VectorField/VectorFieldFgaImporter.h"

#include "Particle/VectorField/VectorFieldAsset.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
	void SetError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	std::filesystem::path ResolveFilePath(const FString& Path)
	{
		std::filesystem::path FilePath(FPaths::ToWide(Path));
		if (!FilePath.is_absolute())
		{
			FilePath = std::filesystem::path(FPaths::RootDir()) / FilePath;
		}
		return FilePath.lexically_normal();
	}

	bool ReadTextFile(const std::filesystem::path& FilePath, FString& OutText)
	{
		std::ifstream File(FilePath, std::ios::binary);
		if (!File.is_open())
		{
			return false;
		}

		std::ostringstream Buffer;
		Buffer << File.rdbuf();
		OutText = Buffer.str();
		return true;
	}

	TArray<double> ExtractNumbers(FString Text)
	{
		// FGA is CSV처럼 파싱함.
		for (char& Character : Text)
		{
			if (Character == ',' || Character == ';' || Character == '\r' || Character == '\n' || Character == '\t')
			{
				Character = ' ';
			}
		}

		std::istringstream Stream(Text);
		TArray<double> Numbers;
		double Value = 0.0;
		while (Stream >> Value)
		{
			Numbers.push_back(Value);
		}
		return Numbers;
	}

	bool ToPositiveInt(double Value, int32& OutValue)
	{
		const double Rounded = std::round(Value);
		if (std::abs(Value - Rounded) > 0.001 || Rounded <= 0.0)
		{
			return false;
		}

		OutValue = static_cast<int32>(Rounded);
		return true;
	}
}

bool FVectorFieldFgaImporter::LoadFgaFile(const FString& SourcePath, FVectorFieldFgaImportResult& OutResult, FString* OutError)
{
	OutResult = FVectorFieldFgaImportResult {};

	const std::filesystem::path FilePath = ResolveFilePath(SourcePath);
	if (!std::filesystem::exists(FilePath) || !std::filesystem::is_regular_file(FilePath))
	{
		SetError(OutError, "FGA source file does not exist.");
		return false;
	}

	FString Text;
	if (!ReadTextFile(FilePath, Text))
	{
		SetError(OutError, "Failed to read FGA source file.");
		return false;
	}

	const TArray<double> Numbers = ExtractNumbers(Text);
	if (Numbers.size() < 9)
	{
		SetError(OutError, "FGA file is too short. Expected resolution, bounds, and vector data.");
		return false;
	}

	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 SizeZ = 0;
	if (!ToPositiveInt(Numbers[0], SizeX) || !ToPositiveInt(Numbers[1], SizeY) || !ToPositiveInt(Numbers[2], SizeZ))
	{
		SetError(OutError, "Invalid FGA resolution. Resolution must be positive integer values.");
		return false;
	}

	const int64 VectorCount64 = static_cast<int64>(SizeX) * static_cast<int64>(SizeY) * static_cast<int64>(SizeZ);
	if (VectorCount64 <= 0 || VectorCount64 > 16 * 1024 * 1024)
	{
		SetError(OutError, "Invalid or too large FGA resolution.");
		return false;
	}

	const size_t ExpectedNumberCount = 9ull + static_cast<size_t>(VectorCount64) * 3ull;
	if (Numbers.size() < ExpectedNumberCount)
	{
		SetError(OutError, "FGA vector count does not match its resolution.");
		return false;
	}

	OutResult.SizeX = SizeX;
	OutResult.SizeY = SizeY;
	OutResult.SizeZ = SizeZ;
	OutResult.BoundsMin = FVector(static_cast<float>(Numbers[3]), static_cast<float>(Numbers[4]), static_cast<float>(Numbers[5]));
	OutResult.BoundsMax = FVector(static_cast<float>(Numbers[6]), static_cast<float>(Numbers[7]), static_cast<float>(Numbers[8]));
	OutResult.Vectors.reserve(static_cast<size_t>(VectorCount64));

	// Stored in the incoming FGA order. Our runtime index convention is x-fastest:
	// x + y * SizeX + z * SizeX * SizeY.
	for (int64 Index = 0; Index < VectorCount64; ++Index)
	{
		const size_t Base = 9ull + static_cast<size_t>(Index) * 3ull;
		OutResult.Vectors.emplace_back(
			static_cast<float>(Numbers[Base + 0]),
			static_cast<float>(Numbers[Base + 1]),
			static_cast<float>(Numbers[Base + 2]));
	}

	return true;
}

bool FVectorFieldFgaImporter::FillAssetFromResult(UVectorFieldAsset* Asset, const FVectorFieldFgaImportResult& Result, FString* OutError)
{
	if (!Asset)
	{
		SetError(OutError, "Vector field asset is null.");
		return false;
	}

	const int64 ExpectedCount = static_cast<int64>(Result.SizeX) * static_cast<int64>(Result.SizeY) * static_cast<int64>(Result.SizeZ);
	if (ExpectedCount <= 0 || ExpectedCount != static_cast<int64>(Result.Vectors.size()))
	{
		SetError(OutError, "Invalid vector field import result.");
		return false;
	}

	Asset->SetGridData(Result.SizeX, Result.SizeY, Result.SizeZ, Result.BoundsMin, Result.BoundsMax, Result.Vectors);
	return Asset->IsValidGrid();
}
