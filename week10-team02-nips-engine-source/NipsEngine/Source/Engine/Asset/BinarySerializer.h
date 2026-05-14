#pragma once

#include "Core/CoreMinimal.h"

#include <fstream>

struct FStaticMesh;
struct FSkeletalMesh;
struct FSkeleton;

enum class ECompiledAssetType : uint32
{
	Unknown = 0,
	StaticMesh = 1,
	SkeletalMesh = 2,
	Skeleton = 3,
};

/*
 *	[주의사항]
 *	- Header나 Body 정보가 변경되면 반드시 Version을 바꿔야 합니다.
 */
struct FStaticMeshBinaryHeader
{
	uint32 Magic = 0;
	uint32 HeaderVersion = 1;
	ECompiledAssetType AssetType = ECompiledAssetType::StaticMesh;
	uint32 PayloadVersion = 1;
	uint32 Flags = 0;

	uint32 VertexCount = 0;
	uint32 IndexCount = 0;
	uint32 SectionCount = 0;
	uint32 SlotCount = 0;
	
	uint64 SourceFileWriteTime = 0;
	uint64 SourceFileHash = 0;
};

class FBinarySerializer
{
public:
	bool SaveStaticMesh(const FString& BinaryPath, const FString& SourcePath, const FStaticMesh& Data);
	bool LoadStaticMesh(const FString& BinaryPath, FStaticMesh& OutData);
	bool SaveSkeleton(const FString& BinaryPath, const FString& SourcePath, const FSkeleton& Data);
	bool LoadSkeleton(const FString& BinaryPath, FSkeleton& OutData);
	bool SaveSkeletalMesh(const FString& BinaryPath, const FString& SourcePath, const FSkeletalMesh& Data);
	bool LoadSkeletalMesh(const FString& BinaryPath, FSkeletalMesh& OutData);
	
	//	Header Read + 검사 장치
	bool ReadAssetHeader(const FString& BinaryPath, FStaticMeshBinaryHeader& OutHeader) const;
	bool ReadStaticMeshHeader(const FString& BinaryPath, FStaticMeshBinaryHeader& OutHeader) const;
	bool ReadSkeletonHeader(const FString& BinaryPath, FStaticMeshBinaryHeader& OutHeader) const;
	bool ReadSkeletalMeshHeader(const FString& BinaryPath, FStaticMeshBinaryHeader& OutHeader) const;

private:
	
	void WriteInt32LE(std::ofstream& Out, int32 Value);
	void WriteUInt32LE(std::ofstream& Out, uint32 Value);
	void WriteUInt64LE(std::ofstream& Out, uint64 Value);
	void WriteFloatLE(std::ofstream& Out, float Value);
	
	bool ReadInt32LE(std::ifstream& In, int32& OutValue) const;
	bool ReadUInt32LE(std::ifstream& In, uint32& OutValue) const;
	bool ReadUInt64LE(std::ifstream& In, uint64& OutValue) const;
	bool ReadFloatLE(std::ifstream& In, float& OutValue) const;

	void WriteMatrix(std::ofstream& Out, const FMatrix& Matrix);
	bool ReadMatrix(std::ifstream& In, FMatrix& OutMatrix) const;
	void WriteTransform(std::ofstream& Out, const FTransform& Transform);
	bool ReadTransform(std::ifstream& In, FTransform& OutTransform) const;
	
	void WriteHeader(std::ofstream& Out, const FStaticMeshBinaryHeader& Header);
	bool ReadHeader(std::ifstream& In, FStaticMeshBinaryHeader& OutHeader) const;

	void WriteString(std::ofstream& Out, const FString& String);
	bool ReadString(std::ifstream& In, FString& OutString) const;

	void WriteIndexArray(std::ofstream& Out, const TArray<uint32>& Array);
	bool ReadIndexArray(std::ifstream& In, TArray<uint32>& OutArray) const;
	
	void WriteVertices(std::ofstream& Out, const FStaticMesh& Data);
	bool ReadVertices(std::ifstream& In, FStaticMesh& OutData, uint32 VertexCount) const;

	void WriteSections(std::ofstream& Out, const FStaticMesh& Data);
	bool ReadSections(std::ifstream& In, FStaticMesh& OutData, uint32 SectionCount) const;

	void WriteBounds(std::ofstream& Out, const FStaticMesh& Data);
	bool ReadBounds(std::ifstream& In, FStaticMesh& OutData) const;
};
