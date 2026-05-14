#include "BinarySerializer.h"

#include "Asset/Skeleton.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Asset/StaticMeshTypes.h"
#include "Core/Paths.h"

#include <filesystem>
#include <chrono>
#include <cstring>

/*
 *	Raw Binary Serialization
 * [장점]
 *	- struct 그대로 write
 *	- 빠름
 *	- 구현 간단
 *	
 *	[단점]
 *	- ABI(Application Binary Interface) 의존성 - 컴파일러에 따라 다르게 해석 가능
 *	- padding 문제
 *	- 플랫폼 종속
 *	
 *	[언리얼과 비교]
 *	- 언리얼은 그냥 write가 아닌 Serialization Abstraction이 존재
 *	- 이를 통해 엔디안, padding 등에 대응 가능
 *	- 또한 Vertices, Indices 등을 Chunk 단위로 묶음 (일부만 로딩 혹은 streaming 가능)
 *	- 이는 Offset 기반으로도 실현 가능 (Jump)
 *
 *	[현재 수정 방향]
 *	- 파일 포맷은 Little-Endian으로 고정
 *	- Header / Body를 struct 통째로 write 하지 않고 멤버 단위 serialize
 *	- 즉, padding / endianness 문제를 줄이는 방향으로 수정
 */

/* Validation Check Constants */
constexpr uint32 COMPILED_ASSET_MAGIC = 0x4153504E; // 'NPSA'
constexpr uint32 COMPILED_ASSET_HEADER_VERSION = 1;
constexpr uint32 STATIC_MESH_PAYLOAD_VERSION = 3;
constexpr uint32 SKELETAL_MESH_PAYLOAD_VERSION = 1;
constexpr uint32 SKELETON_PAYLOAD_VERSION = 1;

//	Vailidation Checkers
constexpr uint32 MAX_STATIC_MESH_VERTEX_COUNT   = 10'000'000;
constexpr uint32 MAX_STATIC_MESH_INDEX_COUNT    = 30'000'000;
constexpr uint32 MAX_STATIC_MESH_SECTION_COUNT  = 100'000;
constexpr uint32 MAX_STATIC_MESH_SLOTNAME_COUNT = 1024;
constexpr uint32 MAX_SKELETON_BONE_COUNT        = 4096;
constexpr uint32 MAX_STRING_LENGTH              = 4096;

static bool IsValidCompiledAssetHeaderBase(const FStaticMeshBinaryHeader& Header)
{
	if (Header.Magic != COMPILED_ASSET_MAGIC)
	{
		return false;
	}

	if (Header.HeaderVersion != COMPILED_ASSET_HEADER_VERSION)
	{
		return false;
	}

	if (Header.AssetType != ECompiledAssetType::StaticMesh &&
		Header.AssetType != ECompiledAssetType::SkeletalMesh &&
		Header.AssetType != ECompiledAssetType::Skeleton)
	{
		return false;
	}

	return true;
}

static bool IsValidMeshPayloadCounts(const FStaticMeshBinaryHeader& Header)
{
	if (Header.VertexCount > MAX_STATIC_MESH_VERTEX_COUNT)
	{
		return false;
	}

	if (Header.IndexCount > MAX_STATIC_MESH_INDEX_COUNT)
	{
		return false;
	}

	if (Header.SectionCount > MAX_STATIC_MESH_SECTION_COUNT)
	{
		return false;
	}

	if (Header.SlotCount > MAX_STATIC_MESH_SLOTNAME_COUNT)
	{
		return false;
	}

	return true;
}

static bool IsValidStaticMeshHeader(const FStaticMeshBinaryHeader& Header)
{
	return IsValidCompiledAssetHeaderBase(Header)
		&& Header.AssetType == ECompiledAssetType::StaticMesh
		&& Header.PayloadVersion == STATIC_MESH_PAYLOAD_VERSION
		&& IsValidMeshPayloadCounts(Header);
}

static bool IsValidSkeletalMeshHeader(const FStaticMeshBinaryHeader& Header)
{
	return IsValidCompiledAssetHeaderBase(Header)
		&& Header.AssetType == ECompiledAssetType::SkeletalMesh
		&& Header.PayloadVersion == SKELETAL_MESH_PAYLOAD_VERSION
		&& IsValidMeshPayloadCounts(Header);
}

static bool IsValidSkeletonHeader(const FStaticMeshBinaryHeader& Header)
{
	return Header.Magic == COMPILED_ASSET_MAGIC
		&& Header.HeaderVersion == COMPILED_ASSET_HEADER_VERSION
		&& Header.AssetType == ECompiledAssetType::Skeleton
		&& Header.PayloadVersion == SKELETON_PAYLOAD_VERSION
		&& Header.VertexCount <= MAX_SKELETON_BONE_COUNT
		&& Header.IndexCount == 0
		&& Header.SectionCount == 0
		&& Header.SlotCount <= MAX_SKELETON_BONE_COUNT;
}

/* Time Checker */
static uint64 GetFileWriteTimeTicks(const FString& Path)
{
	namespace fs = std::filesystem;

	fs::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(Path)));
	if (!fs::exists(FilePath))
	{
		return 0;
	}

	auto WriteTime = fs::last_write_time(FilePath);
	auto Duration = WriteTime.time_since_epoch();
	return static_cast<uint64>(std::chrono::duration_cast<std::chrono::seconds>(Duration).count());
}

