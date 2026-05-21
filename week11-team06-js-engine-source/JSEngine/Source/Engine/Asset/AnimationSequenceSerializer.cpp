#include "Asset/AnimationSequenceSerializer.h"

#include "Asset/AnimationSequence.h"
#include "Core/Paths.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace
{
constexpr uint32 ANIMATION_CLIP_BINARY_MAGIC = 0x4D494E41; // 'ANIM'
constexpr uint32 ANIMATION_CLIP_BINARY_VERSION = 5;
constexpr uint32 MAX_ANIMATION_BONE_TRACK_COUNT = 65'536;
constexpr uint32 MAX_ANIMATION_SHAPE_KEY_TRACK_COUNT = 1'000'000;
constexpr uint32 MAX_ANIMATION_CURVE_COUNT = 2'000'000;
constexpr uint32 MAX_ANIMATION_KEY_COUNT = 20'000'000;
constexpr uint32 MAX_ANIMATION_STRING_LENGTH = 16'384;
constexpr uint32 ANIMATION_SEQUENCE_ASSET_MAGIC = 0x494E414A; // 'JANI'
constexpr uint32 ANIMATION_SEQUENCE_ASSET_VERSION = 1;
constexpr uint32 MAX_ANIMATION_NOTIFY_COUNT = 1'000'000;

static uint64 GetFileWriteTimeTicks(const FString& Path)
{
    const FString NormalizedPath = FPaths::Normalize(Path);
    const std::filesystem::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(NormalizedPath)));
    if (!std::filesystem::exists(FilePath))
    {
        return 0;
    }

    const auto WriteTime = std::filesystem::last_write_time(FilePath);
    const auto Duration = WriteTime.time_since_epoch();
    return static_cast<uint64>(std::chrono::duration_cast<std::chrono::seconds>(Duration).count());
}

static void CountSequenceData(const FAnimationSequence& Sequence, uint32& OutCurveCount, uint32& OutKeyCount)
{
    OutCurveCount = 0;
    OutKeyCount = 0;

    auto CountCurve = [&OutCurveCount, &OutKeyCount](const FAnimationFloatCurve& Curve)
    {
        ++OutCurveCount;
        OutKeyCount += static_cast<uint32>(Curve.Keys.size());
    };

    for (const FBoneAnimationTrack& Track : Sequence.BoneTracks)
    {
        OutKeyCount += static_cast<uint32>(
            Track.InternalTrack.PosKeys.size() +
            Track.InternalTrack.RotKeys.size() +
            Track.InternalTrack.ScaleKeys.size());

    }

    for (const FShapeKeyAnimationTrack& Track : Sequence.ShapeKeyTracks)
    {
        CountCurve(Track.WeightCurve);
    }
}

static bool IsValidHeader(const FAnimationSequenceBinaryHeader& Header)
{
    return Header.MagicNumber == ANIMATION_CLIP_BINARY_MAGIC
        && Header.Version == ANIMATION_CLIP_BINARY_VERSION
        && Header.BoneTrackCount <= MAX_ANIMATION_BONE_TRACK_COUNT
        && Header.ShapeKeyTrackCount <= MAX_ANIMATION_SHAPE_KEY_TRACK_COUNT
        && Header.CurveCount <= MAX_ANIMATION_CURVE_COUNT
        && Header.KeyCount <= MAX_ANIMATION_KEY_COUNT;
}
}

void FAnimationSequenceSerializer::WriteBool(std::ofstream& Out, bool Value)
{
    const uint8 Byte = Value ? 1 : 0;
    Out.write(reinterpret_cast<const char*>(&Byte), sizeof(Byte));
}

void FAnimationSequenceSerializer::WriteInt32LE(std::ofstream& Out, int32 Value)
{
    uint32 U = static_cast<uint32>(Value);
    WriteUInt32LE(Out, U);
}

