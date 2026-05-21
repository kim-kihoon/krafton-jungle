#include "Asset/SkeletonSerializer.h"

#include "Core/Paths.h"
#include "Math/Matrix.h"

#include <cstring>
#include <filesystem>

namespace
{
constexpr uint32 SKELETON_BINARY_MAGIC = 0x4C454B53; // 'SKEL'
constexpr uint32 SKELETON_BINARY_VERSION = 1;
constexpr uint32 MAX_SKELETON_BONE_COUNT = 65'536;
constexpr uint32 MAX_SKELETON_SOCKET_COUNT = 1024;
constexpr uint32 MAX_SKELETON_STRING_LENGTH = 16'384;

static bool IsValidSkeletonHeader(const FSkeletonBinaryHeader& Header)
{
    return Header.MagicNumber == SKELETON_BINARY_MAGIC
        && Header.Version == SKELETON_BINARY_VERSION
        && Header.BoneCount <= MAX_SKELETON_BONE_COUNT
        && Header.SocketCount <= MAX_SKELETON_SOCKET_COUNT
        && Header.RootNodeNameLength <= MAX_SKELETON_STRING_LENGTH;
}
}

void FSkeletonSerializer::WriteInt32LE(std::ofstream& Out, int32 Value)
{
    WriteUInt32LE(Out, static_cast<uint32>(Value));
}

void FSkeletonSerializer::WriteUInt32LE(std::ofstream& Out, uint32 Value)
{
    uint8 Bytes[4] = {
        static_cast<uint8>(Value & 0xFF),
        static_cast<uint8>((Value >> 8) & 0xFF),
        static_cast<uint8>((Value >> 16) & 0xFF),
        static_cast<uint8>((Value >> 24) & 0xFF),
    };
    Out.write(reinterpret_cast<const char*>(Bytes), sizeof(Bytes));
}

void FSkeletonSerializer::WriteFloatLE(std::ofstream& Out, float Value)
{
    uint32 Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(float));
    WriteUInt32LE(Out, Bits);
}

void FSkeletonSerializer::WriteString(std::ofstream& Out, const FString& Value)
{
    const uint32 Length = static_cast<uint32>(Value.size());
    WriteUInt32LE(Out, Length);
    if (Length > 0)
    {
        Out.write(Value.data(), Length);
    }
}

void FSkeletonSerializer::WriteMatrix4x4(std::ofstream& Out, const FMatrix& M)
{
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            WriteFloatLE(Out, M.M[Row][Col]);
        }
    }
}

void FSkeletonSerializer::WriteHeader(std::ofstream& Out, const FSkeletonBinaryHeader& Header)
{
    WriteUInt32LE(Out, Header.MagicNumber);
    WriteUInt32LE(Out, Header.Version);
    WriteUInt32LE(Out, Header.BoneCount);
    WriteUInt32LE(Out, Header.SocketCount);
    WriteUInt32LE(Out, Header.RootNodeNameLength);
    WriteString(Out, Header.RootNodeName);
}

void FSkeletonSerializer::WriteBones(std::ofstream& Out, const TArray<FBoneInfo>& Bones)
{
    WriteUInt32LE(Out, static_cast<uint32>(Bones.size()));

    for (const FBoneInfo& Bone : Bones)
    {
        WriteString(Out, Bone.Name);
        WriteInt32LE(Out, Bone.ParentIndex);
        WriteMatrix4x4(Out, Bone.LocalBindTransform);
        WriteMatrix4x4(Out, Bone.GlobalBindTransform);
        WriteMatrix4x4(Out, Bone.InverseBindPose);
    }
}

