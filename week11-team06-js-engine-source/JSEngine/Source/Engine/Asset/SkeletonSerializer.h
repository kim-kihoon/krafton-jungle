#pragma once

#include "Asset/SkeletonTypes.h"
#include "Core/CoreMinimal.h"

#include <fstream>

struct FSkeletonBinaryHeader
{
    uint32 MagicNumber = 0x4C454B53; // 'SKEL'
    uint32 Version = 1;
    uint32 BoneCount = 0;
    uint32 SocketCount = 0;
    uint32 RootNodeNameLength = 0;

    FString RootNodeName;
};

class FSkeletonSerializer
{
public:
    bool SaveSkeleton(const FString& BinaryPath, const FSkeleton& Data);
    bool LoadSkeleton(const FString& BinaryPath, FSkeleton& OutData);
    bool ReadSkeletonHeader(const FString& BinaryPath, FSkeletonBinaryHeader& OutHeader) const;

private:
    void WriteInt32LE(std::ofstream& Out, int32 Value);
    void WriteUInt32LE(std::ofstream& Out, uint32 Value);
    void WriteFloatLE(std::ofstream& Out, float Value);
    void WriteString(std::ofstream& Out, const FString& Value);
    void WriteMatrix4x4(std::ofstream& Out, const FMatrix& M);
    void WriteHeader(std::ofstream& Out, const FSkeletonBinaryHeader& Header);
    void WriteBones(std::ofstream& Out, const TArray<FBoneInfo>& Bones);
    void WriteSockets(std::ofstream& Out, const TArray<FSkeletalMeshSocket>& Sockets);

    bool ReadInt32LE(std::ifstream& In, int32& OutValue) const;
    bool ReadUInt32LE(std::ifstream& In, uint32& OutValue) const;
    bool ReadFloatLE(std::ifstream& In, float& OutValue) const;
    bool ReadString(std::ifstream& In, FString& OutValue) const;
    bool ReadMatrix4x4(std::ifstream& In, FMatrix& OutM) const;
    bool ReadHeader(std::ifstream& In, FSkeletonBinaryHeader& OutHeader) const;
    bool ReadBones(std::ifstream& In, TArray<FBoneInfo>& OutBones, uint32 BoneCount) const;
    bool ReadSockets(std::ifstream& In, TArray<FSkeletalMeshSocket>& OutSockets, uint32 SocketCount) const;
};
