#pragma once

#include "Core/Types/CoreTypes.h"
#include "Serialization/Archive.h"

enum class EAssetPackageType : uint32
{
	Unknown = 0,
	StaticMesh,
	SkeletalMesh,
	FloatCurve,
	CameraShake,
	Material,
	Skeleton,
	AnimSequence,
	AnimMontage,
	AnimGraph,
	ParticleSystem,
	VectorField,
	LuaBlueprint,
	PhysicsAsset,
};

enum class EAssetPackageSerializationVersion : uint32
{
	LegacyBinaryLayout = 1,
	HeaderVersionedFormat = 2,
	SkeletonSocketPayload = 3,
	SkeletalMeshPhysicsAssetPayload = 4,
    MaterialGraphSourcePayload = 5,
    PhysicsAssetStringConstraintNames = 6,
    SkeletalMeshClothPayload = 7,
};

enum class EAssetPackageFormatBranch : uint8
{
	Invalid = 0,
	Legacy,
	Versioned,
};

struct FAssetPackageHeader
{
    static constexpr uint32 MagicValue     = 0x54455341; // ASET
    static constexpr uint32 LegacyVersion  = static_cast<uint32>(EAssetPackageSerializationVersion::LegacyBinaryLayout);
    static constexpr uint32 CurrentVersion = static_cast<uint32>(EAssetPackageSerializationVersion::SkeletalMeshClothPayload);

	uint32 Magic = MagicValue;
	uint32 Version = CurrentVersion;
	uint32 Type = static_cast<uint32>(EAssetPackageType::Unknown);

	friend FArchive& operator<<(FArchive& Ar, FAssetPackageHeader& Header)
	{
		Ar << Header.Magic;
		Ar << Header.Version;
		Ar << Header.Type;
		return Ar;
	}

	bool IsValid(EAssetPackageType ExpectedType) const
	{
		return Magic == MagicValue
			&& IsKnownVersion()
			&& Type == static_cast<uint32>(ExpectedType);
	}

	bool IsValidPackage() const
	{
		return Magic == MagicValue
			&& IsKnownVersion();
	}

	bool IsKnownVersion() const
	{
		return Version == LegacyVersion
		        || (Version >= static_cast<uint32>(EAssetPackageSerializationVersion::HeaderVersionedFormat)
                    && Version <= CurrentVersion);
	}

	bool IsLegacyFormat() const
	{
		return Version == LegacyVersion;
	}

	bool IsVersionedFormat() const
	{
		return Version >= static_cast<uint32>(EAssetPackageSerializationVersion::HeaderVersionedFormat)
			&& Version <= CurrentVersion;
	}

	EAssetPackageFormatBranch GetFormatBranch() const
	{
		if (IsLegacyFormat())
		{
			return EAssetPackageFormatBranch::Legacy;
		}
		if (IsVersionedFormat())
		{
			return EAssetPackageFormatBranch::Versioned;
		}
		return EAssetPackageFormatBranch::Invalid;
	}
};

struct FAssetImportMetadata
{
	FString SourcePath;
	uint64 SourceTimestamp = 0;
	uint64 SourceFileSize = 0;

	friend FArchive& operator<<(FArchive& Ar, FAssetImportMetadata& Metadata)
	{
		Ar << Metadata.SourcePath;
		Ar << Metadata.SourceTimestamp;
		Ar << Metadata.SourceFileSize;
		return Ar;
	}

	bool IsSourceAvailable() const
	{
		return !SourcePath.empty();
	}

	bool MatchesSource(uint64 CurrentTimestamp, uint64 CurrentFileSize) const
	{
		return SourceTimestamp == CurrentTimestamp
			&& SourceFileSize == CurrentFileSize;
	}
};

class FAssetPackage
{
public:
	static bool IsAssetPackagePath(const FString& Path);
	static bool ReadHeader(const FString& Path, FAssetPackageHeader& OutHeader);
	static bool GetPackageType(const FString& Path, EAssetPackageType& OutType);
	static bool ReadMetadata(const FString& Path, EAssetPackageType ExpectedType, FAssetImportMetadata& OutMetadata);
	static bool ReadPackagePrelude(FArchive& Ar, EAssetPackageType ExpectedType, FAssetPackageHeader& OutHeader, FAssetImportMetadata& OutMetadata);
	static void InitializeHeaderForSave(FAssetPackageHeader& Header, EAssetPackageType Type);
	static bool WritePackagePrelude(FArchive& Ar, EAssetPackageType Type, const FAssetImportMetadata& Metadata, FAssetPackageHeader* OutWrittenHeader = nullptr);

	static bool SaveStringPayload(const FString& Path, EAssetPackageType Type, const FAssetImportMetadata& Metadata, const FString& Payload);
	static bool LoadStringPayload(const FString& Path, EAssetPackageType ExpectedType, FAssetImportMetadata& Metadata, FString& OutPayload);
};