void FSkeletonSerializer::WriteSockets(std::ofstream& Out, const TArray<FSkeletalMeshSocket>& Sockets)
{
    WriteUInt32LE(Out, static_cast<uint32>(Sockets.size()));

    for (const FSkeletalMeshSocket& Socket : Sockets)
    {
        WriteString(Out, Socket.Name.ToString());
        WriteInt32LE(Out, Socket.BoneIndex);

        WriteFloatLE(Out, Socket.RelativeLocation.X);
        WriteFloatLE(Out, Socket.RelativeLocation.Y);
        WriteFloatLE(Out, Socket.RelativeLocation.Z);

        WriteFloatLE(Out, Socket.RelativeRotation.Pitch);
        WriteFloatLE(Out, Socket.RelativeRotation.Yaw);
        WriteFloatLE(Out, Socket.RelativeRotation.Roll);

        WriteFloatLE(Out, Socket.RelativeScale.X);
        WriteFloatLE(Out, Socket.RelativeScale.Y);
        WriteFloatLE(Out, Socket.RelativeScale.Z);
    }
}

bool FSkeletonSerializer::ReadInt32LE(std::ifstream& In, int32& OutValue) const
{
    uint32 U = 0;
    if (!ReadUInt32LE(In, U))
    {
        return false;
    }
    OutValue = static_cast<int32>(U);
    return true;
}

bool FSkeletonSerializer::ReadUInt32LE(std::ifstream& In, uint32& OutValue) const
{
    uint8 Bytes[4] = {};
    In.read(reinterpret_cast<char*>(Bytes), sizeof(Bytes));
    if (!In.good())
    {
        return false;
    }

    OutValue = static_cast<uint32>(Bytes[0])
        | (static_cast<uint32>(Bytes[1]) << 8)
        | (static_cast<uint32>(Bytes[2]) << 16)
        | (static_cast<uint32>(Bytes[3]) << 24);
    return true;
}

bool FSkeletonSerializer::ReadFloatLE(std::ifstream& In, float& OutValue) const
{
    uint32 Bits = 0;
    if (!ReadUInt32LE(In, Bits))
    {
        return false;
    }

    std::memcpy(&OutValue, &Bits, sizeof(float));
    return true;
}

bool FSkeletonSerializer::ReadString(std::ifstream& In, FString& OutValue) const
{
    uint32 Length = 0;
    if (!ReadUInt32LE(In, Length))
    {
        return false;
    }

    if (Length > MAX_SKELETON_STRING_LENGTH)
    {
        In.setstate(std::ios::failbit);
        return false;
    }

    OutValue.resize(Length);
    if (Length > 0)
    {
        In.read(OutValue.data(), Length);
    }

    return In.good();
}

bool FSkeletonSerializer::ReadMatrix4x4(std::ifstream& In, FMatrix& OutM) const
{
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            if (!ReadFloatLE(In, OutM.M[Row][Col]))
            {
                return false;
            }
        }
    }

    return true;
}

bool FSkeletonSerializer::ReadHeader(std::ifstream& In, FSkeletonBinaryHeader& OutHeader) const
{
    if (!ReadUInt32LE(In, OutHeader.MagicNumber)
        || !ReadUInt32LE(In, OutHeader.Version)
        || !ReadUInt32LE(In, OutHeader.BoneCount)
        || !ReadUInt32LE(In, OutHeader.SocketCount)
        || !ReadUInt32LE(In, OutHeader.RootNodeNameLength))
    {
        return false;
    }

    if (!IsValidSkeletonHeader(OutHeader))
    {
        In.setstate(std::ios::failbit);
        return false;
    }

    if (!ReadString(In, OutHeader.RootNodeName))
    {
        return false;
    }

    if (OutHeader.RootNodeName.size() != OutHeader.RootNodeNameLength)
    {
        In.setstate(std::ios::failbit);
        return false;
    }

    return In.good();
}

