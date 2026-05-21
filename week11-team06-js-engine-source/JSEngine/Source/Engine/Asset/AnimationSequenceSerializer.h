#pragma once

#include "Asset/AnimationTypes.h"
#include "Core/CoreMinimal.h"

#include <fstream>

class UAnimationSequence;

struct FAnimationSequenceBinaryHeader
{
    uint32 MagicNumber = 0x4D494E41; // 'ANIM'
    uint32 Version = 5;
    uint32 BoneTrackCount = 0;
    uint32 ShapeKeyTrackCount = 0;
    uint32 CurveCount = 0;
    uint32 KeyCount = 0;
    uint64 SourceFileWriteTime = 0;
};

class FAnimationSequenceSerializer
{
public:
    bool SaveAnimationSequence(const FString& BinaryPath, const FString& SourcePath, const FAnimationSequence& Data);
    bool LoadAnimationSequence(const FString& FbxOrBinaryPath, FAnimationSequence& OutData);
    bool ReadAnimationSequenceHeader(const FString& BinaryPath, FAnimationSequenceBinaryHeader& OutHeader) const;

    bool SaveAnimationSequenceAsset(const FString& AssetPath, const UAnimationSequence& Sequence);
    bool LoadAnimationSequenceAsset(const FString& AssetPath, UAnimationSequence& OutSequence);

	bool WriteAnimationSequence(std::ofstream& Out, const FString& SourcePath, const FAnimationSequence& Data);
    bool ReadAnimationSequence(std::ifstream& In, FAnimationSequence& OutData);

private:
    void WriteBool(std::ofstream& Out, bool Value);
    void WriteInt32LE(std::ofstream& Out, int32 Value);
    void WriteUInt32LE(std::ofstream& Out, uint32 Value);
    void WriteUInt64LE(std::ofstream& Out, uint64 Value);
    void WriteFloatLE(std::ofstream& Out, float Value);
    void WriteString(std::ofstream& Out, const FString& Value);
    void WriteVector(std::ofstream& Out, const FVector& Value);
    void WriteQuat(std::ofstream& Out, const FQuat& Value);
    void WriteHeader(std::ofstream& Out, const FAnimationSequenceBinaryHeader& Header);
    void WriteCurveKey(std::ofstream& Out, const FAnimationCurveKey& Key);
    void WriteFloatCurve(std::ofstream& Out, const FAnimationFloatCurve& Curve);
    void WriteRawAnimSequenceTrack(std::ofstream& Out, const FRawAnimSequenceTrack& Track);
    void WriteBoneTrack(std::ofstream& Out, const FBoneAnimationTrack& Track);
    void WriteShapeKeyTrack(std::ofstream& Out, const FShapeKeyAnimationTrack& Track);

    bool ReadBool(std::ifstream& In, bool& OutValue) const;
    bool ReadInt32LE(std::ifstream& In, int32& OutValue) const;
    bool ReadUInt32LE(std::ifstream& In, uint32& OutValue) const;
    bool ReadUInt64LE(std::ifstream& In, uint64& OutValue) const;
    bool ReadFloatLE(std::ifstream& In, float& OutValue) const;
    bool ReadString(std::ifstream& In, FString& OutValue) const;
    bool ReadVector(std::ifstream& In, FVector& OutValue) const;
    bool ReadQuat(std::ifstream& In, FQuat& OutValue) const;
    bool ReadHeader(std::ifstream& In, FAnimationSequenceBinaryHeader& OutHeader) const;
    bool ReadCurveKey(std::ifstream& In, FAnimationCurveKey& OutKey) const;
    bool ReadFloatCurve(std::ifstream& In, FAnimationFloatCurve& OutCurve) const;
    bool ReadRawAnimSequenceTrack(std::ifstream& In, FRawAnimSequenceTrack& OutTrack) const;
    bool ReadBoneTrack(std::ifstream& In, FBoneAnimationTrack& OutTrack) const;
    bool ReadShapeKeyTrack(std::ifstream& In, FShapeKeyAnimationTrack& OutTrack) const;
};