static uint64 HashFileFNV1a(const FString& Path)
{
	namespace fs = std::filesystem;

	std::ifstream File(fs::path(FPaths::ToAbsolute(FPaths::ToWide(Path))), std::ios::binary);
	if (!File.is_open())
	{
		return 0;
	}

	uint64 Hash = 14695981039346656037ull;
	char Buffer[64 * 1024];
	while (File.good())
	{
		File.read(Buffer, sizeof(Buffer));
		const std::streamsize ReadBytes = File.gcount();
		for (std::streamsize Index = 0; Index < ReadBytes; ++Index)
		{
			Hash ^= static_cast<unsigned char>(Buffer[Index]);
			Hash *= 1099511628211ull;
		}
	}

	return Hash;
}

/* Primitive LE Writers */
void FBinarySerializer::WriteInt32LE(std::ofstream& Out, int32 Value)
{
	WriteUInt32LE(Out, static_cast<uint32>(Value));
}

void FBinarySerializer::WriteUInt32LE(std::ofstream& Out, uint32 Value)
{
	//	하위 바이트부터 저장하는 Little Endian [LSB -> MSB]
	unsigned char Bytes[4];
	Bytes[0] = static_cast<unsigned char>((Value >> 0) & 0xFF);
	Bytes[1] = static_cast<unsigned char>((Value >> 8) & 0xFF);
	Bytes[2] = static_cast<unsigned char>((Value >> 16) & 0xFF);
	Bytes[3] = static_cast<unsigned char>((Value >> 24) & 0xFF);

	//	reinterpret_cast : 주소를 그저 byte로 해석하라 (타입에 대하여 고려하지 않고, 비트 그대로 해석)
	//	이 메모리를 그냥 바이트 덩어리로 넘기고 싶을 때 사용 (unsigned char -> char *로 API 요구 타입만 변경)
	Out.write(reinterpret_cast<const char*>(Bytes), 4);
}

void FBinarySerializer::WriteUInt64LE(std::ofstream& Out, uint64 Value)
{
	unsigned char Bytes[8];
	Bytes[0] = static_cast<unsigned char>((Value >> 0) & 0xFF);
	Bytes[1] = static_cast<unsigned char>((Value >> 8) & 0xFF);
	Bytes[2] = static_cast<unsigned char>((Value >> 16) & 0xFF);
	Bytes[3] = static_cast<unsigned char>((Value >> 24) & 0xFF);
	Bytes[4] = static_cast<unsigned char>((Value >> 32) & 0xFF);
	Bytes[5] = static_cast<unsigned char>((Value >> 40) & 0xFF);
	Bytes[6] = static_cast<unsigned char>((Value >> 48) & 0xFF);
	Bytes[7] = static_cast<unsigned char>((Value >> 56) & 0xFF);
	
	Out.write(reinterpret_cast<const char*>(Bytes), 8);
}

void FBinarySerializer::WriteFloatLE(std::ofstream& Out, float Value)
{
	static_assert(sizeof(float) == sizeof(uint32), "float size must be 4 bytes");

	//	float -> uint32 비트 그대로 복사 (해석만 다르게)
	//	float 3.14f → 0x4048F5C3 (IEEE 754)
	//	reinterpret_cast를 사용하면 안됨 (strict aliasing violation, UB 가능, 최적화에서 깨질 수 있음)
		//	컴파일러는 서로 다른 타입의 포인터는 같은 메모리를 가리키지 않을 것이라고 간주해버림
		//	Release 컴파일러 최적화에서 깨질 수 있음 (컴파일러의 가정을 깨기 때문임)
	//	타입은 다르지만 비트 패턴을 그대로 복사하고 싶을 때 memcpy 사용
	uint32 Bits = 0;
	std::memcpy(&Bits, &Value, sizeof(float));
	WriteUInt32LE(Out, Bits);
}

/* Primitive LE Readers */
bool FBinarySerializer::ReadInt32LE(std::ifstream& In, int32& OutValue) const
{
	uint32 Bits = 0;
	if (!ReadUInt32LE(In, Bits))
	{
		return false;
	}

	OutValue = static_cast<int32>(Bits);
	return true;
}

bool FBinarySerializer::ReadUInt32LE(std::ifstream& In, uint32& OutValue) const
{
	unsigned char Bytes[4] = {};
	In.read(reinterpret_cast<char*>(Bytes), 4);

	if (!In.good())
	{
		return false;
	}

	OutValue =
		(static_cast<uint32>(Bytes[0]) << 0) |
		(static_cast<uint32>(Bytes[1]) << 8) |
		(static_cast<uint32>(Bytes[2]) << 16) |
		(static_cast<uint32>(Bytes[3]) << 24);

	return true;
}

