#include "Mesh/Importer/Fbx/FbxMaterialImporter.h"
#include "Materials/MaterialManager.h"
#include "Materials/Material.h"
#include "Platform/Paths.h"
#include "Core/Logging/Log.h"
#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <system_error>

namespace
{
	namespace fs = std::filesystem;

	struct FTextureResolveContext
	{
		FString FbxSourcePath;
		FString EmbeddedTextureScratchDirectory;
		FString MaterialName;
	};

	FString NormalizeTexturePathSeparators(FString Path)
	{
		std::replace(Path.begin(), Path.end(), '\\', '/');
		return Path;
	}

	fs::path ToFilesystemPath(const FString& Path)
	{
		fs::path Result(FPaths::ToWide(NormalizeTexturePathSeparators(Path)));
		if (!Result.empty() && !Result.is_absolute())
		{
			Result = fs::path(FPaths::RootDir()) / Result;
		}
		return Result;
	}

	fs::path ToLexicallyNormalAbsolutePath(const fs::path& Path)
	{
		if (Path.empty())
		{
			return Path;
		}

		fs::path Result = Path;
		if (!Result.is_absolute())
		{
			Result = fs::path(FPaths::RootDir()) / Result;
		}
		return Result.lexically_normal();
	}

	std::wstring ToLower(std::wstring Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](wchar_t Ch)
		{
			return static_cast<wchar_t>(std::towlower(Ch));
		});
		return Value;
	}

	FString ToLower(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
		return Value;
	}

	bool IsSupportedTextureExtension(const fs::path& Path)
	{
		const std::wstring Ext = ToLower(Path.extension().wstring());
		return Ext == L".png" || Ext == L".jpg" || Ext == L".jpeg" || Ext == L".tga" || Ext == L".bmp" || Ext == L".dds";
	}

	std::wstring DetectTextureExtensionFromContent(const fs::path& Path)
	{
		std::ifstream File(Path, std::ios::binary);
		if (!File)
		{
			return std::wstring();
		}

		unsigned char Header[16] = {};
		File.read(reinterpret_cast<char*>(Header), sizeof(Header));
		const std::streamsize Count = File.gcount();
		if (Count >= 8 &&
			Header[0] == 0x89 && Header[1] == 0x50 && Header[2] == 0x4E && Header[3] == 0x47 &&
			Header[4] == 0x0D && Header[5] == 0x0A && Header[6] == 0x1A && Header[7] == 0x0A)
		{
			return L".png";
		}
		if (Count >= 3 && Header[0] == 0xFF && Header[1] == 0xD8 && Header[2] == 0xFF)
		{
			return L".jpg";
		}
		if (Count >= 4 && Header[0] == 'D' && Header[1] == 'D' && Header[2] == 'S' && Header[3] == ' ')
		{
			return L".dds";
		}
		if (Count >= 2 && Header[0] == 'B' && Header[1] == 'M')
		{
			return L".bmp";
		}
		return std::wstring();
	}

	bool IsSupportedTextureFile(const fs::path& Path)
	{
		return IsSupportedTextureExtension(Path) || !DetectTextureExtensionFromContent(Path).empty();
	}

	bool HasUnsupportedTextureExtension(const fs::path& Path)
	{
		return !Path.extension().empty() && !IsSupportedTextureExtension(Path);
	}

	FString MakeSafeFileStem(FString Value)
	{
		for (char& Ch : Value)
		{
			const unsigned char U = static_cast<unsigned char>(Ch);
			if (!std::isalnum(U) && Ch != '_' && Ch != '-' && Ch != '.')
			{
				Ch = '_';
			}
		}
		return Value.empty() ? FString("Material") : Value;
	}

	FString BuildImportedMaterialAssetPath(const FString& SourcePath, const FString& MaterialName)
	{
		const fs::path FbxPath = ToFilesystemPath(SourcePath);
		const FString SourceStem = MakeSafeFileStem(FPaths::ToUtf8(FbxPath.stem().wstring()));
		const FString MaterialStem = MakeSafeFileStem(MaterialName);
		return "Content/Material/Auto/" + SourceStem + "/" + MaterialStem + ".uasset";
	}

	void LogUnsupportedTexture(const FTextureResolveContext& Context, const fs::path& TexturePath)
	{
		UE_LOG(
			"FBX texture unsupported: Material='%s' Texture='%s' Extension='%s'",
			Context.MaterialName.c_str(),
			FPaths::ToUtf8(TexturePath.generic_wstring()).c_str(),
			FPaths::ToUtf8(TexturePath.extension().wstring()).c_str()
		);
	}

	const TArray<std::wstring>& GetTextureExtensionFallbacks()
	{
		static const TArray<std::wstring> Extensions = {
			L".png", L".jpg", L".jpeg", L".tga", L".bmp", L".dds"
		};
		return Extensions;
	}

	std::wstring NormalizeTextureMatchName(std::wstring Value)
	{
		Value = ToLower(std::move(Value));
		Value.erase(std::remove_if(Value.begin(), Value.end(), [](wchar_t Ch)
		{
			return Ch == L'.' || Ch == L'_' || Ch == L'-' || std::iswspace(Ch);
		}), Value.end());
		return Value;
	}

	bool TryFindCaseInsensitive(const fs::path& Candidate, fs::path& OutPath)
	{
		std::error_code Ec;
		if (fs::exists(Candidate, Ec) && fs::is_regular_file(Candidate, Ec))
		{
			OutPath = Candidate;
			return true;
		}

		const fs::path Parent = Candidate.parent_path();
		if (Parent.empty() || !fs::exists(Parent, Ec) || !fs::is_directory(Parent, Ec))
		{
			return false;
		}

		const std::wstring Wanted = ToLower(Candidate.filename().wstring());
		for (const fs::directory_entry& Entry : fs::directory_iterator(Parent, Ec))
		{
			if (Ec || !Entry.is_regular_file())
			{
				continue;
			}

			if (ToLower(Entry.path().filename().wstring()) == Wanted)
			{
				OutPath = Entry.path();
				return true;
			}
		}

		return false;
	}

	bool TryResolveTextureCandidate(const fs::path& Candidate, fs::path& OutPath)
	{
		if (Candidate.empty())
		{
			return false;
		}

		if (TryFindCaseInsensitive(Candidate, OutPath))
		{
			return true;
		}

		const fs::path Parent = Candidate.parent_path();
		const fs::path Stem = Candidate.stem();
		if (Parent.empty() || Stem.empty())
		{
			return false;
		}

		for (const std::wstring& Extension : GetTextureExtensionFallbacks())
		{
			fs::path Alternative = Parent / (Stem.wstring() + Extension);
			if (TryFindCaseInsensitive(Alternative, OutPath))
			{
				return true;
			}
		}

		return false;
	}

	void AddUniquePath(TArray<fs::path>& Paths, const fs::path& Path)
	{
		if (Path.empty())
		{
			return;
		}

		for (const fs::path& Existing : Paths)
		{
			if (Existing == Path)
			{
				return;
			}
		}
		Paths.push_back(Path);
	}

	fs::path GetFbxSidecarFbmDirectory(const fs::path& FbxPath)
	{
		const fs::path FbxDir = FbxPath.parent_path();
		if (FbxDir.empty() || FbxPath.stem().empty())
		{
			return fs::path();
		}
		return FbxDir / (FbxPath.stem().wstring() + L".fbm");
	}

	TArray<fs::path> BuildTextureSearchDirectories(const FTextureResolveContext& ResolveContext)
	{
		TArray<fs::path> Directories;
		const fs::path FbxPath = ToFilesystemPath(ResolveContext.FbxSourcePath);
		const fs::path FbxDir = FbxPath.parent_path();
		const fs::path ParentDir = FbxDir.parent_path();
		const fs::path FbmDir = GetFbxSidecarFbmDirectory(FbxPath);

		if (!ResolveContext.EmbeddedTextureScratchDirectory.empty())
		{
			AddUniquePath(Directories, ToFilesystemPath(ResolveContext.EmbeddedTextureScratchDirectory));
		}
		AddUniquePath(Directories, FbxDir);
		AddUniquePath(Directories, FbmDir);
		AddUniquePath(Directories, FbxDir / L"textures");
		AddUniquePath(Directories, FbxDir / L"Textures");
		AddUniquePath(Directories, ParentDir);
		AddUniquePath(Directories, ParentDir / L"textures");
		AddUniquePath(Directories, ParentDir / L"Textures");
		return Directories;
	}

	TArray<fs::path> BuildTextureCandidates(const FString& RawTexturePath, const FTextureResolveContext& ResolveContext)
	{
		TArray<fs::path> Candidates;
		const FString NormalizedRaw = NormalizeTexturePathSeparators(RawTexturePath);
		const fs::path RawPath(FPaths::ToWide(NormalizedRaw));
		if (RawPath.empty())
		{
			return Candidates;
		}

		const fs::path FileName = RawPath.filename();
		const fs::path FbxPath = ToFilesystemPath(ResolveContext.FbxSourcePath);
		const fs::path FbxDir = FbxPath.parent_path();
		const fs::path ParentDir = FbxDir.parent_path();
		const fs::path FbmDir = GetFbxSidecarFbmDirectory(FbxPath);
		const fs::path EmbeddedDir = ResolveContext.EmbeddedTextureScratchDirectory.empty()
			? fs::path()
			: ToFilesystemPath(ResolveContext.EmbeddedTextureScratchDirectory);

		AddUniquePath(Candidates, RawPath);
		if (!RawPath.is_absolute())
		{
			AddUniquePath(Candidates, FbxDir / RawPath);
		}
		AddUniquePath(Candidates, FbxDir / FileName);
		AddUniquePath(Candidates, FbmDir / FileName);
		AddUniquePath(Candidates, FbxDir / L"textures" / FileName);
		AddUniquePath(Candidates, FbxDir / L"Textures" / FileName);
		AddUniquePath(Candidates, ParentDir / RawPath);
		AddUniquePath(Candidates, ParentDir / FileName);
		AddUniquePath(Candidates, ParentDir / L"textures" / FileName);
		AddUniquePath(Candidates, ParentDir / L"Textures" / FileName);
		AddUniquePath(Candidates, EmbeddedDir / FileName);
		if (!EmbeddedDir.empty() && RawPath.has_parent_path())
		{
			AddUniquePath(Candidates, EmbeddedDir / RawPath.filename());
		}
		return Candidates;
	}

	TArray<fs::path> FindEmbeddedTextureMatches(const FString& RawTexturePath, const FTextureResolveContext& ResolveContext)
	{
		TArray<fs::path> Matches;
		if (ResolveContext.EmbeddedTextureScratchDirectory.empty())
		{
			return Matches;
		}

		const fs::path EmbeddedDir = ToFilesystemPath(ResolveContext.EmbeddedTextureScratchDirectory);
		std::error_code Ec;
		if (EmbeddedDir.empty() || !fs::exists(EmbeddedDir, Ec) || !fs::is_directory(EmbeddedDir, Ec))
		{
			return Matches;
		}

		const fs::path RawPath(FPaths::ToWide(NormalizeTexturePathSeparators(RawTexturePath)));
		const std::wstring WantedFileName = ToLower(RawPath.filename().wstring());
		const std::wstring WantedStem = ToLower(RawPath.stem().wstring());
		if (WantedFileName.empty() && WantedStem.empty())
		{
			return Matches;
		}

		for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(EmbeddedDir, Ec))
		{
			if (Ec || !Entry.is_regular_file())
			{
				continue;
			}

			const fs::path EntryPath = Entry.path();
			if (!IsSupportedTextureFile(EntryPath))
			{
				continue;
			}

			const std::wstring EntryFileName = ToLower(EntryPath.filename().wstring());
			const std::wstring EntryStem = ToLower(EntryPath.stem().wstring());
			const std::wstring EntryMatchName = NormalizeTextureMatchName(EntryPath.filename().wstring());
			const std::wstring WantedMatchName = NormalizeTextureMatchName(RawPath.filename().wstring());
			if ((!WantedFileName.empty() && EntryFileName == WantedFileName) ||
				(!WantedStem.empty() && EntryStem == WantedStem) ||
				(!WantedMatchName.empty() && EntryMatchName == WantedMatchName))
			{
				Matches.push_back(EntryPath);
			}
		}

		std::sort(Matches.begin(), Matches.end(), [](const fs::path& A, const fs::path& B)
		{
			return ToLower(A.generic_wstring()) < ToLower(B.generic_wstring());
		});
		return Matches;
	}

	FString CopyResolvedTextureToProject(const fs::path& FoundPath, const FTextureResolveContext& ResolveContext)
	{
		if (FoundPath.empty())
		{
			return FString();
		}

		std::wstring DetectedExtension;
		if (!IsSupportedTextureExtension(FoundPath))
		{
			DetectedExtension = DetectTextureExtensionFromContent(FoundPath);
			if (DetectedExtension.empty())
			{
				LogUnsupportedTexture(ResolveContext, FoundPath);
				return FString();
			}
		}

		const fs::path FbxPath = ToFilesystemPath(ResolveContext.FbxSourcePath);
		const std::wstring SubFolder = FbxPath.stem().wstring();
		const fs::path DestRelDir = fs::path(L"Content") / L"Texture" / L"Auto" / SubFolder;
		const fs::path DestAbsDir = fs::path(FPaths::RootDir()) / DestRelDir;

		std::error_code Ec;
		fs::create_directories(DestAbsDir, Ec);

		fs::path DestFileName = FoundPath.filename();
		if (!DetectedExtension.empty())
		{
			DestFileName += DetectedExtension;
		}
		const fs::path DestAbsPath = DestAbsDir / DestFileName;
		if (ToLexicallyNormalAbsolutePath(FoundPath) == ToLexicallyNormalAbsolutePath(DestAbsPath))
		{
			const fs::path DestRelPath = DestRelDir / DestFileName;
			return FPaths::ToUtf8(DestRelPath.generic_wstring());
		}

		fs::copy_file(FoundPath, DestAbsPath, fs::copy_options::overwrite_existing, Ec);
		if (Ec)
		{
			return FPaths::MakeProjectRelative(FPaths::ToUtf8(FoundPath.generic_wstring()));
		}

		const fs::path DestRelPath = DestRelDir / DestFileName;
		return FPaths::ToUtf8(DestRelPath.generic_wstring());
	}

	FString GenerateNormalMapFromDiffuse(const FString& DiffuseTexturePath, const FTextureResolveContext& ResolveContext)
	{
		if (DiffuseTexturePath.empty())
		{
			return FString();
		}

		const fs::path DiffuseAbsPath = ToFilesystemPath(DiffuseTexturePath);
		int Width = 0;
		int Height = 0;
		int Channels = 0;
		stbi_uc* Pixels = stbi_load(FPaths::ToUtf8(DiffuseAbsPath.wstring()).c_str(), &Width, &Height, &Channels, 4);
		if (!Pixels || Width <= 1 || Height <= 1)
		{
			if (Pixels)
			{
				stbi_image_free(Pixels);
			}
			return FString();
		}

		auto LumaAt = [&](int X, int Y) -> float
		{
			X = std::clamp(X, 0, Width - 1);
			Y = std::clamp(Y, 0, Height - 1);
			const int Index = (Y * Width + X) * 4;
			const float R = Pixels[Index + 0] / 255.0f;
			const float G = Pixels[Index + 1] / 255.0f;
			const float B = Pixels[Index + 2] / 255.0f;
			return R * 0.299f + G * 0.587f + B * 0.114f;
		};

		std::vector<unsigned char> NormalPixels(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 3u);
		constexpr float Strength = 3.0f;
		for (int Y = 0; Y < Height; ++Y)
		{
			for (int X = 0; X < Width; ++X)
			{
				const float Dx = (LumaAt(X + 1, Y) - LumaAt(X - 1, Y)) * Strength;
				const float Dy = (LumaAt(X, Y + 1) - LumaAt(X, Y - 1)) * Strength;
				FVector N(-Dx, -Dy, 1.0f);
				N.Normalize();

				const int OutIndex = (Y * Width + X) * 3;
				NormalPixels[OutIndex + 0] = static_cast<unsigned char>(std::clamp(N.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
				NormalPixels[OutIndex + 1] = static_cast<unsigned char>(std::clamp(N.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
				NormalPixels[OutIndex + 2] = static_cast<unsigned char>(std::clamp(N.Z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f);
			}
		}
		stbi_image_free(Pixels);

		const fs::path FbxPath = ToFilesystemPath(ResolveContext.FbxSourcePath);
		const fs::path DestRelDir = fs::path(L"Content") / L"Texture" / L"Auto" / FbxPath.stem().wstring();
		const fs::path DestAbsDir = fs::path(FPaths::RootDir()) / DestRelDir;
		std::error_code Ec;
		fs::create_directories(DestAbsDir, Ec);

		const fs::path DestFileName = FPaths::ToWide(MakeSafeFileStem(ResolveContext.MaterialName) + "_generated_normal.tga");
		const fs::path DestAbsPath = DestAbsDir / DestFileName;

		std::ofstream File(DestAbsPath, std::ios::binary);
		if (!File)
		{
			return FString();
		}

		unsigned char Header[18] = {};
		Header[2] = 2; // uncompressed true-color
		Header[12] = static_cast<unsigned char>(Width & 0xFF);
		Header[13] = static_cast<unsigned char>((Width >> 8) & 0xFF);
		Header[14] = static_cast<unsigned char>(Height & 0xFF);
		Header[15] = static_cast<unsigned char>((Height >> 8) & 0xFF);
		Header[16] = 24;
		Header[17] = 0x20; // top-left origin
		File.write(reinterpret_cast<const char*>(Header), sizeof(Header));

		for (int Y = 0; Y < Height; ++Y)
		{
			for (int X = 0; X < Width; ++X)
			{
				const int Index = (Y * Width + X) * 3;
				const unsigned char Bgr[3] = {
					NormalPixels[Index + 2],
					NormalPixels[Index + 1],
					NormalPixels[Index + 0]
				};
				File.write(reinterpret_cast<const char*>(Bgr), sizeof(Bgr));
			}
		}

		if (!File)
		{
			return FString();
		}

		const fs::path DestRelPath = DestRelDir / DestFileName;
		UE_LOG(
			"FBX generated fallback normal map: Material='%s' Diffuse='%s' Normal='%s'",
			ResolveContext.MaterialName.c_str(),
			DiffuseTexturePath.c_str(),
			FPaths::ToUtf8(DestRelPath.generic_wstring()).c_str()
		);
		return FPaths::ToUtf8(DestRelPath.generic_wstring());
	}

	FString ImportTextureToProject(const FString& RawTexturePath, const FTextureResolveContext& ResolveContext)
	{
		if (RawTexturePath.empty())
		{
			return FString();
		}

		fs::path FoundPath;
		for (const fs::path& Candidate : BuildTextureCandidates(RawTexturePath, ResolveContext))
		{
			if (TryResolveTextureCandidate(Candidate, FoundPath))
			{
				return CopyResolvedTextureToProject(FoundPath, ResolveContext);
			}
		}

		TArray<fs::path> EmbeddedMatches = FindEmbeddedTextureMatches(RawTexturePath, ResolveContext);
		if (!EmbeddedMatches.empty())
		{
			if (EmbeddedMatches.size() > 1)
			{
				UE_LOG(
					"FBX embedded texture duplicate match: Material='%s' Texture='%s' Count=%d Using='%s'",
					ResolveContext.MaterialName.c_str(),
					RawTexturePath.c_str(),
					static_cast<int32>(EmbeddedMatches.size()),
					FPaths::ToUtf8(EmbeddedMatches.front().generic_wstring()).c_str()
				);
			}
			return CopyResolvedTextureToProject(EmbeddedMatches.front(), ResolveContext);
		}

		const fs::path RawPath(FPaths::ToWide(NormalizeTexturePathSeparators(RawTexturePath)));
		if (HasUnsupportedTextureExtension(RawPath))
		{
			LogUnsupportedTexture(ResolveContext, RawPath);
			return FString();
		}

		UE_LOG(
			"FBX texture not found: Material='%s' Texture='%s' EmbeddedDir='%s'",
			ResolveContext.MaterialName.c_str(),
			RawTexturePath.c_str(),
			ResolveContext.EmbeddedTextureScratchDirectory.c_str()
		);
		return FString();
	}

	bool ContainsAnyToken(const FString& Text, std::initializer_list<const char*> Tokens)
	{
		for (const char* Token : Tokens)
		{
			if (Text.find(Token) != FString::npos)
			{
				return true;
			}
		}
		return false;
	}

	bool EndsWithToken(const FString& Text, const char* Token)
	{
		const size_t TokenLength = std::strlen(Token);
		if (Text.size() < TokenLength)
		{
			return false;
		}

		return Text.compare(Text.size() - TokenLength, TokenLength, Token) == 0;
	}

	bool IsEmissiveMaterialName(const FString& MaterialName)
	{
		return EndsWithToken(MaterialName, "_Emissive");
	}

	bool IsGlassMaterialName(const FString& MaterialName)
	{
		const FString LowerName = ToLower(MaterialName);
		return LowerName.find("glass") != FString::npos;
	}

	bool IsAlphaDecalSourcePath(const FString& SourcePath)
	{
		const FString LowerStem = ToLower(FPaths::ToUtf8(fs::path(FPaths::ToWide(SourcePath)).stem().wstring()));
		return LowerStem.find("decal") != FString::npos || LowerStem.find("blood") != FString::npos;
	}

	float ReadDoubleProperty(FbxSurfaceMaterial* Material, const char* PropertyName, float DefaultValue)
	{
		if (!Material || !PropertyName)
		{
			return DefaultValue;
		}

		FbxProperty Property = Material->FindProperty(PropertyName);
		if (!Property.IsValid())
		{
			return DefaultValue;
		}

		return static_cast<float>(Property.Get<FbxDouble>());
	}

	float ReadDoubleProperty(FbxSurfaceMaterial* Material, const char* PropertyNameA, const char* PropertyNameB, float DefaultValue)
	{
		const float ValueA = ReadDoubleProperty(Material, PropertyNameA, DefaultValue);
		if (ValueA != DefaultValue)
		{
			return ValueA;
		}

		return ReadDoubleProperty(Material, PropertyNameB, DefaultValue);
	}

	bool IsNearlyBlack(const FVector& Color)
	{
		return Color.X < 0.02f && Color.Y < 0.02f && Color.Z < 0.02f;
	}

	FVector SanitizeLitDiffuseColor(const FVector& Color)
	{
		constexpr float MinChannel = 0.2f;
		return FVector(
			std::max(Color.X, MinChannel),
			std::max(Color.Y, MinChannel),
			std::max(Color.Z, MinChannel));
	}

	FString GetLowerTextureStem(const FString& TexturePath)
	{
		const fs::path Path(FPaths::ToWide(NormalizeTexturePathSeparators(TexturePath)));
		return ToLower(FPaths::ToUtf8(Path.stem().wstring()));
	}

	bool HasNormalTextureToken(const FString& Stem)
	{
		return EndsWithToken(Stem, "_n") ||
			EndsWithToken(Stem, "-n") ||
			ContainsAnyToken(Stem, { "_normal", "-normal", " normal", "normal", "_norm", "-norm", " norm", "_bump", "-bump", " bump", "bump" });
	}

	bool HasColorTextureToken(const FString& Stem)
	{
		return EndsWithToken(Stem, "_c") ||
			EndsWithToken(Stem, "-c") ||
			ContainsAnyToken(Stem, { "_diff", "-diff", " diff", "diffuse", "albedo", "basecolor", "_color", "-color", " color", "color", "_col", "-col", " col" });
	}

	bool HasPackedDataTextureToken(const FString& Stem)
	{
		return EndsWithToken(Stem, "_ro") ||
			EndsWithToken(Stem, "-ro") ||
			ContainsAnyToken(Stem, { "rough", "_spec", "-spec", " spec", "metal", "_ao", "-ao", " ao" });
	}

	bool IsLikelyNormalTexturePath(const FString& TexturePath)
	{
		const FString Stem = GetLowerTextureStem(TexturePath);
		return HasNormalTextureToken(Stem);
	}

	bool IsLikelyColorTexturePath(const FString& TexturePath)
	{
		const FString Stem = GetLowerTextureStem(TexturePath);
		return HasColorTextureToken(Stem);
	}

	// Normal/Bump 슬롯에 컬러 텍스처가 들어오는 FBX에서 라이팅이 죽지 않도록 역할을 검증한다.
	void TryAssignNormalMapSlot(FFbxImportedMaterialInfo& MaterialInfo, const FString& TexturePath)
	{
		if (TexturePath.empty())
		{
			return;
		}

		if (IsLikelyColorTexturePath(TexturePath) && MaterialInfo.DiffuseTexturePath.empty())
		{
			MaterialInfo.DiffuseTexturePath = TexturePath;
			return;
		}

		// Direct NormalMap/Bump FBX links are authoritative. Embedded Blender images often
		// arrive as generic names like Image_2_002.png, so filename tokens are only a guard
		// against obvious color maps, not a requirement.
		MaterialInfo.NormalTexturePath = TexturePath;
	}

	bool TextureNameMatchesMaterial(const FString& TextureStem, const FString& MaterialName)
	{
		const std::wstring NormalizedTexture = NormalizeTextureMatchName(FPaths::ToWide(TextureStem));
		const std::wstring NormalizedMaterial = NormalizeTextureMatchName(FPaths::ToWide(MaterialName));
		if (NormalizedTexture.empty() || NormalizedMaterial.empty())
		{
			return false;
		}

		return NormalizedTexture.find(NormalizedMaterial) != std::wstring::npos ||
			NormalizedMaterial.find(NormalizedTexture) != std::wstring::npos;
	}

	int32 ScoreTextureForRole(const fs::path& TexturePath, const FString& MaterialName, bool bNormalMap)
	{
		const FString Stem = ToLower(FPaths::ToUtf8(TexturePath.stem().wstring()));
		const FString File = ToLower(FPaths::ToUtf8(TexturePath.filename().wstring()));
		const FString Mat  = ToLower(MaterialName);
		int32 Score = 0;

		if (!TextureNameMatchesMaterial(Stem, Mat))
		{
			return -10000;
		}

		if (Mat.find(Stem) != FString::npos || Stem.find(Mat) != FString::npos)
		{
			Score += 80;
		}
		else
		{
			Score += 60;
		}

		if (bNormalMap)
		{
			if (HasNormalTextureToken(Stem)) Score += 120;
			if (HasColorTextureToken(Stem) || HasPackedDataTextureToken(Stem)) Score -= 100;
		}
		else
		{
			if (HasColorTextureToken(Stem)) Score += 120;
			if (HasNormalTextureToken(Stem) || HasPackedDataTextureToken(Stem)) Score -= 100;
		}

		if (File.find("car") != FString::npos && Mat.find("car") != FString::npos)
		{
			Score += 10;
		}
		return Score;
	}

	FString FindBestTextureByRole(const FTextureResolveContext& ResolveContext, bool bNormalMap)
	{
		fs::path BestPath;
		int32 BestScore = 100;
		for (const fs::path& Directory : BuildTextureSearchDirectories(ResolveContext))
		{
			std::error_code Ec;
			if (Directory.empty() || !fs::exists(Directory, Ec) || !fs::is_directory(Directory, Ec))
			{
				continue;
			}

			for (const fs::directory_entry& Entry : fs::directory_iterator(Directory, Ec))
			{
				if (Ec || !Entry.is_regular_file() || !IsSupportedTextureFile(Entry.path()))
				{
					continue;
				}

				const int32 Score = ScoreTextureForRole(Entry.path(), ResolveContext.MaterialName, bNormalMap);
				if (Score > BestScore)
				{
					BestScore = Score;
					BestPath = Entry.path();
				}
			}
		}

		if (BestPath.empty())
		{
			return FString();
		}

		UE_LOG(
			"FBX texture heuristic match: Material='%s' Role='%s' Texture='%s' Score=%d",
			ResolveContext.MaterialName.c_str(),
			bNormalMap ? "Normal" : "Diffuse",
			FPaths::ToUtf8(BestPath.generic_wstring()).c_str(),
			BestScore
		);
		return CopyResolvedTextureToProject(BestPath, ResolveContext);
	}

	FString ReadFirstTextureFromProperty(const FbxProperty& Property, const FTextureResolveContext& ResolveContext)
	{
		if (!Property.IsValid())
		{
			return FString();
		}

		const int32 TextureCount = Property.GetSrcObjectCount<FbxTexture>();
		for (int32 TextureIndex = 0; TextureIndex < TextureCount; ++TextureIndex)
		{
			FbxFileTexture* Texture = Property.GetSrcObject<FbxFileTexture>(TextureIndex);
			if (Texture)
			{
				const char* FileName = Texture->GetFileName();
				const char* RelativeFileName = Texture->GetRelativeFileName();

				auto TryImportPath = [&](const char* Path) -> FString
				{
					return (Path && Path[0] != '\0') ? ImportTextureToProject(Path, ResolveContext) : FString();
				};

				// Blender Copy+Embed exports often keep a portable relative .fbm path while
				// the absolute path still points at the original export machine.
				FString Imported = TryImportPath(RelativeFileName);
				if (Imported.empty())
				{
					Imported = TryImportPath(FileName);
				}

				if (!Imported.empty())
				{
					return Imported;
				}
			}
			else if (FbxTexture* AnyTexture = Property.GetSrcObject<FbxTexture>(TextureIndex))
			{
				UE_LOG(
					"FBX texture unsupported: Material='%s' TextureObject='%s' Type='%s'",
					ResolveContext.MaterialName.c_str(),
					AnyTexture->GetName(),
					AnyTexture->GetClassId().GetName()
				);
			}
		}

		return FString();
	}
}

void FFbxMaterialImporter::CollectMaterials(FbxScene* Scene, FFbxImportContext& Context)
{
	Context.Materials.clear();
	Context.MaterialToSlotIndex.clear();

	if (!Scene)
	{
		return;
	}

	const int32 MaterialCount = Scene->GetMaterialCount();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		FbxSurfaceMaterial* Material = Scene->GetMaterial(MaterialIndex);
		if (!Material)
		{
			continue;
		}

		FFbxImportedMaterialInfo MaterialInfo;
		MaterialInfo.Name = Material->GetName();
		MaterialInfo.DiffuseColor = FVector(1.0f, 1.0f, 1.0f);
		MaterialInfo.bEmissive = IsEmissiveMaterialName(MaterialInfo.Name);
		MaterialInfo.bTwoSided = MaterialInfo.bEmissive;

		FTextureResolveContext ResolveContext;
		ResolveContext.FbxSourcePath = Context.SourcePath;
		ResolveContext.EmbeddedTextureScratchDirectory = Context.EmbeddedTextureScratchDirectory;
		ResolveContext.MaterialName = MaterialInfo.Name;

		FbxProperty DiffuseProp = Material->FindProperty(FbxSurfaceMaterial::sDiffuse);
		if (DiffuseProp.IsValid())
		{
			FbxDouble3 Color = DiffuseProp.Get<FbxDouble3>();
			MaterialInfo.DiffuseColor = FVector(static_cast<float>(Color[0]), static_cast<float>(Color[1]), static_cast<float>(Color[2]));
			const FString DiffuseTexturePath = ReadFirstTextureFromProperty(DiffuseProp, ResolveContext);
			if (!DiffuseTexturePath.empty())
			{
				if (IsLikelyNormalTexturePath(DiffuseTexturePath))
				{
					MaterialInfo.NormalTexturePath = DiffuseTexturePath;
				}
				else
				{
					MaterialInfo.DiffuseTexturePath = DiffuseTexturePath;
				}
			}
		}

		if (MaterialInfo.bEmissive)
		{
			FbxProperty EmissiveProp = Material->FindProperty(FbxSurfaceMaterial::sEmissive);
			if (EmissiveProp.IsValid())
			{
				const FbxDouble3 EmissiveColor = EmissiveProp.Get<FbxDouble3>();
				const FVector ImportedEmissiveColor(
					static_cast<float>(EmissiveColor[0]),
					static_cast<float>(EmissiveColor[1]),
					static_cast<float>(EmissiveColor[2]));
				if (!IsNearlyBlack(ImportedEmissiveColor))
				{
					MaterialInfo.DiffuseColor = ImportedEmissiveColor;
				}
			}

			if (MaterialInfo.Name == "Material.029_Emissive" &&
				std::fabs(MaterialInfo.DiffuseColor.X - MaterialInfo.DiffuseColor.Y) < 0.02f &&
				std::fabs(MaterialInfo.DiffuseColor.Y - MaterialInfo.DiffuseColor.Z) < 0.02f)
			{
				MaterialInfo.DiffuseColor = FVector(1.0f, 0.0f, 0.0f);
			}
			else if (MaterialInfo.Name == "Material.009_Emissive")
			{
				MaterialInfo.DiffuseColor = FVector(196.0f / 255.0f, 125.0f / 255.0f, 124.0f / 255.0f);
			}
		}

		bool bHadExplicitNormalTexture = false;
		FbxProperty NormalProp = Material->FindProperty(FbxSurfaceMaterial::sNormalMap);
		bHadExplicitNormalTexture = bHadExplicitNormalTexture || (NormalProp.IsValid() && NormalProp.GetSrcObjectCount<FbxTexture>() > 0);
		TryAssignNormalMapSlot(MaterialInfo, ReadFirstTextureFromProperty(NormalProp, ResolveContext));

		if (MaterialInfo.NormalTexturePath.empty())
		{
			FbxProperty BumpProp = Material->FindProperty(FbxSurfaceMaterial::sBump);
			bHadExplicitNormalTexture = bHadExplicitNormalTexture || (BumpProp.IsValid() && BumpProp.GetSrcObjectCount<FbxTexture>() > 0);
			TryAssignNormalMapSlot(MaterialInfo, ReadFirstTextureFromProperty(BumpProp, ResolveContext));
		}

		if (MaterialInfo.DiffuseTexturePath.empty())
		{
			FbxProperty BaseColorProp = Material->FindProperty("base_color_texture");
			if (BaseColorProp.IsValid())
			{
				const FString BaseColorTexturePath = ReadFirstTextureFromProperty(BaseColorProp, ResolveContext);
				if (!BaseColorTexturePath.empty())
				{
					MaterialInfo.DiffuseTexturePath = BaseColorTexturePath;
				}
			}
		}

		if (MaterialInfo.DiffuseTexturePath.empty())
		{
			MaterialInfo.DiffuseTexturePath = FindBestTextureByRole(ResolveContext, false);
		}
		if (MaterialInfo.NormalTexturePath.empty())
		{
			MaterialInfo.NormalTexturePath = FindBestTextureByRole(ResolveContext, true);
		}
		if (MaterialInfo.NormalTexturePath.empty() && bHadExplicitNormalTexture && !MaterialInfo.DiffuseTexturePath.empty())
		{
			MaterialInfo.NormalTexturePath = GenerateNormalMapFromDiffuse(MaterialInfo.DiffuseTexturePath, ResolveContext);
		}

		float RawOpacity = 1.0f;
		float RawTransparencyFactor = 0.0f;
		bool bHasOpacity = false;
		bool bHasTransparencyFactor = false;

		FbxProperty OpacityProp = Material->FindProperty("Opacity");
		if (OpacityProp.IsValid())
		{
			RawOpacity = static_cast<float>(OpacityProp.Get<FbxDouble>());
			bHasOpacity = true;
		}

		FbxProperty TransparencyFactorProp = Material->FindProperty(FbxSurfaceMaterial::sTransparencyFactor);
		if (TransparencyFactorProp.IsValid())
		{
			RawTransparencyFactor = static_cast<float>(TransparencyFactorProp.Get<FbxDouble>());
			bHasTransparencyFactor = true;
		}

		if (bHasOpacity)
		{
			MaterialInfo.Opacity = RawOpacity;
		}
		else if (bHasTransparencyFactor)
		{
			MaterialInfo.Opacity = 1.0f - RawTransparencyFactor;
		}

		MaterialInfo.Opacity = std::clamp(MaterialInfo.Opacity, 0.0f, 1.0f);
		MaterialInfo.bTransparent = MaterialInfo.Opacity < 0.999f;
		MaterialInfo.bTwoSided = MaterialInfo.bTwoSided || MaterialInfo.bTransparent;

		const float SpecularFactor = ReadDoubleProperty(Material, "SpecularFactor", 1.0f);
		const float ReflectionFactor = ReadDoubleProperty(Material, "ReflectionFactor", 0.0f);
		const float ImportedShininess = ReadDoubleProperty(Material, "Shininess", "ShininessExponent", 32.0f);
		const float ImportedMetallic = ReadDoubleProperty(Material, "Metallic", 0.0f);
		MaterialInfo.SpecularIntensity = std::clamp(std::max(SpecularFactor, ReflectionFactor), 0.0f, 4.0f);
		MaterialInfo.Shininess = std::clamp(ImportedShininess, 2.0f, 256.0f);
		MaterialInfo.Metallic = std::clamp(ImportedMetallic, 0.0f, 1.0f);

		if (MaterialInfo.bTransparent && MaterialInfo.DiffuseTexturePath.empty() &&
			IsGlassMaterialName(MaterialInfo.Name) && IsNearlyBlack(MaterialInfo.DiffuseColor))
		{
			MaterialInfo.DiffuseColor = FVector(0.65f, 0.85f, 1.0f);
		}

		UE_LOG("[MaterialImport] %s -> Emissive=%s TwoSided=%s Transparent=%s Opacity=%.3f RawOpacity=%s:%.3f RawTransparencyFactor=%s:%.3f DiffuseColor=(%.3f, %.3f, %.3f) Specular=%.3f Shininess=%.3f Metallic=%.3f Diffuse='%s' Normal='%s'",
			MaterialInfo.Name.c_str(),
			MaterialInfo.bEmissive ? "true" : "false",
			MaterialInfo.bTwoSided ? "true" : "false",
			MaterialInfo.bTransparent ? "true" : "false",
			MaterialInfo.Opacity,
			bHasOpacity ? "true" : "false",
			RawOpacity,
			bHasTransparencyFactor ? "true" : "false",
			RawTransparencyFactor,
			MaterialInfo.DiffuseColor.X,
			MaterialInfo.DiffuseColor.Y,
			MaterialInfo.DiffuseColor.Z,
			MaterialInfo.SpecularIntensity,
			MaterialInfo.Shininess,
			MaterialInfo.Metallic,
			MaterialInfo.DiffuseTexturePath.c_str(),
			MaterialInfo.NormalTexturePath.c_str());

		const int32 GlobalIndex = static_cast<int32>(Context.Materials.size());
		Context.Materials.push_back(MaterialInfo);
		Context.MaterialToSlotIndex[Material] = GlobalIndex;
	}
}

int32 FFbxMaterialImporter::GetMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex)
{
	FbxLayerElementMaterial* LayerElementMaterial = Mesh ? Mesh->GetElementMaterial() : nullptr;
	if (!LayerElementMaterial)
	{
		return -1;
	}

	FbxLayerElementArrayTemplate<int32>& MaterialIndices = LayerElementMaterial->GetIndexArray();
	switch (LayerElementMaterial->GetMappingMode())
	{
	case FbxLayerElement::eAllSame:
		return MaterialIndices[0];
	case FbxLayerElement::eByPolygon:
		return MaterialIndices[PolygonIndex];
	default:
		return 0;
	}
}

void FFbxMaterialImporter::BuildStaticMaterials(const FFbxImportContext& Context, TArray<FStaticMaterial>& OutMaterials)
{
	OutMaterials.clear();
	OutMaterials.reserve(Context.Materials.size());

	for (const FFbxImportedMaterialInfo& MaterialInfo : Context.Materials)
	{
		FStaticMaterial NewMaterial;
		NewMaterial.MaterialSlotName = MaterialInfo.Name;
		NewMaterial.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial(CreateOrUpdateMaterialAsset(MaterialInfo, Context.SourcePath));
		OutMaterials.push_back(NewMaterial);
	}
}

void FFbxMaterialImporter::BuildSkeletalMaterials(const FFbxImportContext& Context, const TArray<FSkeletalMeshSection>& Sections, TArray<FSkeletalMaterial>& OutMaterials, TArray<FSkeletalMeshSection>& InOutSections)
{
	OutMaterials.clear();
	OutMaterials.reserve(Context.Materials.size());

	for (const FFbxImportedMaterialInfo& MaterialInfo : Context.Materials)
	{
		const FString MaterialPath = CreateOrUpdateMaterialAsset(MaterialInfo, Context.SourcePath);
		UMaterial* MaterialObject = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);

		FSkeletalMaterial NewMaterial;
		NewMaterial.MaterialInterface = MaterialObject;
		NewMaterial.MaterialSlotName = MaterialInfo.Name;
		NewMaterial.MaterialPath = MaterialPath;
		OutMaterials.push_back(NewMaterial);
	}

	bool bNeedsNoneSlot = OutMaterials.empty();
	for (const FSkeletalMeshSection& Section : Sections)
	{
		if (Section.MaterialSlotName == "None")
		{
			bNeedsNoneSlot = true;
			break;
		}
	}

	if (bNeedsNoneSlot)
	{
		FSkeletalMaterial DefaultMaterial;
		DefaultMaterial.MaterialInterface = FMaterialManager::Get().GetOrCreateMaterial("None");
		DefaultMaterial.MaterialSlotName = "None";
		DefaultMaterial.MaterialPath = DefaultMaterial.MaterialInterface
			? DefaultMaterial.MaterialInterface->GetAssetPathFileName()
			: FString();
		OutMaterials.push_back(DefaultMaterial);

		const int32 NoneMaterialIndex = static_cast<int32>(OutMaterials.size()) - 1;
		for (FSkeletalMeshSection& Section : InOutSections)
		{
			if (Section.MaterialSlotName == "None")
			{
				Section.MaterialIndex = NoneMaterialIndex;
			}
		}
	}
}

FString FFbxMaterialImporter::CreateOrUpdateMaterialAsset(const FFbxImportedMaterialInfo& MaterialInfo, const FString& SourcePath)
{
	const FString UassetPath = BuildImportedMaterialAssetPath(SourcePath, MaterialInfo.Name);

	std::filesystem::create_directories(std::filesystem::path(FPaths::ToWide(UassetPath)).parent_path());

	// Diffuse 텍스처가 있으면 SectionColor는 흰색(텍스처 알베도 유지). Emissive/무텍스처만 FBX 색 사용.
	const bool bUseImportedColor = MaterialInfo.DiffuseTexturePath.empty() || MaterialInfo.bEmissive;
	const FVector DiffuseForSection = MaterialInfo.bEmissive
		? MaterialInfo.DiffuseColor
		: SanitizeLitDiffuseColor(MaterialInfo.DiffuseColor);
	const FVector4 SectionColor = bUseImportedColor
		? FVector4(DiffuseForSection.X, DiffuseForSection.Y, DiffuseForSection.Z, 1.0f)
		: FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	const FString DiffuseTex = MaterialInfo.DiffuseTexturePath.empty() ? FString() : FPaths::MakeProjectRelative(MaterialInfo.DiffuseTexturePath);
	const FString NormalTex  = MaterialInfo.NormalTexturePath.empty()  ? FString() : FPaths::MakeProjectRelative(MaterialInfo.NormalTexturePath);
	const float EmissiveIntensity = MaterialInfo.Name == "Material.009_Emissive" ? 1.0f : 4.0f;

	bool bTransparent = MaterialInfo.bTransparent;
	bool bMasked = false;
	bool bTwoSided = MaterialInfo.bTwoSided;
	bool bEmissive = MaterialInfo.bEmissive;
	float ImportedEmissiveIntensity = EmissiveIntensity;
	float AlphaClip = 0.0f;
	if (!DiffuseTex.empty() && IsAlphaDecalSourcePath(SourcePath))
	{
		bMasked = true;
		bTransparent = false;
		bTwoSided = true;
		bEmissive = true;
		ImportedEmissiveIntensity = 1.0f;
		AlphaClip = 0.5f;
	}

	// JSON 없이 머티리얼을 직접 빌드해 .uasset(바이너리)으로 저장.
	FMaterialManager::Get().CreateImportedMaterialAsset(
		UassetPath,
		SectionColor,
		DiffuseTex,
		NormalTex,
		bEmissive,
		ImportedEmissiveIntensity,
		bTransparent,
		bMasked,
		MaterialInfo.Opacity,
		bTwoSided,
		MaterialInfo.SpecularIntensity,
		MaterialInfo.Shininess,
		MaterialInfo.Metallic,
		AlphaClip);
	return UassetPath;
}
