#include "ObjMtlLoader.h"
#include "Asset/FileUtils.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Core/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace
{
	FString ToLowerAscii(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char CharValue)
		{
			return static_cast<char>(std::tolower(CharValue));
		});
		return Value;
	}

	bool IsTokenSeparator(char CharValue)
	{
		switch (CharValue)
		{
		case '_':
		case '-':
		case '.':
		case '(':
		case ')':
		case '[':
		case ']':
		case ' ':
		case '\t':
			return true;
		default:
			return false;
		}
	}

	void FlushFilenameToken(FString& CurrentToken, TArray<FString>& OutTokens)
	{
		if (CurrentToken.empty())
		{
			return;
		}

		OutTokens.push_back(ToLowerAscii(CurrentToken));
		CurrentToken.clear();
	}

	TArray<FString> TokenizeTextureStem(const FString& Stem)
	{
		TArray<FString> Tokens;
		FString CurrentToken;
		CurrentToken.reserve(Stem.size());

		for (size_t Index = 0; Index < Stem.size(); ++Index)
		{
			const char CharValue = Stem[Index];
			if (IsTokenSeparator(CharValue))
			{
				FlushFilenameToken(CurrentToken, Tokens);
				continue;
			}

			if (!CurrentToken.empty() &&
				std::isupper(static_cast<unsigned char>(CharValue)) &&
				(std::islower(static_cast<unsigned char>(Stem[Index - 1])) ||
				 std::isdigit(static_cast<unsigned char>(Stem[Index - 1]))))
			{
				FlushFilenameToken(CurrentToken, Tokens);
			}

			CurrentToken.push_back(CharValue);
		}

		FlushFilenameToken(CurrentToken, Tokens);
		return Tokens;
	}

	bool EndsWithInsensitive(const FString& Value, const char* Suffix)
	{
		const FString LowerValue = ToLowerAscii(Value);
		const FString LowerSuffix = ToLowerAscii(Suffix);
		if (LowerValue.size() < LowerSuffix.size())
		{
			return false;
		}

		return LowerValue.compare(LowerValue.size() - LowerSuffix.size(), LowerSuffix.size(), LowerSuffix) == 0;
	}

	bool IsStrongNormalStem(const FString& TexturePath)
	{
		const std::filesystem::path Path(FPaths::ToWide(TexturePath));
		const FString Stem = FPaths::ToUtf8(Path.stem().generic_wstring());
		if (Stem.empty())
		{
			return false;
		}

		for (const FString& Token : TokenizeTextureStem(Stem))
		{
			if (Token == "normal" || Token == "norm" || Token == "nrm")
			{
				return true;
			}
		}

		return EndsWithInsensitive(Stem, "normal") ||
			   EndsWithInsensitive(Stem, "norm") ||
			   EndsWithInsensitive(Stem, "nrm");
	}
}