bool FBinarySerializer::ReadUInt64LE(std::ifstream& In, uint64& OutValue) const
{
	unsigned char Bytes[8] = {};
	In.read(reinterpret_cast<char*>(Bytes), 8);

	if (!In.good())
	{
		return false;
	}

	OutValue =
		(static_cast<uint64>(Bytes[0]) << 0)  |
		(static_cast<uint64>(Bytes[1]) << 8)  |
		(static_cast<uint64>(Bytes[2]) << 16) |
		(static_cast<uint64>(Bytes[3]) << 24) |
		(static_cast<uint64>(Bytes[4]) << 32) |
		(static_cast<uint64>(Bytes[5]) << 40) |
		(static_cast<uint64>(Bytes[6]) << 48) |
		(static_cast<uint64>(Bytes[7]) << 56);

	return true;
}

bool FBinarySerializer::ReadFloatLE(std::ifstream& In, float& OutValue) const
{
	uint32 Bits = 0;
	if (!ReadUInt32LE(In, Bits))
	{
		return false;
	}

	std::memcpy(&OutValue, &Bits, sizeof(float));
	return true;
}

void FBinarySerializer::WriteMatrix(std::ofstream& Out, const FMatrix& Matrix)
{
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Col = 0; Col < 4; ++Col)
		{
			WriteFloatLE(Out, Matrix.M[Row][Col]);
		}
	}
}

bool FBinarySerializer::ReadMatrix(std::ifstream& In, FMatrix& OutMatrix) const
{
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Col = 0; Col < 4; ++Col)
		{
			if (!ReadFloatLE(In, OutMatrix.M[Row][Col]))
			{
				return false;
			}
		}
	}

	return true;
}

void FBinarySerializer::WriteTransform(std::ofstream& Out, const FTransform& Transform)
{
	WriteMatrix(Out, Transform.ToMatrixWithScale());
}

bool FBinarySerializer::ReadTransform(std::ifstream& In, FTransform& OutTransform) const
{
	FMatrix Matrix = FMatrix::Identity;
	if (!ReadMatrix(In, Matrix))
	{
		return false;
	}

	OutTransform = FTransform(Matrix);
	return true;
}

/* Header Serialization */
void FBinarySerializer::WriteHeader(std::ofstream& Out, const FStaticMeshBinaryHeader& Header)
{
	WriteUInt32LE(Out, Header.Magic);
	WriteUInt32LE(Out, Header.HeaderVersion);
	WriteUInt32LE(Out, static_cast<uint32>(Header.AssetType));
	WriteUInt32LE(Out, Header.PayloadVersion);
	WriteUInt32LE(Out, Header.Flags);
	WriteUInt32LE(Out, Header.VertexCount);
	WriteUInt32LE(Out, Header.IndexCount);
	WriteUInt32LE(Out, Header.SectionCount);
	WriteUInt32LE(Out, Header.SlotCount);
	WriteUInt64LE(Out, Header.SourceFileWriteTime);
	WriteUInt64LE(Out, Header.SourceFileHash);
}

bool FBinarySerializer::ReadHeader(std::ifstream& In, FStaticMeshBinaryHeader& OutHeader) const
{
	uint32 AssetTypeValue = 0;
	const bool bOk = ReadUInt32LE(In, OutHeader.Magic)
		&& ReadUInt32LE(In, OutHeader.HeaderVersion)
		&& ReadUInt32LE(In, AssetTypeValue)
		&& ReadUInt32LE(In, OutHeader.PayloadVersion)
		&& ReadUInt32LE(In, OutHeader.Flags)
		&& ReadUInt32LE(In, OutHeader.VertexCount)
		&& ReadUInt32LE(In, OutHeader.IndexCount)
		&& ReadUInt32LE(In, OutHeader.SectionCount)
		&& ReadUInt32LE(In, OutHeader.SlotCount)
		&& ReadUInt64LE(In, OutHeader.SourceFileWriteTime)
		&& ReadUInt64LE(In, OutHeader.SourceFileHash);

	OutHeader.AssetType = static_cast<ECompiledAssetType>(AssetTypeValue);
	return bOk;
}

void FBinarySerializer::WriteString(std::ofstream& Out, const FString& String)
{
	//	Length + Data Pattern
	uint32 Length = static_cast<uint32>(String.length());
	WriteUInt32LE(Out, Length);

	if (Length > 0)
	{
		/*
		 *	주의:
		 *	- FString::value_type 크기에 의존함
		 *	- 현재 프로젝트가 wchar_t 기반이라면 같은 프로젝트 내에서는 문제없음
		 *	- 완전한 플랫폼 독립 문자열 포맷이 필요하면 UTF-8 변환 후 byte array로 저장하는 편이 더 낫다.
		 */
		Out.write(reinterpret_cast<const char*>(String.c_str()), sizeof(FString::value_type) * Length);
	}
}

bool FBinarySerializer::ReadString(std::ifstream& In, FString& OutString) const
{
	uint32 Length = 0;
	if (!ReadUInt32LE(In, Length))
	{
		OutString.clear();
		return false;
	}

	if (Length > MAX_STRING_LENGTH)
	{
		In.setstate(std::ios::failbit);
		OutString.clear();
		return false;
	}

	OutString.resize(Length);

	if (Length > 0)
	{
		In.read(reinterpret_cast<char*>(OutString.data()), sizeof(FString::value_type) * Length);

		if (!In.good())
		{
			OutString.clear();
			return false;
		}
	}

	return true;
}