void FAnimationSequenceSerializer::WriteUInt32LE(std::ofstream& Out, uint32 Value)
{
    uint8 Bytes[4] = {
        static_cast<uint8>(Value & 0xFF),
        static_cast<uint8>((Value >> 8) & 0xFF),
        static_cast<uint8>((Value >> 16) & 0xFF),
        static_cast<uint8>((Value >> 24) & 0xFF),
    };
    Out.write(reinterpret_cast<const char*>(Bytes), sizeof(Bytes));
}

void FAnimationSequenceSerializer::WriteUInt64LE(std::ofstream& Out, uint64 Value)
{
    for (int32 i = 0; i < 8; ++i)
    {
        const uint8 Byte = static_cast<uint8>((Value >> (i * 8)) & 0xFF);
        Out.write(reinterpret_cast<const char*>(&Byte), sizeof(Byte));
    }
}

void FAnimationSequenceSerializer::WriteFloatLE(std::ofstream& Out, float Value)
{
    uint32 Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(float));
    WriteUInt32LE(Out, Bits);
}

void FAnimationSequenceSerializer::WriteString(std::ofstream& Out, const FString& Value)
{
    const uint32 Length = static_cast<uint32>(Value.size());
    WriteUInt32LE(Out, Length);
    if (Length > 0)
    {
        Out.write(Value.data(), Length);
    }
}

void FAnimationSequenceSerializer::WriteVector(std::ofstream& Out, const FVector& Value)
{
    WriteFloatLE(Out, Value.X);
    WriteFloatLE(Out, Value.Y);
    WriteFloatLE(Out, Value.Z);
}

void FAnimationSequenceSerializer::WriteQuat(std::ofstream& Out, const FQuat& Value)
{
    WriteFloatLE(Out, Value.X);
    WriteFloatLE(Out, Value.Y);
    WriteFloatLE(Out, Value.Z);
    WriteFloatLE(Out, Value.W);
}

void FAnimationSequenceSerializer::WriteHeader(std::ofstream& Out, const FAnimationSequenceBinaryHeader& Header)
{
    WriteUInt32LE(Out, Header.MagicNumber);
    WriteUInt32LE(Out, Header.Version);
    WriteUInt32LE(Out, Header.BoneTrackCount);
    WriteUInt32LE(Out, Header.ShapeKeyTrackCount);
    WriteUInt32LE(Out, Header.CurveCount);
    WriteUInt32LE(Out, Header.KeyCount);
    WriteUInt64LE(Out, Header.SourceFileWriteTime);
}

void FAnimationSequenceSerializer::WriteCurveKey(std::ofstream& Out, const FAnimationCurveKey& Key)
{
    WriteFloatLE(Out, Key.TimeSeconds);
    WriteFloatLE(Out, Key.Value);
    WriteInt32LE(Out, static_cast<int32>(Key.InterpMode));
    WriteInt32LE(Out, static_cast<int32>(Key.TangentMode));
    WriteFloatLE(Out, Key.ArriveTangent);
    WriteFloatLE(Out, Key.LeaveTangent);
    WriteFloatLE(Out, Key.ArriveTangentWeight);
    WriteFloatLE(Out, Key.LeaveTangentWeight);
    WriteFloatLE(Out, Key.ArriveTangentVelocity);
    WriteFloatLE(Out, Key.LeaveTangentVelocity);
    WriteBool(Out, Key.bArriveWeighted);
    WriteBool(Out, Key.bLeaveWeighted);
    WriteBool(Out, Key.bArriveHasVelocity);
    WriteBool(Out, Key.bLeaveHasVelocity);
    WriteInt32LE(Out, Key.RawInterpolation);
    WriteInt32LE(Out, Key.RawConstantMode);
    WriteInt32LE(Out, Key.RawTangentMode);
}

void FAnimationSequenceSerializer::WriteFloatCurve(std::ofstream& Out, const FAnimationFloatCurve& Curve)
{
    WriteInt32LE(Out, static_cast<int32>(Curve.Channel));
    WriteFloatLE(Out, Curve.DefaultValue);
    WriteBool(Out, Curve.bHasCurve);
    WriteUInt32LE(Out, static_cast<uint32>(Curve.Keys.size()));
    for (const FAnimationCurveKey& Key : Curve.Keys)
    {
        WriteCurveKey(Out, Key);
    }
}

