#pragma once

#include "Animation/AnimStateMachineTypes.h"
#include "Core/CoreTypes.h"

#include <fstream>

class UAnimStateMachineAsset;

struct FAnimStateMachineBinaryHeader
{
    uint32 MagicNumber = 0x534D5341; // 'ASMS'
    uint32 Version = 3;
    uint32 StateCount = 0;
    uint32 TransitionCount = 0;
    uint32 ConditionCount = 0;
    uint32 ParameterCount = 0;
    uint32 StateMetadataCount = 0;
    uint32 TransitionMetadataCount = 0;
};

class FAnimStateMachineSerializer
{
public:
    bool Save(const FString& AssetPath, const UAnimStateMachineAsset& Asset);
    bool Load(const FString& AssetPath, UAnimStateMachineAsset& OutAsset);
    bool ReadHeader(const FString& AssetPath, FAnimStateMachineBinaryHeader& OutHeader) const;
    bool ReadAnimationDependencies(const FString& AssetPath, TArray<FString>& OutAnimationPaths) const;

private:
    void WriteBool(std::ofstream& Out, bool Value) const;
    void WriteUInt32LE(std::ofstream& Out, uint32 Value) const;
    void WriteInt32LE(std::ofstream& Out, int32 Value) const;
    void WriteFloatLE(std::ofstream& Out, float Value) const;
    void WriteString(std::ofstream& Out, const FString& Value) const;
    void WriteHeader(std::ofstream& Out, const FAnimStateMachineBinaryHeader& Header) const;
    void WriteParameter(std::ofstream& Out, const FAnimStateMachineParameterDesc& Parameter) const;
    void WriteCondition(std::ofstream& Out, const FAnimTransitionCondition& Condition) const;

    bool ReadBool(std::ifstream& In, bool& OutValue) const;
    bool ReadUInt32LE(std::ifstream& In, uint32& OutValue) const;
    bool ReadInt32LE(std::ifstream& In, int32& OutValue) const;
    bool ReadFloatLE(std::ifstream& In, float& OutValue) const;
    bool ReadString(std::ifstream& In, FString& OutValue) const;
    bool ReadHeader(std::ifstream& In, FAnimStateMachineBinaryHeader& OutHeader) const;
    bool ReadParameter(std::ifstream& In, FAnimStateMachineParameterDesc& OutParameter) const;
    bool ReadCondition(std::ifstream& In, FAnimTransitionCondition& OutCondition) const;
    bool ReadStateRecord(std::ifstream& In, UAnimStateMachineAsset* OutAsset, FString* OutAnimationPath) const;
    bool SkipTransitionRecord(std::ifstream& In) const;

    bool IsValidHeader(const FAnimStateMachineBinaryHeader& Header) const;
};