void FBinarySerializer::WriteIndexArray(std::ofstream& Out, const TArray<uint32>& Array)
{
	//	Length + Data Pattern
	uint32 Count = static_cast<uint32>(Array.size());
	WriteUInt32LE(Out, Count);

	for (uint32 Value : Array)
	{
		WriteUInt32LE(Out, Value);
	}
}

bool FBinarySerializer::ReadIndexArray(std::ifstream& In, TArray<uint32>& OutArray) const
{
	uint32 Count = 0;
	if (!ReadUInt32LE(In, Count))
	{
		return false;
	}

	if (Count > MAX_STATIC_MESH_INDEX_COUNT)
	{
		In.setstate(std::ios::failbit);
		return false;
	}

	OutArray.resize(Count);

	for (uint32 i = 0; i < Count; i++)
	{
		if (!ReadUInt32LE(In, OutArray[i]))
		{
			return false;
		}
	}

	return true;
}

void FBinarySerializer::WriteVertices(std::ofstream& Out, const FStaticMesh& Data)
{

	uint32 Count = static_cast<uint32>(Data.Vertices.size());
	WriteUInt32LE(Out, Count);

	for (const FNormalVertex& Vertex : Data.Vertices)
	{
		//	Position
		WriteFloatLE(Out, Vertex.Position.X);
		WriteFloatLE(Out, Vertex.Position.Y);
		WriteFloatLE(Out, Vertex.Position.Z);

		//	Color
		WriteFloatLE(Out, Vertex.Color.R);
		WriteFloatLE(Out, Vertex.Color.G);
		WriteFloatLE(Out, Vertex.Color.B);
		WriteFloatLE(Out, Vertex.Color.A);

		//	Normal
		WriteFloatLE(Out, Vertex.Normal.X);
		WriteFloatLE(Out, Vertex.Normal.Y);
		WriteFloatLE(Out, Vertex.Normal.Z);

		//	UVs
		WriteFloatLE(Out, Vertex.UVs.X);
		WriteFloatLE(Out, Vertex.UVs.Y);

		//	Tangent
		WriteFloatLE(Out, Vertex.Tangent.X);
		WriteFloatLE(Out, Vertex.Tangent.Y);
		WriteFloatLE(Out, Vertex.Tangent.Z);

		//	Bitangent
		WriteFloatLE(Out, Vertex.Bitangent.X);
		WriteFloatLE(Out, Vertex.Bitangent.Y);
		WriteFloatLE(Out, Vertex.Bitangent.Z);
	}
}

bool FBinarySerializer::ReadVertices(std::ifstream& In, FStaticMesh& OutData, uint32 VertexCount) const
{
	uint32 Count = 0;
	if (!ReadUInt32LE(In, Count))
	{
		return false;
	}

	if (Count != VertexCount || Count > MAX_STATIC_MESH_VERTEX_COUNT)
	{
		In.setstate(std::ios::failbit);
		return false;
	}

	OutData.Vertices.resize(Count);

	for (FNormalVertex& Vertex : OutData.Vertices)
	{
		//	Position
		if (!ReadFloatLE(In, Vertex.Position.X) ||
			!ReadFloatLE(In, Vertex.Position.Y) ||
			!ReadFloatLE(In, Vertex.Position.Z))
		{
			return false;
		}

		//	Color
		if (!ReadFloatLE(In, Vertex.Color.R) ||
			!ReadFloatLE(In, Vertex.Color.G) ||
			!ReadFloatLE(In, Vertex.Color.B) ||
			!ReadFloatLE(In, Vertex.Color.A))
		{
			return false;
		}

		//	Normal
		if (!ReadFloatLE(In, Vertex.Normal.X) ||
			!ReadFloatLE(In, Vertex.Normal.Y) ||
			!ReadFloatLE(In, Vertex.Normal.Z))
		{
			return false;
		}

		//	UVs
		if (!ReadFloatLE(In, Vertex.UVs.X) ||
			!ReadFloatLE(In, Vertex.UVs.Y))
		{
			return false;
		}

		//	Tangent
		if (!ReadFloatLE(In, Vertex.Tangent.X) ||
			!ReadFloatLE(In, Vertex.Tangent.Y) ||
			!ReadFloatLE(In, Vertex.Tangent.Z))
		{
			return false;
		}

		//	Bitangent
		if (!ReadFloatLE(In, Vertex.Bitangent.X) ||
			!ReadFloatLE(In, Vertex.Bitangent.Y) ||
			!ReadFloatLE(In, Vertex.Bitangent.Z))
		{
			return false;
		}
	}

	return In.good();
}

void FBinarySerializer::WriteSections(std::ofstream& Out, const FStaticMesh& Data)
{
	uint32 Count = static_cast<uint32>(Data.Sections.size());
	WriteUInt32LE(Out, Count);

	for (const FStaticMeshSection& Section : Data.Sections)
	{
		WriteUInt32LE(Out, Section.StartIndex);
		WriteUInt32LE(Out, Section.IndexCount);
		WriteInt32LE(Out, Section.MaterialSlotIndex);
	}
}

