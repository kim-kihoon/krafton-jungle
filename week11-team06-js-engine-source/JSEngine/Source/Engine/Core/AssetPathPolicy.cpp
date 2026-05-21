#include "Core/AssetPathPolicy.h"

#include "Core/Paths.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>

namespace
{
	bool EndsWith(const FString& Value, const FString& Suffix)
	{
		return Value.size() >= Suffix.size() &&
			Value.compare(Value.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
	}

	FString PathToNormalizedString(const std::filesystem::path& Path)
	{
		return FPaths::Normalize(FPaths::ToUtf8(Path.generic_wstring()));
	}

	std::filesystem::path MakeSiblingBinaryPath(
		const FString& SourceFbxPath,
		const FString& AssetKind,
		const FString& Token)
	{
		std::filesystem::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourceFbxPath)));
		std::filesystem::path BinaryFileName = SourceFsPath.stem();
		BinaryFileName += FPaths::ToWide("_" + AssetKind + "_" + FAssetPathPolicy::SanitizeImportedAssetToken(Token));
		BinaryFileName += L".bin";
		return SourceFsPath.parent_path() / BinaryFileName;
	}

}

bool FAssetPathPolicy::FileExists(const FString& Path)
{
	std::error_code Ec;
	return std::filesystem::exists(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(Path))), Ec) && !Ec;
}

bool FAssetPathPolicy::IsCurveAssetPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	FString FileName = FPaths::ToUtf8(FsPath.filename().wstring());
	std::transform(
		FileName.begin(),
		FileName.end(),
		FileName.begin(),
		[](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});

	return EndsWith(FileName, ".curve");
}

bool FAssetPathPolicy::IsAnimationSequenceAssetPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	std::wstring Extension = FsPath.extension().wstring();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
    // Keep .animsequence accepted while .anim becomes the authored asset format.
	return Extension == L".anim" || Extension == L".animsequence";
}

bool FAssetPathPolicy::IsAnimStateMachineAssetPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	std::wstring Extension = FsPath.extension().wstring();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
	return Extension == L".animsm";
}

bool FAssetPathPolicy::IsSequenceAssetPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	std::wstring Extension = FsPath.extension().wstring();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
	return Extension == L".sequence";
}

bool FAssetPathPolicy::IsSerializedMaterialAssetPath(const FString& Path)
{
	std::filesystem::path FsPath(FPaths::ToWide(FPaths::Normalize(Path)));
	std::wstring Extension = FsPath.extension().wstring();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
	return Extension == L".mat" || Extension == L".matinst";
}

FString FAssetPathPolicy::MakeCookedStaticMeshBinaryPath(const FString& SourcePath)
{
	std::filesystem::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
	const std::filesystem::path AssetMeshRoot = std::filesystem::path(L"Asset") / L"Mesh";
	std::filesystem::path SubPath = SourceFsPath.lexically_normal().lexically_relative(AssetMeshRoot);
	if (SubPath.empty())
	{
		SubPath = SourceFsPath.filename();
	}
	else
	{
		for (const std::filesystem::path& Part : SubPath)
		{
			if (Part == L"..")
			{
				SubPath = SourceFsPath.filename();
				break;
			}
		}
	}

	SubPath.replace_extension(L".bin");
	return FPaths::ToUtf8((std::filesystem::path(L"Asset") / L"Cooked" / L"Mesh" / SubPath).generic_wstring());
}

FString FAssetPathPolicy::MakeSiblingStaticMeshBinaryPath(const FString& SourcePath)
{
	std::filesystem::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
	SourceFsPath.replace_extension(L".bin");
	return FPaths::ToUtf8(SourceFsPath.generic_wstring());
}

FString FAssetPathPolicy::MakeStaticMeshCacheBinaryPath(const FString& SourcePath)
{
	std::filesystem::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourcePath)));
	std::filesystem::path BinaryFileName = SourceFsPath.stem();
	BinaryFileName += L".bin";
	return FPaths::ToUtf8((std::filesystem::path(L"Asset") / L"Mesh" / L"Bin" / BinaryFileName).generic_wstring());
}

FString FAssetPathPolicy::MakeWritableStaticMeshCacheBinaryPath(const FString& SourcePath)
{
	const FString NormalizedSourcePath = FPaths::Normalize(SourcePath);
	std::filesystem::path SourceFsPath(FPaths::ToWide(NormalizedSourcePath));

	std::filesystem::path BinDir = std::filesystem::path(FPaths::RootDir()) / "Asset" / "Mesh" / "Bin";

	if (!std::filesystem::exists(BinDir))
	{
		std::filesystem::create_directories(BinDir);
	}

	std::filesystem::path BinaryFileName = SourceFsPath.stem();
	BinaryFileName += ".bin";

	std::filesystem::path BinaryPath = BinDir / BinaryFileName;
	return FPaths::ToString(BinaryPath.wstring());
}

