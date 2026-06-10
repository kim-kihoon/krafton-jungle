#include "AssetPackage.h"

#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <filesystem>

static std::wstring GetLowerExtension(const FString& Path)
{
	std::filesystem::path SrcPath(FPaths::ToWide(Path));
	std::wstring Ext = SrcPath.extension().wstring();
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
	return Ext;
}

bool FAssetPackage::IsAssetPackagePath(const FString& Path)
{
	std::wstring Ext = GetLowerExtension(Path);
	return Ext == L".uasset";
}

bool FAssetPackage::ReadHeader(const FString& Path, FAssetPackageHeader& OutHeader)
{
	FWindowsBinReader Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid()) return false;

	Ar << OutHeader;
	return Ar.IsValid() && OutHeader.IsValidPackage();
}

bool FAssetPackage::ReadPackagePrelude(
	FArchive& Ar,
	EAssetPackageType ExpectedType,
	FAssetPackageHeader& OutHeader,
	FAssetImportMetadata& OutMetadata)
{
	Ar << OutHeader;
	if (!Ar.IsValid() || !OutHeader.IsValid(ExpectedType))
	{
		return false;
	}

	Ar.SetTaggedPropertySerializationEnabled(false);

	// Legacy packages predate the explicit format seam used for future schema
	// evolution. Newer packages opt into the versioned branch explicitly even
	// though both paths still deserialize the same metadata payload for now.
	switch (OutHeader.GetFormatBranch())
	{
	case EAssetPackageFormatBranch::Legacy:
	case EAssetPackageFormatBranch::Versioned:
		Ar << OutMetadata;
		Ar.SetTaggedPropertySerializationEnabled(OutHeader.IsVersionedFormat());
		return Ar.IsValid();
	default:
		return false;
	}
}

void FAssetPackage::InitializeHeaderForSave(FAssetPackageHeader& Header, EAssetPackageType Type)
{
	Header.Magic = FAssetPackageHeader::MagicValue;
	Header.Version = FAssetPackageHeader::CurrentVersion;
	Header.Type = static_cast<uint32>(Type);
}

bool FAssetPackage::WritePackagePrelude(
	FArchive& Ar,
	EAssetPackageType Type,
	const FAssetImportMetadata& Metadata,
	FAssetPackageHeader* OutWrittenHeader)
{
	FAssetPackageHeader Header;
	InitializeHeaderForSave(Header, Type);

	FAssetImportMetadata MetadataCopy = Metadata;

	Ar << Header;
	Ar << MetadataCopy;
	Ar.SetTaggedPropertySerializationEnabled(Header.IsVersionedFormat());

	if (OutWrittenHeader)
	{
		*OutWrittenHeader = Header;
	}

	return Ar.IsValid();
}

bool FAssetPackage::GetPackageType(const FString& Path, EAssetPackageType& OutType)
{
	FAssetPackageHeader Header;
	if (!ReadHeader(Path, Header))
	{
		OutType = EAssetPackageType::Unknown;
		return false;
	}

	OutType = static_cast<EAssetPackageType>(Header.Type);
	return true;
}

bool FAssetPackage::ReadMetadata(const FString& Path, EAssetPackageType ExpectedType, FAssetImportMetadata& OutMetadata)
{
	FWindowsBinReader Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid()) return false;

	FAssetPackageHeader Header;
	return ReadPackagePrelude(Ar, ExpectedType, Header, OutMetadata);
}

bool FAssetPackage::SaveStringPayload(const FString& Path, EAssetPackageType Type, const FAssetImportMetadata& Metadata, const FString& Payload)
{
	// 패키지 헤더 + 임포트 메타데이터 + 문자열 페이로드를 순서대로 저장합니다.
	FWindowsBinWriter Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid()) return false;

	FAssetPackageHeader Header;
	FString PayloadCopy = Payload;
	if (!WritePackagePrelude(Ar, Type, Metadata, &Header))
	{
		return false;
	}
	Ar << PayloadCopy;

	return Ar.IsValid();
}

bool FAssetPackage::LoadStringPayload(const FString& Path, EAssetPackageType ExpectedType, FAssetImportMetadata& Metadata, FString& OutPayload)
{
	FWindowsBinReader Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid()) return false;

	FAssetPackageHeader Header;
	if (!ReadPackagePrelude(Ar, ExpectedType, Header, Metadata))
	{
		return false;
	}
	Ar << OutPayload;

	return Ar.IsValid();
}