bool FBinarySerializer::ReadSections(std::ifstream& In, FStaticMesh& OutData, uint32 SectionCount) const
{
	uint32 Count = 0;
	if (!ReadUInt32LE(In, Count))
	{
		return false;
	}

	if (Count != SectionCount || Count > MAX_STATIC_MESH_SECTION_COUNT)
	{
		In.setstate(std::ios::failbit);
		return false;
	}

	OutData.Sections.resize(Count);

	for (FStaticMeshSection& Section : OutData.Sections)
	{
		if (!ReadUInt32LE(In, Section.StartIndex) ||
			!ReadUInt32LE(In, Section.IndexCount) ||
			!ReadInt32LE(In, Section.MaterialSlotIndex))
		{
			return false;
		}
	}

	return In.good();
}

void FBinarySerializer::WriteBounds(std::ofstream& Out, const FStaticMesh& Data)
{

	WriteFloatLE(Out, Data.LocalBounds.Min.X);
	WriteFloatLE(Out, Data.LocalBounds.Min.Y);
	WriteFloatLE(Out, Data.LocalBounds.Min.Z);

	WriteFloatLE(Out, Data.LocalBounds.Max.X);
	WriteFloatLE(Out, Data.LocalBounds.Max.Y);
	WriteFloatLE(Out, Data.LocalBounds.Max.Z);
}

bool FBinarySerializer::ReadBounds(std::ifstream& In, FStaticMesh& OutData) const
{

	return ReadFloatLE(In, OutData.LocalBounds.Min.X)
		&& ReadFloatLE(In, OutData.LocalBounds.Min.Y)
		&& ReadFloatLE(In, OutData.LocalBounds.Min.Z)
		&& ReadFloatLE(In, OutData.LocalBounds.Max.X)
		&& ReadFloatLE(In, OutData.LocalBounds.Max.Y)
		&& ReadFloatLE(In, OutData.LocalBounds.Max.Z);
}

//	보내는 순서와 읽는 순서는 동일 (Header + Body 순서를 고정 -> protocol의 정의)
bool FBinarySerializer::SaveStaticMesh(const FString& BinaryPath, const FString& SourcePath, const FStaticMesh& Data)
{
	namespace fs = std::filesystem;

	const fs::path AbsoluteBinaryPath(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath)));
	std::error_code Ec;
	fs::create_directories(AbsoluteBinaryPath.parent_path(), Ec);

	std::ofstream Out(AbsoluteBinaryPath, std::ios::binary);
	if (!Out.is_open())
	{
		return false;
	}
	
	//	Packet Header와 유사한 개념 (쓰레기 데이터를 읽지 않기 위함)
	FStaticMeshBinaryHeader Header;
	Header.Magic = COMPILED_ASSET_MAGIC;	//	우리의 포맷인지 확인
	Header.HeaderVersion = COMPILED_ASSET_HEADER_VERSION;
	Header.AssetType = ECompiledAssetType::StaticMesh;
	Header.PayloadVersion = STATIC_MESH_PAYLOAD_VERSION;	//	포맷이 변경되었을 시 Version을 통해 무력화 혹은 대응 가능
	Header.Flags = 0;
	//	Count류 -> Parsing 안정성
	Header.VertexCount = static_cast<uint32>(Data.Vertices.size());
	Header.IndexCount = static_cast<uint32>(Data.Indices.size());
	Header.SectionCount = static_cast<uint32>(Data.Sections.size());
	Header.SlotCount = static_cast<uint32>(Data.Slots.size());
	Header.SourceFileWriteTime = GetFileWriteTimeTicks(SourcePath);
	Header.SourceFileHash = HashFileFNV1a(SourcePath);

	if (!IsValidStaticMeshHeader(Header))
	{
		return false;
	}

	WriteHeader(Out, Header);

	WriteString(Out, FPaths::Normalize(BinaryPath));
	WriteVertices(Out, Data);
	WriteIndexArray(Out, Data.Indices);
	WriteSections(Out, Data);

	uint32 Count = static_cast<uint32>(Data.Slots.size());
	WriteUInt32LE(Out, Count);
	for (const auto& Slot : Data.Slots)
	{
		WriteString(Out, Slot.SlotName);
		WriteString(Out, Slot.MaterialAssetPath);
	}

	WriteBounds(Out, Data);

	return Out.good();
}

bool FBinarySerializer::LoadStaticMesh(const FString& BinaryPath, FStaticMesh& OutData)
{
	std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath))), std::ios::binary);
	if (!In.is_open())
	{
		return false;
	}

	FStaticMeshBinaryHeader Header;
	if (!ReadHeader(In, Header))
	{
		return false;
	}

	if (!IsValidStaticMeshHeader(Header))
	{
		return false;
	}

	if (!ReadString(In, OutData.PathFileName))
	{
		return false;
	}

	if (!ReadVertices(In, OutData, Header.VertexCount))
	{
		return false;
	}

	if (!ReadIndexArray(In, OutData.Indices))
	{
		return false;
	}

	if (!ReadSections(In, OutData, Header.SectionCount))
	{
		return false;
	}

	uint32 Count = 0;
	if (!ReadUInt32LE(In, Count))
	{
		return false;
	}

	if (Count != Header.SlotCount || Count > MAX_STATIC_MESH_SLOTNAME_COUNT)
	{
		return false;
	}

	OutData.Slots.resize(Count);

	for (uint32 i = 0; i < Count; i++)
	{
		if (!ReadString(In, OutData.Slots[i].SlotName) ||
			!ReadString(In, OutData.Slots[i].MaterialAssetPath))
		{
			return false;
		}
	}

	if (!ReadBounds(In, OutData))
	{
		return false;
	}

	if (!In.good())
	{
		return false;
	}

	return OutData.Vertices.size() == Header.VertexCount
		&& OutData.Indices.size() == Header.IndexCount
		&& OutData.Sections.size() == Header.SectionCount
		&& OutData.Slots.size() == Header.SlotCount;
}