void FAnimationSequenceSerializer::WriteRawAnimSequenceTrack(std::ofstream& Out, const FRawAnimSequenceTrack& Track)
{
    WriteUInt32LE(Out, static_cast<uint32>(Track.PosKeys.size()));
    for (const FVector& Key : Track.PosKeys)
    {
        WriteVector(Out, Key);
    }

    WriteUInt32LE(Out, static_cast<uint32>(Track.RotKeys.size()));
    for (const FQuat& Key : Track.RotKeys)
    {
        WriteQuat(Out, Key);
    }

    WriteUInt32LE(Out, static_cast<uint32>(Track.ScaleKeys.size()));
    for (const FVector& Key : Track.ScaleKeys)
    {
        WriteVector(Out, Key);
    }
}

void FAnimationSequenceSerializer::WriteBoneTrack(std::ofstream& Out, const FBoneAnimationTrack& Track)
{
    WriteString(Out, Track.BoneName);
    WriteRawAnimSequenceTrack(Out, Track.InternalTrack);
    WriteVector(Out, Track.DefaultTranslation);
    WriteVector(Out, Track.DefaultRotationEuler);
    WriteVector(Out, Track.DefaultScale);
    WriteInt32LE(Out, static_cast<int32>(Track.RotationOrder));
    WriteInt32LE(Out, Track.TransformInheritType);
    WriteBool(Out, Track.bRotationActive);
    WriteVector(Out, Track.PreRotation);
    WriteVector(Out, Track.PostRotation);
    WriteVector(Out, Track.RotationOffset);
    WriteVector(Out, Track.RotationPivot);
    WriteVector(Out, Track.ScalingOffset);
    WriteVector(Out, Track.ScalingPivot);

}

void FAnimationSequenceSerializer::WriteShapeKeyTrack(std::ofstream& Out, const FShapeKeyAnimationTrack& Track)
{
    WriteString(Out, Track.MeshNodeName);
    WriteString(Out, Track.ShapeKeyName);
    WriteInt32LE(Out, Track.ShapeKeyIndex);
    WriteFloatCurve(Out, Track.WeightCurve);
}

bool FAnimationSequenceSerializer::ReadBool(std::ifstream& In, bool& OutValue) const
{
    uint8 Byte = 0;
    In.read(reinterpret_cast<char*>(&Byte), sizeof(Byte));
    if (!In.good())
    {
        return false;
    }
    OutValue = Byte != 0;
    return true;
}

bool FAnimationSequenceSerializer::ReadInt32LE(std::ifstream& In, int32& OutValue) const
{
    uint32 U = 0;
    if (!ReadUInt32LE(In, U))
    {
        return false;
    }
    OutValue = static_cast<int32>(U);
    return true;
}

bool FAnimationSequenceSerializer::ReadUInt32LE(std::ifstream& In, uint32& OutValue) const
{
    uint8 Bytes[4] = {};
    In.read(reinterpret_cast<char*>(Bytes), sizeof(Bytes));
    if (!In.good())
    {
        return false;
    }

    OutValue =
        static_cast<uint32>(Bytes[0]) |
        (static_cast<uint32>(Bytes[1]) << 8) |
        (static_cast<uint32>(Bytes[2]) << 16) |
        (static_cast<uint32>(Bytes[3]) << 24);
    return true;
}

bool FAnimationSequenceSerializer::ReadUInt64LE(std::ifstream& In, uint64& OutValue) const
{
    OutValue = 0;
    for (int32 i = 0; i < 8; ++i)
    {
        uint8 Byte = 0;
        In.read(reinterpret_cast<char*>(&Byte), sizeof(Byte));
        if (!In.good())
        {
            return false;
        }
        OutValue |= static_cast<uint64>(Byte) << (i * 8);
    }
    return true;
}

bool FAnimationSequenceSerializer::ReadFloatLE(std::ifstream& In, float& OutValue) const
{
    uint32 Bits = 0;
    if (!ReadUInt32LE(In, Bits))
    {
        return false;
    }
    std::memcpy(&OutValue, &Bits, sizeof(float));
    return true;
}