FString FAssetPathPolicy::SanitizeImportedAssetToken(const FString& Token)
{
	FString Result;
	Result.reserve(Token.size());

	bool bPreviousUnderscore = false;
	for (unsigned char Ch : Token)
	{
		const bool bIsUnsafeAscii =
			Ch < 32
			|| Ch == '<'
			|| Ch == '>'
			|| Ch == ':'
			|| Ch == '"'
			|| Ch == '/'
			|| Ch == '\\'
			|| Ch == '|'
			|| Ch == '?'
			|| Ch == '*';

		const bool bUseUnderscore = bIsUnsafeAscii || std::isspace(Ch) != 0;
		if (bUseUnderscore)
		{
			if (!bPreviousUnderscore)
			{
				Result.push_back('_');
				bPreviousUnderscore = true;
			}
			continue;
		}

		Result.push_back(static_cast<char>(Ch));
		bPreviousUnderscore = false;
	}

	while (!Result.empty() && (Result.front() == '.' || Result.front() == '_'))
	{
		Result.erase(Result.begin());
	}

	while (!Result.empty() && (Result.back() == '.' || Result.back() == '_'))
	{
		Result.pop_back();
	}

	return Result.empty() ? "unnamed" : Result;
}

FString FAssetPathPolicy::MakeSiblingSkeletonBinaryPath(const FString& SourceFbxPath, const FString& SkeletonRootName)
{
	return PathToNormalizedString(MakeSiblingBinaryPath(SourceFbxPath, "skeleton", SkeletonRootName));
}

FString FAssetPathPolicy::MakeSiblingSkeletalMeshBinaryPath(const FString& SourceFbxPath, const FString& MeshOrSkeletonToken)
{
	return PathToNormalizedString(MakeSiblingBinaryPath(SourceFbxPath, "skeletalmesh", MeshOrSkeletonToken));
}

FString FAssetPathPolicy::MakeSiblingAnimationSequenceAssetPath(const FString& SourceFbxPath, const FString& SequenceName)
{
	std::filesystem::path SourceFsPath(FPaths::ToWide(FPaths::Normalize(SourceFbxPath)));
	std::filesystem::path AssetFileName = SourceFsPath.stem();
	AssetFileName += FPaths::ToWide("_anim_" + SanitizeImportedAssetToken(SequenceName));
	AssetFileName += L".anim";
    return PathToNormalizedString(SourceFsPath.parent_path() / L"Animation" / AssetFileName);
}

FString FAssetPathPolicy::MakeAssetRelativePath(const FString& FromAssetPath, const FString& ToAssetPath)
{
	std::filesystem::path FromPath(FPaths::ToWide(FPaths::Normalize(FromAssetPath)));
	std::filesystem::path ToPath(FPaths::ToWide(FPaths::Normalize(ToAssetPath)));
	std::filesystem::path FromDir = FromPath.has_filename() ? FromPath.parent_path() : FromPath;

	FromDir = FromDir.lexically_normal();
	ToPath = ToPath.lexically_normal();

	if (FromDir.empty())
	{
		return PathToNormalizedString(ToPath);
	}

	std::filesystem::path RelativePath = ToPath.lexically_relative(FromDir);
	if (RelativePath.empty())
	{
		return PathToNormalizedString(ToPath);
	}

	return PathToNormalizedString(RelativePath);
}

FString FAssetPathPolicy::ResolveAssetRelativePath(const FString& FromAssetPath, const FString& RelativeTargetPath)
{
	std::filesystem::path TargetPath(FPaths::ToWide(FPaths::Normalize(RelativeTargetPath)));
	if (TargetPath.is_absolute())
	{
		return PathToNormalizedString(TargetPath.lexically_normal());
	}

	std::filesystem::path FromPath(FPaths::ToWide(FPaths::Normalize(FromAssetPath)));
	std::filesystem::path FromDir = FromPath.has_filename() ? FromPath.parent_path() : FromPath;
	return PathToNormalizedString((FromDir / TargetPath).lexically_normal());
}

FString FAssetPathPolicy::MakeWritableSkeletalMeshCacheBinaryPath(const FString& SourcePath)
{
	// Keep skeletal mesh cache files in a separate root from static mesh cache files.
	const FString NormalizedSourcePath = FPaths::Normalize(SourcePath);
	std::filesystem::path SourceFsPath(FPaths::ToWide(NormalizedSourcePath));

	std::filesystem::path BinDir = std::filesystem::path(FPaths::RootDir()) / "Asset" / "SkeletalMesh" / "Bin";

	if (!std::filesystem::exists(BinDir))
	{
		std::filesystem::create_directories(BinDir);
	}

	std::filesystem::path BinaryFileName = SourceFsPath.stem();
	BinaryFileName += ".bin";

	std::filesystem::path BinaryPath = BinDir / BinaryFileName;
	return FPaths::ToString(BinaryPath.wstring());
}