bool FBinarySerializer::SaveSkeleton(const FString& BinaryPath, const FString& SourcePath, const FSkeleton& Data)
{
	namespace fs = std::filesystem;

	const fs::path AbsoluteBinaryPath(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath)));
	std::error_code Ec;
	fs::create_directories(AbsoluteBinaryPath.parent_path(), Ec);

	std::ofstream Out(AbsoluteBinaryPath, std::ios::binary);
	if (!Out.is_open())
	{
		return false;
	}

	FStaticMeshBinaryHeader Header;
	Header.Magic = COMPILED_ASSET_MAGIC;
	Header.HeaderVersion = COMPILED_ASSET_HEADER_VERSION;
	Header.AssetType = ECompiledAssetType::Skeleton;
	Header.PayloadVersion = SKELETON_PAYLOAD_VERSION;
	Header.Flags = 0;
	Header.VertexCount = static_cast<uint32>(Data.Bones.size());
	Header.IndexCount = 0;
	Header.SectionCount = 0;
	Header.SlotCount = static_cast<uint32>(Data.InverseBindPoseMatrices.size());
	Header.SourceFileWriteTime = GetFileWriteTimeTicks(SourcePath);
	Header.SourceFileHash = HashFileFNV1a(SourcePath);

	if (!IsValidSkeletonHeader(Header) || Header.VertexCount != Header.SlotCount)
	{
		return false;
	}

	WriteHeader(Out, Header);
	WriteString(Out, FPaths::Normalize(BinaryPath));

	WriteUInt32LE(Out, static_cast<uint32>(Data.Bones.size()));
	for (const FSkeletalBone& Bone : Data.Bones)
	{
		WriteString(Out, Bone.Name);
		WriteInt32LE(Out, Bone.ParentIndex);
		WriteTransform(Out, Bone.ReferenceLocalTransform);
	}

	WriteUInt32LE(Out, static_cast<uint32>(Data.InverseBindPoseMatrices.size()));
	for (const FMatrix& InverseBindPoseMatrix : Data.InverseBindPoseMatrices)
	{
		WriteMatrix(Out, InverseBindPoseMatrix);
	}

	return Out.good();
}

bool FBinarySerializer::LoadSkeleton(const FString& BinaryPath, FSkeleton& OutData)
{
	std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath))), std::ios::binary);
	if (!In.is_open())
	{
		return false;
	}

	FStaticMeshBinaryHeader Header;
	if (!ReadHeader(In, Header) || !IsValidSkeletonHeader(Header) || Header.VertexCount != Header.SlotCount)
	{
		return false;
	}

	OutData = FSkeleton();
	if (!ReadString(In, OutData.PathFileName))
	{
		return false;
	}

	uint32 BoneCount = 0;
	if (!ReadUInt32LE(In, BoneCount) ||
		BoneCount != Header.VertexCount ||
		BoneCount > MAX_SKELETON_BONE_COUNT)
	{
		return false;
	}

	OutData.Bones.resize(BoneCount);
	for (FSkeletalBone& Bone : OutData.Bones)
	{
		if (!ReadString(In, Bone.Name) ||
			!ReadInt32LE(In, Bone.ParentIndex) ||
			!ReadTransform(In, Bone.ReferenceLocalTransform))
		{
			return false;
		}
	}

	uint32 InverseBindPoseCount = 0;
	if (!ReadUInt32LE(In, InverseBindPoseCount) ||
		InverseBindPoseCount != Header.SlotCount ||
		InverseBindPoseCount != BoneCount)
	{
		return false;
	}

	OutData.InverseBindPoseMatrices.resize(InverseBindPoseCount);
	for (FMatrix& InverseBindPoseMatrix : OutData.InverseBindPoseMatrices)
	{
		if (!ReadMatrix(In, InverseBindPoseMatrix))
		{
			return false;
		}
	}

	return In.good() &&
		OutData.Bones.size() == Header.VertexCount &&
		OutData.InverseBindPoseMatrices.size() == Header.SlotCount;
}