bool FAnimationSequenceSerializer::ReadString(std::ifstream& In, FString& OutValue) const
{
    uint32 Length = 0;
    if (!ReadUInt32LE(In, Length) || Length > MAX_ANIMATION_STRING_LENGTH)
    {
        return false;
    }

    OutValue.resize(Length);
    if (Length > 0)
    {
        In.read(OutValue.data(), Length);
        if (!In.good())
        {
            return false;
        }
    }

    return true;
}

bool FAnimationSequenceSerializer::ReadVector(std::ifstream& In, FVector& OutValue) const
{
    return ReadFloatLE(In, OutValue.X)
        && ReadFloatLE(In, OutValue.Y)
        && ReadFloatLE(In, OutValue.Z);
}

bool FAnimationSequenceSerializer::ReadQuat(std::ifstream& In, FQuat& OutValue) const
{
    return ReadFloatLE(In, OutValue.X)
        && ReadFloatLE(In, OutValue.Y)
        && ReadFloatLE(In, OutValue.Z)
        && ReadFloatLE(In, OutValue.W);
}

bool FAnimationSequenceSerializer::ReadHeader(std::ifstream& In, FAnimationSequenceBinaryHeader& OutHeader) const
{
    return ReadUInt32LE(In, OutHeader.MagicNumber)
        && ReadUInt32LE(In, OutHeader.Version)
        && ReadUInt32LE(In, OutHeader.BoneTrackCount)
        && ReadUInt32LE(In, OutHeader.ShapeKeyTrackCount)
        && ReadUInt32LE(In, OutHeader.CurveCount)
        && ReadUInt32LE(In, OutHeader.KeyCount)
        && ReadUInt64LE(In, OutHeader.SourceFileWriteTime);
}

bool FAnimationSequenceSerializer::ReadCurveKey(std::ifstream& In, FAnimationCurveKey& OutKey) const
{
    int32 InterpMode = 0;
    int32 TangentMode = 0;
    if (!ReadFloatLE(In, OutKey.TimeSeconds) ||
        !ReadFloatLE(In, OutKey.Value) ||
        !ReadInt32LE(In, InterpMode) ||
        !ReadInt32LE(In, TangentMode) ||
        !ReadFloatLE(In, OutKey.ArriveTangent) ||
        !ReadFloatLE(In, OutKey.LeaveTangent) ||
        !ReadFloatLE(In, OutKey.ArriveTangentWeight) ||
        !ReadFloatLE(In, OutKey.LeaveTangentWeight) ||
        !ReadFloatLE(In, OutKey.ArriveTangentVelocity) ||
        !ReadFloatLE(In, OutKey.LeaveTangentVelocity) ||
        !ReadBool(In, OutKey.bArriveWeighted) ||
        !ReadBool(In, OutKey.bLeaveWeighted) ||
        !ReadBool(In, OutKey.bArriveHasVelocity) ||
        !ReadBool(In, OutKey.bLeaveHasVelocity) ||
        !ReadInt32LE(In, OutKey.RawInterpolation) ||
        !ReadInt32LE(In, OutKey.RawConstantMode) ||
        !ReadInt32LE(In, OutKey.RawTangentMode))
    {
        return false;
    }

    OutKey.InterpMode = static_cast<ECurveInterpMode>(InterpMode);
    OutKey.TangentMode = static_cast<ECurveTangentMode>(TangentMode);
    return true;
}

bool FAnimationSequenceSerializer::ReadFloatCurve(std::ifstream& In, FAnimationFloatCurve& OutCurve) const
{
    int32 Channel = 0;
    uint32 KeyCount = 0;
    if (!ReadInt32LE(In, Channel) ||
        !ReadFloatLE(In, OutCurve.DefaultValue) ||
        !ReadBool(In, OutCurve.bHasCurve) ||
        !ReadUInt32LE(In, KeyCount) ||
        KeyCount > MAX_ANIMATION_KEY_COUNT)
    {
        return false;
    }

    OutCurve.Channel = static_cast<EAnimationCurveChannel>(Channel);
    OutCurve.Keys.resize(KeyCount);
    for (FAnimationCurveKey& Key : OutCurve.Keys)
    {
        if (!ReadCurveKey(In, Key))
        {
            return false;
        }
    }
    return true;
}