bool FSkeletonSerializer::ReadBones(std::ifstream& In, TArray<FBoneInfo>& OutBones, uint32 BoneCount) const
{
    uint32 Count = 0;
    if (!ReadUInt32LE(In, Count))
    {
        return false;
    }

    if (Count != BoneCount || Count > MAX_SKELETON_BONE_COUNT)
    {
        In.setstate(std::ios::failbit);
        return false;
    }

    OutBones.resize(Count);
    for (FBoneInfo& Bone : OutBones)
    {
        if (!ReadString(In, Bone.Name)
            || !ReadInt32LE(In, Bone.ParentIndex)
            || !ReadMatrix4x4(In, Bone.LocalBindTransform)
            || !ReadMatrix4x4(In, Bone.GlobalBindTransform)
            || !ReadMatrix4x4(In, Bone.InverseBindPose))
        {
            return false;
        }
    }

    return In.good();
}

bool FSkeletonSerializer::ReadSockets(std::ifstream& In, TArray<FSkeletalMeshSocket>& OutSockets, uint32 SocketCount) const
{
    uint32 Count = 0;
    if (!ReadUInt32LE(In, Count))
    {
        return false;
    }

    if (Count != SocketCount || Count > MAX_SKELETON_SOCKET_COUNT)
    {
        In.setstate(std::ios::failbit);
        return false;
    }

    OutSockets.resize(Count);
    for (FSkeletalMeshSocket& Socket : OutSockets)
    {
        FString SocketName;
        if (!ReadString(In, SocketName))
        {
            return false;
        }

        Socket.Name = FName(SocketName);

        if (!ReadInt32LE(In, Socket.BoneIndex)
            || !ReadFloatLE(In, Socket.RelativeLocation.X)
            || !ReadFloatLE(In, Socket.RelativeLocation.Y)
            || !ReadFloatLE(In, Socket.RelativeLocation.Z)
            || !ReadFloatLE(In, Socket.RelativeRotation.Pitch)
            || !ReadFloatLE(In, Socket.RelativeRotation.Yaw)
            || !ReadFloatLE(In, Socket.RelativeRotation.Roll)
            || !ReadFloatLE(In, Socket.RelativeScale.X)
            || !ReadFloatLE(In, Socket.RelativeScale.Y)
            || !ReadFloatLE(In, Socket.RelativeScale.Z))
        {
            return false;
        }
    }

    return In.good();
}

bool FSkeletonSerializer::SaveSkeleton(const FString& BinaryPath, const FSkeleton& Data)
{
    std::ofstream Out(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath))), std::ios::binary);
    if (!Out.is_open())
    {
        return false;
    }

    FSkeletonBinaryHeader Header;
    Header.MagicNumber = SKELETON_BINARY_MAGIC;
    Header.Version = SKELETON_BINARY_VERSION;
    Header.BoneCount = static_cast<uint32>(Data.Bones.size());
    Header.SocketCount = static_cast<uint32>(Data.Sockets.size());
    Header.RootNodeNameLength = static_cast<uint32>(Data.RootNodeName.size());
    Header.RootNodeName = Data.RootNodeName;

    if (!IsValidSkeletonHeader(Header))
    {
        return false;
    }

    WriteHeader(Out, Header);
    WriteString(Out, Data.PathFileName);
    WriteBones(Out, Data.Bones);
    WriteSockets(Out, Data.Sockets);

    return Out.good();
}

bool FSkeletonSerializer::LoadSkeleton(const FString& BinaryPath, FSkeleton& OutData)
{
    std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath))), std::ios::binary);
    if (!In.is_open())
    {
        return false;
    }

    FSkeletonBinaryHeader Header;
    if (!ReadHeader(In, Header))
    {
        return false;
    }

    OutData.RootNodeName = Header.RootNodeName;

    if (!ReadString(In, OutData.PathFileName)
        || !ReadBones(In, OutData.Bones, Header.BoneCount)
        || !ReadSockets(In, OutData.Sockets, Header.SocketCount))
    {
        return false;
    }

    return In.good()
        && OutData.Bones.size() == Header.BoneCount
        && OutData.Sockets.size() == Header.SocketCount;
}

bool FSkeletonSerializer::ReadSkeletonHeader(const FString& BinaryPath, FSkeletonBinaryHeader& OutHeader) const
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

    return In.good();
}