bool FBinarySerializer::SaveSkeletalMesh(const FString& BinaryPath, const FString& SourcePath, const FSkeletalMesh& Data)
{
	namespace fs = std::filesystem;

	const fs::path AbsoluteBinaryPath(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath)));
	std::error_code Ec;
	fs::create_directories(AbsoluteBinaryPath.parent_path(), Ec);

	std::ofstream Out(AbsoluteBinaryPath, std::ios::binary);
	if (!Out.is_open())
	{
		return false;
	}

	FStaticMeshBinaryHeader Header;
	Header.Magic = COMPILED_ASSET_MAGIC;
	Header.HeaderVersion = COMPILED_ASSET_HEADER_VERSION;
	Header.AssetType = ECompiledAssetType::SkeletalMesh;
	Header.PayloadVersion = SKELETAL_MESH_PAYLOAD_VERSION;
	Header.Flags = 0;
	Header.VertexCount = static_cast<uint32>(Data.Vertices.size());
	Header.IndexCount = static_cast<uint32>(Data.Indices.size());
	Header.SectionCount = static_cast<uint32>(Data.Sections.size());
	Header.SlotCount = static_cast<uint32>(Data.MaterialSlots.size());
	Header.SourceFileWriteTime = GetFileWriteTimeTicks(SourcePath);
	Header.SourceFileHash = HashFileFNV1a(SourcePath);

	if (!IsValidSkeletalMeshHeader(Header))
	{
		return false;
	}

	WriteHeader(Out, Header);
	WriteString(Out, FPaths::Normalize(BinaryPath));
	WriteString(Out, FPaths::Normalize(Data.SkeletonAssetPath));

	WriteUInt32LE(Out, static_cast<uint32>(Data.Vertices.size()));
	for (const FSkeletalMeshVertex& Vertex : Data.Vertices)
	{
		WriteFloatLE(Out, Vertex.Position.X);
		WriteFloatLE(Out, Vertex.Position.Y);
		WriteFloatLE(Out, Vertex.Position.Z);

		WriteFloatLE(Out, Vertex.Color.R);
		WriteFloatLE(Out, Vertex.Color.G);
		WriteFloatLE(Out, Vertex.Color.B);
		WriteFloatLE(Out, Vertex.Color.A);

		WriteFloatLE(Out, Vertex.Normal.X);
		WriteFloatLE(Out, Vertex.Normal.Y);
		WriteFloatLE(Out, Vertex.Normal.Z);

		WriteFloatLE(Out, Vertex.UVs.X);
		WriteFloatLE(Out, Vertex.UVs.Y);

		WriteFloatLE(Out, Vertex.Tangent.X);
		WriteFloatLE(Out, Vertex.Tangent.Y);
		WriteFloatLE(Out, Vertex.Tangent.Z);

		WriteFloatLE(Out, Vertex.Bitangent.X);
		WriteFloatLE(Out, Vertex.Bitangent.Y);
		WriteFloatLE(Out, Vertex.Bitangent.Z);

		for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
		{
			WriteInt32LE(Out, Vertex.BoneIndices[InfluenceIndex]);
			WriteFloatLE(Out, Vertex.BoneWeights[InfluenceIndex]);
		}
	}

	WriteIndexArray(Out, Data.Indices);

	WriteUInt32LE(Out, static_cast<uint32>(Data.Sections.size()));
	for (const FSkeletalMeshSection& Section : Data.Sections)
	{
		WriteUInt32LE(Out, Section.StartIndex);
		WriteUInt32LE(Out, Section.IndexCount);
		WriteInt32LE(Out, Section.MaterialSlotIndex);
	}

	WriteUInt32LE(Out, static_cast<uint32>(Data.MaterialSlots.size()));
	for (const FSkeletalMeshMaterialSlot& Slot : Data.MaterialSlots)
	{
		WriteString(Out, Slot.SlotName);
		WriteString(Out, Slot.MaterialAssetPath);
		WriteString(Out, Slot.ExtractedDiffusePath);
		WriteString(Out, Slot.ExtractedNormalPath);
		WriteString(Out, Slot.ExtractedSpecularPath);
	}

	WriteFloatLE(Out, Data.LocalBounds.Min.X);
	WriteFloatLE(Out, Data.LocalBounds.Min.Y);
	WriteFloatLE(Out, Data.LocalBounds.Min.Z);
	WriteFloatLE(Out, Data.LocalBounds.Max.X);
	WriteFloatLE(Out, Data.LocalBounds.Max.Y);
	WriteFloatLE(Out, Data.LocalBounds.Max.Z);

	return Out.good();
}