bool FAnimationSequenceSerializer::ReadRawAnimSequenceTrack(std::ifstream& In, FRawAnimSequenceTrack& OutTrack) const
{
    uint32 PosKeyCount = 0;
    if (!ReadUInt32LE(In, PosKeyCount) || PosKeyCount > MAX_ANIMATION_KEY_COUNT)
    {
        return false;
    }
    OutTrack.PosKeys.resize(PosKeyCount);
    for (FVector& Key : OutTrack.PosKeys)
    {
        if (!ReadVector(In, Key))
        {
            return false;
        }
    }

    uint32 RotKeyCount = 0;
    if (!ReadUInt32LE(In, RotKeyCount) || RotKeyCount > MAX_ANIMATION_KEY_COUNT)
    {
        return false;
    }
    OutTrack.RotKeys.resize(RotKeyCount);
    for (FQuat& Key : OutTrack.RotKeys)
    {
        if (!ReadQuat(In, Key))
        {
            return false;
        }
    }

    uint32 ScaleKeyCount = 0;
    if (!ReadUInt32LE(In, ScaleKeyCount) || ScaleKeyCount > MAX_ANIMATION_KEY_COUNT)
    {
        return false;
    }
    OutTrack.ScaleKeys.resize(ScaleKeyCount);
    for (FVector& Key : OutTrack.ScaleKeys)
    {
        if (!ReadVector(In, Key))
        {
            return false;
        }
    }

    return true;
}

bool FAnimationSequenceSerializer::ReadBoneTrack(std::ifstream& In, FBoneAnimationTrack& OutTrack) const
{
    int32 RotationOrder = 0;
    if (!ReadString(In, OutTrack.BoneName) ||
        !ReadRawAnimSequenceTrack(In, OutTrack.InternalTrack) ||
        !ReadVector(In, OutTrack.DefaultTranslation) ||
        !ReadVector(In, OutTrack.DefaultRotationEuler) ||
        !ReadVector(In, OutTrack.DefaultScale) ||
        !ReadInt32LE(In, RotationOrder) ||
        !ReadInt32LE(In, OutTrack.TransformInheritType) ||
        !ReadBool(In, OutTrack.bRotationActive) ||
        !ReadVector(In, OutTrack.PreRotation) ||
        !ReadVector(In, OutTrack.PostRotation) ||
        !ReadVector(In, OutTrack.RotationOffset) ||
        !ReadVector(In, OutTrack.RotationPivot) ||
        !ReadVector(In, OutTrack.ScalingOffset) ||
        !ReadVector(In, OutTrack.ScalingPivot))
    {
        return false;
    }

    OutTrack.RotationOrder = static_cast<EAnimationRotationOrder>(RotationOrder);

    return true;
}

bool FAnimationSequenceSerializer::ReadShapeKeyTrack(std::ifstream& In, FShapeKeyAnimationTrack& OutTrack) const
{
    return ReadString(In, OutTrack.MeshNodeName)
        && ReadString(In, OutTrack.ShapeKeyName)
        && ReadInt32LE(In, OutTrack.ShapeKeyIndex)
        && ReadFloatCurve(In, OutTrack.WeightCurve);
}

bool FAnimationSequenceSerializer::SaveAnimationSequence(const FString& BinaryPath, const FString& SourcePath, const FAnimationSequence& Data)
{
    std::ofstream Out(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath))), std::ios::binary);
    if (!Out.is_open())
    {
        return false;
    }

    return WriteAnimationSequence(Out, SourcePath, Data);
}
bool FAnimationSequenceSerializer::LoadAnimationSequence(const FString& FbxOrBinaryPath, FAnimationSequence& OutData)
{
    std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(FbxOrBinaryPath))), std::ios::binary);
    if (!In.is_open())
    {
        return false;
    }
    return ReadAnimationSequence(In, OutData);
}