bool FObjMtlLoader::Load(const FString& FilePath, TMap<FString, UMaterial*>& OutMaterialAssets, ID3D11Device* Device)
{
	std::ifstream File(std::filesystem::path(FPaths::ToWide(FilePath)));
	if (!File.is_open())
	{
		return false;
	}

	// 한글 경로 안전을 위해 wide string 기반으로 filesystem 연산 수행
	std::filesystem::path MtlDir = std::filesystem::path(FPaths::ToWide(FilePath)).parent_path();

	auto ResolveTexPath = [&](std::istringstream& InISS) -> FString
		{
			FString RelPath;

			InISS >> RelPath;
			if (RelPath.empty())
			{
				return {};
			}

			std::filesystem::path FileName = std::filesystem::path(FPaths::ToWide(RelPath)).filename();

			FString outTexPath = "";
			FFileUtils::FindFileRecursively(
				FPaths::ToUtf8(MtlDir.generic_wstring()),
				FPaths::ToUtf8(FileName.generic_wstring()),
				outTexPath);

			// 기존: std::filesystem::path TexPath = (MtlDir / outTexPath).lexically_normal();
			// 변경: UTF-8 문자열(outTexPath)을 wide로 명시 변환 후 결합
			std::filesystem::path TexPath = (MtlDir / std::filesystem::path(FPaths::ToWide(outTexPath))).lexically_normal();

			return FPaths::ToUtf8(TexPath.generic_wstring());
		};

	UMaterial* Current = nullptr;
	FString    Line;

	auto ParseFVector = [](std::istringstream& InISS) -> FVector
		{
			FVector Vector;
			InISS >> Vector.X >> Vector.Y >> Vector.Z;
			return Vector;
		};

	while (std::getline(File, Line))
	{
		Line = StringUtils::Trim(Line);
		if (Line.empty() || Line.front() == '#')
			continue;

		std::istringstream ISS(Line);
		FString Token;
		ISS >> Token;
		const FString LowerToken = ToLowerAscii(Token);

		if (LowerToken == "newmtl")
		{
			FString MatName;
			ISS >> MatName;
			OutMaterialAssets[MatName] = UObjectManager::Get().CreateObject<UMaterial>();
			Current = OutMaterialAssets[MatName];
			Current->Name = MatName;
		}
		// newmtl 이전 라인은 무시
		else if (!Current)
		{
			continue;
		}
		// 색상
		else if (LowerToken == "ka")
		{
			// Legacy MTL ambient is ignored in the UberLit material model.
		}
		else if (LowerToken == "kd")
		{
			Current->MaterialData.BaseColor = ParseFVector(ISS);
		}
		else if (LowerToken == "ks")
		{
			Current->MaterialData.SpecularColor = ParseFVector(ISS);
		}
		else if (LowerToken == "ke")
		{
			Current->MaterialData.EmissiveColor = ParseFVector(ISS);
		}
		// 광택 집중도
		else if (LowerToken == "ns")
		{
			ISS >> Current->MaterialData.Shininess;
		}
		// 보통 d 아니면 Tr로 투명도 처리 (Tr = 1 - d)
		else if (LowerToken == "d")
		{
			ISS >> Current->MaterialData.Opacity;
		}
		else if (LowerToken == "tr")
		{
			float Tr = 0.0f;
			ISS >> Tr;
			Current->MaterialData.Opacity = 1.0f - Tr;
		}
		/**
		 * 0 -> 조명 계산 없음
		 * 1 -> Kd
		 * 2 -> Kd + Ks
		 */
		else if (LowerToken == "illum")
		{
			ISS >> Current->MaterialData.IllumModel;
		}
		// TextureMap - 파싱 시점에 절대 경로로 정규화
		else if (LowerToken == "map_kd")
		{
			Current->MaterialData.DiffuseTexPath = ResolveTexPath(ISS);
			Current->MaterialData.bHasDiffuseTexture = true;
		}
		else if (LowerToken == "map_ka")
		{
			// Legacy MTL ambient map is ignored in the UberLit material model.
		}
		else if (LowerToken == "map_ks")
		{
			Current->MaterialData.SpecularTexPath = ResolveTexPath(ISS);
			Current->MaterialData.bHasSpecularTexture = true;
		}
		else if (LowerToken == "norm" || LowerToken == "map_norm" || LowerToken == "map_kn")
		{
			Current->MaterialData.NormalTexPath = ResolveTexPath(ISS);
			Current->MaterialData.bHasNormalTexture = true;
		}
		// 범프 맵은 그레이스케일로 높이값이 저장되어 있고 추후 노말로 변환한다고 한다.
		else if (LowerToken == "map_bump" || LowerToken == "bump")
		{
			const FString ResolvedTexPath = ResolveTexPath(ISS);
			if (IsStrongNormalStem(ResolvedTexPath))
			{
				Current->MaterialData.NormalTexPath = ResolvedTexPath;
				Current->MaterialData.bHasNormalTexture = true;
				UE_LOG("[ObjMtlLoader] Promoted bump token to NormalMap by filename heuristic: %s", ResolvedTexPath.c_str());
			}
			else
			{
				Current->MaterialData.BumpTexPath = ResolvedTexPath;
				Current->MaterialData.bHasBumpTexture = true;
			}
		}
	}

	for (auto& [Name, Mat] : OutMaterialAssets)
	{
		Mat->MaterialParams["BaseColor"] = FMaterialParamValue(Mat->MaterialData.BaseColor);
		Mat->MaterialParams["SpecularColor"] = FMaterialParamValue(Mat->MaterialData.SpecularColor);
		Mat->MaterialParams["EmissiveColor"] = FMaterialParamValue(Mat->MaterialData.EmissiveColor);
		Mat->MaterialParams["Shininess"] = FMaterialParamValue(Mat->MaterialData.Shininess);
		Mat->MaterialParams["Opacity"] = FMaterialParamValue(Mat->MaterialData.Opacity);

		UTexture* DefaultWhite = FResourceManager::Get().GetTexture("DefaultWhite");
		UTexture* DefaultNormal = FResourceManager::Get().GetTexture("DefaultNormal");

		if (Mat->MaterialData.bHasDiffuseTexture)
			Mat->MaterialParams["DiffuseMap"] = FMaterialParamValue(FResourceManager::Get().LoadTexture(Mat->MaterialData.DiffuseTexPath, Device));
		else
			Mat->MaterialParams["DiffuseMap"] = FMaterialParamValue(DefaultWhite);

		if (Mat->MaterialData.bHasSpecularTexture)
			Mat->MaterialParams["SpecularMap"] = FMaterialParamValue(FResourceManager::Get().LoadTexture(Mat->MaterialData.SpecularTexPath, Device));
		else
			Mat->MaterialParams["SpecularMap"] = FMaterialParamValue(DefaultWhite);

		if (Mat->MaterialData.bHasNormalTexture)
			Mat->MaterialParams["NormalMap"] = FMaterialParamValue(FResourceManager::Get().LoadTexture(Mat->MaterialData.NormalTexPath, Device));
		else
			Mat->MaterialParams["NormalMap"] = FMaterialParamValue(DefaultNormal);

		if (Mat->MaterialData.bHasBumpTexture)
			Mat->MaterialParams["BumpMap"] = FMaterialParamValue(FResourceManager::Get().LoadTexture(Mat->MaterialData.BumpTexPath, Device));
		else
			Mat->MaterialParams["BumpMap"] = FMaterialParamValue(DefaultWhite);

		Mat->MaterialParams["bHasDiffuseMap"] = FMaterialParamValue(Mat->MaterialData.bHasDiffuseTexture);
		Mat->MaterialParams["bHasSpecularMap"] = FMaterialParamValue(Mat->MaterialData.bHasSpecularTexture);
		Mat->MaterialParams["bHasNormalMap"] = FMaterialParamValue(Mat->MaterialData.bHasNormalTexture);
		Mat->MaterialParams["bHasBumpMap"] = FMaterialParamValue(Mat->MaterialData.bHasBumpTexture);

		Mat->MaterialParams["ScrollUV"] = FMaterialParamValue(FVector2(0.0f, 0.0f));
	}

	return true;

}