bool FBinarySerializer::LoadSkeletalMesh(const FString& BinaryPath, FSkeletalMesh& OutData)
{
	std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath))), std::ios::binary);
	if (!In.is_open())
	{
		return false;
	}

	FStaticMeshBinaryHeader Header;
	if (!ReadHeader(In, Header) || !IsValidSkeletalMeshHeader(Header))
	{
		return false;
	}

	OutData = FSkeletalMesh();
	if (!ReadString(In, OutData.PathFileName) ||
		!ReadString(In, OutData.SkeletonAssetPath))
	{
		return false;
	}

	uint32 VertexCount = 0;
	if (!ReadUInt32LE(In, VertexCount) ||
		VertexCount != Header.VertexCount ||
		VertexCount > MAX_STATIC_MESH_VERTEX_COUNT)
	{
		return false;
	}

	OutData.Vertices.resize(VertexCount);
	for (FSkeletalMeshVertex& Vertex : OutData.Vertices)
	{
		if (!ReadFloatLE(In, Vertex.Position.X) ||
			!ReadFloatLE(In, Vertex.Position.Y) ||
			!ReadFloatLE(In, Vertex.Position.Z) ||
			!ReadFloatLE(In, Vertex.Color.R) ||
			!ReadFloatLE(In, Vertex.Color.G) ||
			!ReadFloatLE(In, Vertex.Color.B) ||
			!ReadFloatLE(In, Vertex.Color.A) ||
			!ReadFloatLE(In, Vertex.Normal.X) ||
			!ReadFloatLE(In, Vertex.Normal.Y) ||
			!ReadFloatLE(In, Vertex.Normal.Z) ||
			!ReadFloatLE(In, Vertex.UVs.X) ||
			!ReadFloatLE(In, Vertex.UVs.Y) ||
			!ReadFloatLE(In, Vertex.Tangent.X) ||
			!ReadFloatLE(In, Vertex.Tangent.Y) ||
			!ReadFloatLE(In, Vertex.Tangent.Z) ||
			!ReadFloatLE(In, Vertex.Bitangent.X) ||
			!ReadFloatLE(In, Vertex.Bitangent.Y) ||
			!ReadFloatLE(In, Vertex.Bitangent.Z))
		{
			return false;
		}

		for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
		{
			if (!ReadInt32LE(In, Vertex.BoneIndices[InfluenceIndex]) ||
				!ReadFloatLE(In, Vertex.BoneWeights[InfluenceIndex]))
			{
				return false;
			}
		}
	}

	if (!ReadIndexArray(In, OutData.Indices))
	{
		return false;
	}

	uint32 SectionCount = 0;
	if (!ReadUInt32LE(In, SectionCount) ||
		SectionCount != Header.SectionCount ||
		SectionCount > MAX_STATIC_MESH_SECTION_COUNT)
	{
		return false;
	}

	OutData.Sections.resize(SectionCount);
	for (FSkeletalMeshSection& Section : OutData.Sections)
	{
		if (!ReadUInt32LE(In, Section.StartIndex) ||
			!ReadUInt32LE(In, Section.IndexCount) ||
			!ReadInt32LE(In, Section.MaterialSlotIndex))
		{
			return false;
		}
	}

	uint32 SlotCount = 0;
	if (!ReadUInt32LE(In, SlotCount) ||
		SlotCount != Header.SlotCount ||
		SlotCount > MAX_STATIC_MESH_SLOTNAME_COUNT)
	{
		return false;
	}

	OutData.MaterialSlots.resize(SlotCount);
	for (FSkeletalMeshMaterialSlot& Slot : OutData.MaterialSlots)
	{
		if (!ReadString(In, Slot.SlotName) ||
			!ReadString(In, Slot.MaterialAssetPath) ||
			!ReadString(In, Slot.ExtractedDiffusePath) ||
			!ReadString(In, Slot.ExtractedNormalPath) ||
			!ReadString(In, Slot.ExtractedSpecularPath))
		{
			return false;
		}
	}

	if (!ReadFloatLE(In, OutData.LocalBounds.Min.X) ||
		!ReadFloatLE(In, OutData.LocalBounds.Min.Y) ||
		!ReadFloatLE(In, OutData.LocalBounds.Min.Z) ||
		!ReadFloatLE(In, OutData.LocalBounds.Max.X) ||
		!ReadFloatLE(In, OutData.LocalBounds.Max.Y) ||
		!ReadFloatLE(In, OutData.LocalBounds.Max.Z))
	{
		return false;
	}

	OutData.Bones.clear();
	OutData.InverseBindPoseMatrices.clear();

	return In.good()
		&& OutData.Vertices.size() == Header.VertexCount
		&& OutData.Indices.size() == Header.IndexCount
		&& OutData.Sections.size() == Header.SectionCount
		&& OutData.MaterialSlots.size() == Header.SlotCount;
}

bool FBinarySerializer::ReadAssetHeader(const FString& BinaryPath, FStaticMeshBinaryHeader& OutHeader) const
{
	std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath))), std::ios::binary);
	if (!In.is_open())
	{
		return false;
	}

	if (!ReadHeader(In, OutHeader))
	{
		return false;
	}

	if (!In.good())
	{
		return false;
	}

	if (!IsValidCompiledAssetHeaderBase(OutHeader))
	{
		return false;
	}

	return true;
}

bool FBinarySerializer::ReadStaticMeshHeader(const FString& BinaryPath, FStaticMeshBinaryHeader& OutHeader) const
{
	if (!ReadAssetHeader(BinaryPath, OutHeader))
	{
		return false;
	}

	return IsValidStaticMeshHeader(OutHeader);
}

bool FBinarySerializer::ReadSkeletonHeader(const FString& BinaryPath, FStaticMeshBinaryHeader& OutHeader) const
{
	if (!ReadAssetHeader(BinaryPath, OutHeader))
	{
		return false;
	}

	return IsValidSkeletonHeader(OutHeader);
}

bool FBinarySerializer::ReadSkeletalMeshHeader(const FString& BinaryPath, FStaticMeshBinaryHeader& OutHeader) const
{
	if (!ReadAssetHeader(BinaryPath, OutHeader))
	{
		return false;
	}

	return IsValidSkeletalMeshHeader(OutHeader);
}