bool FAnimationSequenceSerializer::ReadAnimationSequenceHeader(const FString& BinaryPath, FAnimationSequenceBinaryHeader& OutHeader) const
{
    std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(BinaryPath))), std::ios::binary);
    if (!In.is_open())
    {
        return false;
    }

    return ReadHeader(In, OutHeader) && IsValidHeader(OutHeader);
}

bool FAnimationSequenceSerializer::SaveAnimationSequenceAsset(const FString& AssetPath, const UAnimationSequence& Sequence)
{
    const FAnimationSequence* Data = Sequence.GetSequenceData();
    if (!Data)
    {
        return false;
    }
    const std::filesystem::path AbsolutePath = FPaths::ToAbsolute(FPaths::ToWide(AssetPath));
    std::filesystem::create_directories(AbsolutePath.parent_path());

    std::ofstream Out(AbsolutePath, std::ios::binary);
    if (!Out.is_open())
    {
        return false;
    }

    const FString SourceImportPath = !Sequence.GetSourceImportPath().empty()
        ? Sequence.GetSourceImportPath()
        : Data->SourcePath;

    // .anim is the authored asset. The track payload uses the existing
    // animation serializer, so .bin cache data and .anim asset data stay aligned.
    WriteUInt32LE(Out, ANIMATION_SEQUENCE_ASSET_MAGIC);
    WriteUInt32LE(Out, ANIMATION_SEQUENCE_ASSET_VERSION);
    WriteString(Out, SourceImportPath);

    if (!WriteAnimationSequence(Out, SourceImportPath, *Data))
    {
        return false;
    }

    const TArray<FAnimNotifyEvent>& Notifies = Sequence.GetNotifies();
    WriteUInt32LE(Out, static_cast<uint32>(Notifies.size()));
    for (const FAnimNotifyEvent& Notify : Notifies)
    {
        WriteFloatLE(Out, Notify.TriggerTime);
        WriteString(Out, Notify.NotifyName.ToString());
    }

    return Out.good();
}

bool FAnimationSequenceSerializer::LoadAnimationSequenceAsset(const FString& AssetPath, UAnimationSequence& OutSequence)
{
    std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(AssetPath))), std::ios::binary);
    if (!In.is_open())
    {
        return false;
    }

    uint32 Magic = 0;
    uint32 Version = 0;
    if (!ReadUInt32LE(In, Magic) || Magic != ANIMATION_SEQUENCE_ASSET_MAGIC)
    {
        return false;
    }

    if (!ReadUInt32LE(In, Version) || Version != ANIMATION_SEQUENCE_ASSET_VERSION)
    {
        return false;
    }

    FString SourceImportPath;
    if (!ReadString(In, SourceImportPath))
    {
        return false;
    }

    FAnimationSequence* Data = new FAnimationSequence();
    if (!ReadAnimationSequence(In, *Data))
    {
        delete Data;
        return false;
    }

    uint32 NotifyCount = 0;
    if (!ReadUInt32LE(In, NotifyCount) || NotifyCount > MAX_ANIMATION_NOTIFY_COUNT)
    {
        delete Data;
        return false;
    }

    TArray<FAnimNotifyEvent> LoadedNotifies;
    LoadedNotifies.reserve(NotifyCount);
    for (uint32 NotifyIndex = 0; NotifyIndex < NotifyCount; ++NotifyIndex)
    {
        FAnimNotifyEvent Notify;
        FString NotifyName;
        if (!ReadFloatLE(In, Notify.TriggerTime)
            || !ReadString(In, NotifyName))
        {
            delete Data;
            return false;
        }

        Notify.NotifyName = FName(NotifyName.c_str());
        LoadedNotifies.push_back(Notify);
    }

    if (!In.good())
    {
        delete Data;
        return false;
    }

    OutSequence.SetAssetPath(AssetPath);
    OutSequence.SetSourceImportPath(SourceImportPath);
    OutSequence.SetSequenceData(Data);
    OutSequence.ClearNotifies();
    for (const FAnimNotifyEvent& Notify : LoadedNotifies)
    {
        OutSequence.AddNotify(Notify);
    }

    return true;
}

bool FAnimationSequenceSerializer::WriteAnimationSequence(std::ofstream& Out, const FString& SourcePath, const FAnimationSequence& Data)
{
    uint32 CurveCount = 0;
    uint32 KeyCount = 0;
    CountSequenceData(Data, CurveCount, KeyCount);

    FAnimationSequenceBinaryHeader Header;
    Header.MagicNumber = ANIMATION_CLIP_BINARY_MAGIC;
    Header.Version = ANIMATION_CLIP_BINARY_VERSION;
    Header.BoneTrackCount = static_cast<uint32>(Data.BoneTracks.size());
    Header.ShapeKeyTrackCount = static_cast<uint32>(Data.ShapeKeyTracks.size());
    Header.CurveCount = CurveCount;
    Header.KeyCount = KeyCount;
    Header.SourceFileWriteTime = GetFileWriteTimeTicks(SourcePath);

    if (!IsValidHeader(Header))
    {
        return false;
    }

    WriteHeader(Out, Header);
    WriteString(Out, Data.Name);
    WriteString(Out, Data.SourcePath);
    WriteString(Out, Data.SkeletonSourcePath);
    WriteFloatLE(Out, Data.StartTimeSeconds);
    WriteFloatLE(Out, Data.EndTimeSeconds);
    WriteFloatLE(Out, Data.DurationSeconds);
    WriteFloatLE(Out, Data.SourceFrameRate);
    WriteInt32LE(Out, Data.NumberOfFrames);
    WriteInt32LE(Out, Data.NumberOfKeys);

    WriteUInt32LE(Out, Header.BoneTrackCount);
    for (const FBoneAnimationTrack& Track : Data.BoneTracks)
    {
        WriteBoneTrack(Out, Track);
    }

    WriteUInt32LE(Out, Header.ShapeKeyTrackCount);
    for (const FShapeKeyAnimationTrack& Track : Data.ShapeKeyTracks)
    {
        WriteShapeKeyTrack(Out, Track);
    }    
    return Out.good();
}

bool FAnimationSequenceSerializer::ReadAnimationSequence(std::ifstream& In, FAnimationSequence& OutData)
{
    FAnimationSequenceBinaryHeader Header;
    if (!ReadHeader(In, Header) || !IsValidHeader(Header))
    {
        return false;
    }

    if (!ReadString(In, OutData.Name) ||
        !ReadString(In, OutData.SourcePath) ||
        !ReadString(In, OutData.SkeletonSourcePath) ||
        !ReadFloatLE(In, OutData.StartTimeSeconds) ||
        !ReadFloatLE(In, OutData.EndTimeSeconds) ||
        !ReadFloatLE(In, OutData.DurationSeconds) ||
        !ReadFloatLE(In, OutData.SourceFrameRate) ||
        !ReadInt32LE(In, OutData.NumberOfFrames) ||
        !ReadInt32LE(In, OutData.NumberOfKeys))
    {
        return false;
    }

    uint32 BoneTrackCount = 0;
    if (!ReadUInt32LE(In, BoneTrackCount) || BoneTrackCount != Header.BoneTrackCount)
    {
        return false;
    }

    OutData.BoneTracks.resize(BoneTrackCount);
    for (FBoneAnimationTrack& Track : OutData.BoneTracks)
    {
        if (!ReadBoneTrack(In, Track))
        {
            return false;
        }
    }

    uint32 ShapeKeyTrackCount = 0;
    if (!ReadUInt32LE(In, ShapeKeyTrackCount) || ShapeKeyTrackCount != Header.ShapeKeyTrackCount)
    {
        return false;
    }

    OutData.ShapeKeyTracks.resize(ShapeKeyTrackCount);
    for (FShapeKeyAnimationTrack& Track : OutData.ShapeKeyTracks)
    {
        if (!ReadShapeKeyTrack(In, Track))
        {
            return false;
        }
    }

    return In.good();
}
