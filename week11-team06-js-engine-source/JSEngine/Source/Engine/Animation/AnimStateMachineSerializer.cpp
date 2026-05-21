#include "Animation/AnimStateMachineSerializer.h"

#include "Animation/AnimStateMachineAsset.h"
#include "Core/Logging/Log.h"
#include "Core/Paths.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace
{
constexpr uint32 ANIM_STATE_MACHINE_MAGIC = 0x534D5341;
constexpr uint32 ANIM_STATE_MACHINE_VERSION = 3;
constexpr uint32 MAX_STATE_COUNT = 4096;
constexpr uint32 MAX_TRANSITION_COUNT = 16384;
constexpr uint32 MAX_CONDITION_COUNT = 65536;
constexpr uint32 MAX_PARAMETER_COUNT = 4096;
constexpr uint32 MAX_METADATA_COUNT = 65536;
constexpr uint32 MAX_STRING_LENGTH = 65536;
}

bool FAnimStateMachineSerializer::Save(const FString& AssetPath, const UAnimStateMachineAsset& Asset)
{
    FString ValidationMessage;
    if (!Asset.Validate(&ValidationMessage))
    {
        UE_LOG_WARNING("[AnimSM] Save failed: invalid asset %s", ValidationMessage.c_str());
        return false;
    }

    const std::filesystem::path AbsolutePath(FPaths::ToAbsolute(FPaths::ToWide(AssetPath)));
    std::error_code Ec;
    std::filesystem::create_directories(AbsolutePath.parent_path(), Ec);

    std::ofstream Out(AbsolutePath, std::ios::binary | std::ios::trunc);
    if (!Out.is_open())
    {
        UE_LOG_WARNING("[AnimSM] Save failed: cannot open %s", AssetPath.c_str());
        return false;
    }

    uint32 ConditionCount = 0;
    for (const FAnimTransitionDesc& Transition : Asset.GetTransitions())
    {
        ConditionCount += static_cast<uint32>(Transition.Conditions.size());
    }

    FAnimStateMachineBinaryHeader Header;
    Header.MagicNumber = ANIM_STATE_MACHINE_MAGIC;
    Header.Version = ANIM_STATE_MACHINE_VERSION;
    Header.StateCount = static_cast<uint32>(Asset.GetStates().size());
    Header.TransitionCount = static_cast<uint32>(Asset.GetTransitions().size());
    Header.ConditionCount = ConditionCount;
    Header.ParameterCount = static_cast<uint32>(Asset.GetParameters().size());
    Header.StateMetadataCount = static_cast<uint32>(Asset.GetStateEditorMetadata().size());
    Header.TransitionMetadataCount = static_cast<uint32>(Asset.GetTransitionEditorMetadata().size());
    WriteHeader(Out, Header);
    WriteUInt32LE(Out, Asset.GetEntryStateId());
    WriteString(Out, Asset.GetEntryState().ToString());

    for (const FAnimStateMachineParameterDesc& Parameter : Asset.GetParameters())
    {
        WriteParameter(Out, Parameter);
    }

    for (const FAnimStateDesc& State : Asset.GetStates())
    {
        WriteUInt32LE(Out, State.Id);
        WriteString(Out, State.StateName.ToString());
        WriteString(Out, State.AnimationName.ToString());
        WriteString(Out, State.AnimationPath);
        WriteBool(Out, State.bLoop);
    }

    for (const FAnimTransitionDesc& Transition : Asset.GetTransitions())
    {
        WriteUInt32LE(Out, Transition.Id);
        WriteUInt32LE(Out, Transition.FromStateId);
        WriteUInt32LE(Out, Transition.ToStateId);
        WriteFloatLE(Out, Transition.BlendTime);
        WriteUInt32LE(Out, static_cast<uint32>(Transition.EaseOption));
        WriteInt32LE(Out, Transition.Priority);
        WriteUInt32LE(Out, static_cast<uint32>(Transition.Conditions.size()));
        for (const FAnimTransitionCondition& Condition : Transition.Conditions)
        {
            WriteCondition(Out, Condition);
        }
    }

    for (const FAnimStateEditorMetadata& Metadata : Asset.GetStateEditorMetadata())
    {
        WriteUInt32LE(Out, Metadata.StateId);
        WriteFloatLE(Out, Metadata.NodeX);
        WriteFloatLE(Out, Metadata.NodeY);
    }

    for (const FAnimTransitionEditorMetadata& Metadata : Asset.GetTransitionEditorMetadata())
    {
        WriteUInt32LE(Out, Metadata.TransitionId);
    }

    return Out.good();
}

bool FAnimStateMachineSerializer::Load(const FString& AssetPath, UAnimStateMachineAsset& OutAsset)
{
    std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(AssetPath))), std::ios::binary);
    if (!In.is_open())
    {
        UE_LOG_WARNING("[AnimSM] Load failed: cannot open %s", AssetPath.c_str());
        return false;
    }

    FAnimStateMachineBinaryHeader Header;
    if (!ReadHeader(In, Header))
    {
        UE_LOG_WARNING("[AnimSM] Load failed: invalid header %s", AssetPath.c_str());
        return false;
    }

    if (Header.MagicNumber != ANIM_STATE_MACHINE_MAGIC)
    {
        UE_LOG_WARNING("[AnimSM] Load failed: invalid magic %s", AssetPath.c_str());
        return false;
    }

    if (Header.Version != ANIM_STATE_MACHINE_VERSION)
    {
        UE_LOG_WARNING(
            "[AnimSM] Load failed: unsupported version %u for %s (expected %u)",
            Header.Version,
            AssetPath.c_str(),
            ANIM_STATE_MACHINE_VERSION);
        return false;
    }

    if (!IsValidHeader(Header))
    {
        UE_LOG_WARNING("[AnimSM] Load failed: invalid counts %s", AssetPath.c_str());
        return false;
    }

    OutAsset.Clear();

    uint32 EntryStateId = InvalidAnimStateId;
    FString EntryStateName;
    if (!ReadUInt32LE(In, EntryStateId) || !ReadString(In, EntryStateName))
    {
        return false;
    }

    for (uint32 Index = 0; Index < Header.ParameterCount; ++Index)
    {
        FAnimStateMachineParameterDesc Parameter;
        if (!ReadParameter(In, Parameter) || !OutAsset.AddParameter(Parameter.Name, Parameter.Type))
        {
            return false;
        }
    }

    for (uint32 Index = 0; Index < Header.StateCount; ++Index)
    {
        if (!ReadStateRecord(In, &OutAsset, nullptr))
        {
            return false;
        }
    }

    OutAsset.SetEntryStateId(EntryStateId);
    if (OutAsset.GetEntryStateId() == InvalidAnimStateId && !EntryStateName.empty())
    {
        OutAsset.SetEntryState(FName(EntryStateName));
    }

    uint32 ReadConditionCount = 0;
    for (uint32 Index = 0; Index < Header.TransitionCount; ++Index)
    {
        uint32 TransitionId = InvalidAnimTransitionId;
        uint32 FromStateId = InvalidAnimStateId;
        uint32 ToStateId = InvalidAnimStateId;
        float BlendTime = 0.0f;
        uint32 EaseOption = 0;
        int32 Priority = 0;
        uint32 ConditionCount = 0;
        if (!ReadUInt32LE(In, TransitionId) ||
            !ReadUInt32LE(In, FromStateId) ||
            !ReadUInt32LE(In, ToStateId) ||
            !ReadFloatLE(In, BlendTime) ||
            !ReadUInt32LE(In, EaseOption) ||
            !ReadInt32LE(In, Priority) ||
            !ReadUInt32LE(In, ConditionCount) ||
            ConditionCount > MAX_CONDITION_COUNT ||
            ReadConditionCount + ConditionCount > Header.ConditionCount)
        {
            return false;
        }

        TArray<FAnimTransitionCondition> Conditions;
        Conditions.reserve(ConditionCount);
        for (uint32 ConditionIndex = 0; ConditionIndex < ConditionCount; ++ConditionIndex)
        {
            FAnimTransitionCondition Condition;
            if (!ReadCondition(In, Condition))
            {
                return false;
            }
            Conditions.push_back(Condition);
        }
        ReadConditionCount += ConditionCount;

        if (!OutAsset.AddTransitionWithId(
            TransitionId,
            FromStateId,
            ToStateId,
            Conditions,
            BlendTime,
            Priority,
            static_cast<EAnimBlendEaseOption>(EaseOption)))
        {
            return false;
        }
    }

    if (ReadConditionCount != Header.ConditionCount)
    {
        return false;
    }

    for (uint32 Index = 0; Index < Header.StateMetadataCount; ++Index)
    {
        FAnimStateEditorMetadata Metadata;
        if (!ReadUInt32LE(In, Metadata.StateId) ||
            !ReadFloatLE(In, Metadata.NodeX) ||
            !ReadFloatLE(In, Metadata.NodeY))
        {
            return false;
        }
        OutAsset.StateEditorMetadata.push_back(Metadata);
    }

    for (uint32 Index = 0; Index < Header.TransitionMetadataCount; ++Index)
    {
        FAnimTransitionEditorMetadata Metadata;
        if (!ReadUInt32LE(In, Metadata.TransitionId))
        {
            return false;
        }
        OutAsset.TransitionEditorMetadata.push_back(Metadata);
    }

    FString ValidationMessage;
    if (!OutAsset.Validate(&ValidationMessage))
    {
        UE_LOG_WARNING("[AnimSM] Load failed: invalid asset %s | %s", AssetPath.c_str(), ValidationMessage.c_str());
        OutAsset.Clear();
        return false;
    }

    return true;
}

bool FAnimStateMachineSerializer::ReadHeader(const FString& AssetPath, FAnimStateMachineBinaryHeader& OutHeader) const
{
    std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(AssetPath))), std::ios::binary);
    return In.is_open() && ReadHeader(In, OutHeader) && IsValidHeader(OutHeader);
}

bool FAnimStateMachineSerializer::ReadAnimationDependencies(const FString& AssetPath, TArray<FString>& OutAnimationPaths) const
{
    OutAnimationPaths.clear();

    std::ifstream In(std::filesystem::path(FPaths::ToAbsolute(FPaths::ToWide(AssetPath))), std::ios::binary);
    if (!In.is_open())
    {
        return false;
    }

    FAnimStateMachineBinaryHeader Header;
    if (!ReadHeader(In, Header) || !IsValidHeader(Header))
    {
        return false;
    }

    uint32 EntryStateId = InvalidAnimStateId;
    FString EntryStateName;
    if (!ReadUInt32LE(In, EntryStateId) || !ReadString(In, EntryStateName))
    {
        return false;
    }

    for (uint32 Index = 0; Index < Header.ParameterCount; ++Index)
    {
        FAnimStateMachineParameterDesc Parameter;
        if (!ReadParameter(In, Parameter))
        {
            return false;
        }
    }

    for (uint32 Index = 0; Index < Header.StateCount; ++Index)
    {
        FString AnimationPath;
        if (!ReadStateRecord(In, nullptr, &AnimationPath))
        {
            return false;
        }

        if (!AnimationPath.empty() &&
            std::find(OutAnimationPaths.begin(), OutAnimationPaths.end(), AnimationPath) == OutAnimationPaths.end())
        {
            OutAnimationPaths.push_back(AnimationPath);
        }
    }

    for (uint32 Index = 0; Index < Header.TransitionCount; ++Index)
    {
        if (!SkipTransitionRecord(In))
        {
            return false;
        }
    }

    return In.good();
}

void FAnimStateMachineSerializer::WriteBool(std::ofstream& Out, bool Value) const
{
    const uint8 Byte = Value ? 1 : 0;
    Out.write(reinterpret_cast<const char*>(&Byte), sizeof(Byte));
}

void FAnimStateMachineSerializer::WriteUInt32LE(std::ofstream& Out, uint32 Value) const
{
    uint8 Bytes[4] = {
        static_cast<uint8>(Value & 0xFF),
        static_cast<uint8>((Value >> 8) & 0xFF),
        static_cast<uint8>((Value >> 16) & 0xFF),
        static_cast<uint8>((Value >> 24) & 0xFF),
    };
    Out.write(reinterpret_cast<const char*>(Bytes), sizeof(Bytes));
}

void FAnimStateMachineSerializer::WriteInt32LE(std::ofstream& Out, int32 Value) const
{
    WriteUInt32LE(Out, static_cast<uint32>(Value));
}

void FAnimStateMachineSerializer::WriteFloatLE(std::ofstream& Out, float Value) const
{
    uint32 Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(float));
    WriteUInt32LE(Out, Bits);
}

void FAnimStateMachineSerializer::WriteString(std::ofstream& Out, const FString& Value) const
{
    WriteUInt32LE(Out, static_cast<uint32>(Value.size()));
    if (!Value.empty())
    {
        Out.write(Value.data(), Value.size());
    }
}

void FAnimStateMachineSerializer::WriteHeader(std::ofstream& Out, const FAnimStateMachineBinaryHeader& Header) const
{
    WriteUInt32LE(Out, Header.MagicNumber);
    WriteUInt32LE(Out, Header.Version);
    WriteUInt32LE(Out, Header.StateCount);
    WriteUInt32LE(Out, Header.TransitionCount);
    WriteUInt32LE(Out, Header.ConditionCount);
    WriteUInt32LE(Out, Header.ParameterCount);
    WriteUInt32LE(Out, Header.StateMetadataCount);
    WriteUInt32LE(Out, Header.TransitionMetadataCount);
}

void FAnimStateMachineSerializer::WriteParameter(
    std::ofstream& Out,
    const FAnimStateMachineParameterDesc& Parameter) const
{
    WriteString(Out, Parameter.Name.ToString());
    WriteUInt32LE(Out, static_cast<uint32>(Parameter.Type));
}

void FAnimStateMachineSerializer::WriteCondition(std::ofstream& Out, const FAnimTransitionCondition& Condition) const
{
    WriteString(Out, Condition.ParameterName.ToString());
    WriteUInt32LE(Out, static_cast<uint32>(Condition.CompareOp));
    WriteBool(Out, Condition.CompareValue.BoolValue);
    WriteInt32LE(Out, Condition.CompareValue.IntValue);
    WriteFloatLE(Out, Condition.CompareValue.FloatValue);
    WriteFloatLE(Out, Condition.CompareValue.VectorValue.X);
    WriteFloatLE(Out, Condition.CompareValue.VectorValue.Y);
    WriteFloatLE(Out, Condition.CompareValue.VectorValue.Z);
}

bool FAnimStateMachineSerializer::ReadBool(std::ifstream& In, bool& OutValue) const
{
    uint8 Byte = 0;
    if (!In.read(reinterpret_cast<char*>(&Byte), sizeof(Byte)))
    {
        return false;
    }
    OutValue = Byte != 0;
    return true;
}

bool FAnimStateMachineSerializer::ReadUInt32LE(std::ifstream& In, uint32& OutValue) const
{
    uint8 Bytes[4] = {};
    if (!In.read(reinterpret_cast<char*>(Bytes), sizeof(Bytes)))
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

bool FAnimStateMachineSerializer::ReadInt32LE(std::ifstream& In, int32& OutValue) const
{
    uint32 Value = 0;
    if (!ReadUInt32LE(In, Value))
    {
        return false;
    }
    OutValue = static_cast<int32>(Value);
    return true;
}

bool FAnimStateMachineSerializer::ReadFloatLE(std::ifstream& In, float& OutValue) const
{
    uint32 Bits = 0;
    if (!ReadUInt32LE(In, Bits))
    {
        return false;
    }
    std::memcpy(&OutValue, &Bits, sizeof(float));
    return true;
}

bool FAnimStateMachineSerializer::ReadString(std::ifstream& In, FString& OutValue) const
{
    uint32 Length = 0;
    if (!ReadUInt32LE(In, Length) || Length > MAX_STRING_LENGTH)
    {
        return false;
    }

    OutValue.clear();
    if (Length == 0)
    {
        return true;
    }

    OutValue.resize(Length);
    return static_cast<bool>(In.read(OutValue.data(), Length));
}

bool FAnimStateMachineSerializer::ReadHeader(std::ifstream& In, FAnimStateMachineBinaryHeader& OutHeader) const
{
    return ReadUInt32LE(In, OutHeader.MagicNumber) &&
        ReadUInt32LE(In, OutHeader.Version) &&
        ReadUInt32LE(In, OutHeader.StateCount) &&
        ReadUInt32LE(In, OutHeader.TransitionCount) &&
        ReadUInt32LE(In, OutHeader.ConditionCount) &&
        ReadUInt32LE(In, OutHeader.ParameterCount) &&
        ReadUInt32LE(In, OutHeader.StateMetadataCount) &&
        ReadUInt32LE(In, OutHeader.TransitionMetadataCount);
}

bool FAnimStateMachineSerializer::ReadParameter(
    std::ifstream& In,
    FAnimStateMachineParameterDesc& OutParameter) const
{
    FString ParameterName;
    uint32 ParameterType = 0;
    if (!ReadString(In, ParameterName) ||
        !ReadUInt32LE(In, ParameterType) ||
        ParameterType > static_cast<uint32>(EAnimParameterType::Trigger))
    {
        return false;
    }

    OutParameter.Name = FName(ParameterName);
    OutParameter.Type = static_cast<EAnimParameterType>(ParameterType);
    return true;
}

bool FAnimStateMachineSerializer::ReadCondition(std::ifstream& In, FAnimTransitionCondition& OutCondition) const
{
    FString ParameterName;
    uint32 CompareOp = 0;
    if (!ReadString(In, ParameterName) ||
        !ReadUInt32LE(In, CompareOp) ||
        CompareOp > static_cast<uint32>(EAnimCompareOp::IsFalse))
    {
        return false;
    }

    OutCondition.ParameterName = FName(ParameterName);
    OutCondition.CompareOp = static_cast<EAnimCompareOp>(CompareOp);
    return ReadBool(In, OutCondition.CompareValue.BoolValue) &&
        ReadInt32LE(In, OutCondition.CompareValue.IntValue) &&
        ReadFloatLE(In, OutCondition.CompareValue.FloatValue) &&
        ReadFloatLE(In, OutCondition.CompareValue.VectorValue.X) &&
        ReadFloatLE(In, OutCondition.CompareValue.VectorValue.Y) &&
        ReadFloatLE(In, OutCondition.CompareValue.VectorValue.Z);
}

bool FAnimStateMachineSerializer::ReadStateRecord(
    std::ifstream& In,
    UAnimStateMachineAsset* OutAsset,
    FString* OutAnimationPath) const
{
    uint32 StateId = InvalidAnimStateId;
    FString StateName;
    FString AnimationName;
    FString AnimationPath;
    bool bLoop = true;
    if (!ReadUInt32LE(In, StateId) ||
        !ReadString(In, StateName) ||
        !ReadString(In, AnimationName) ||
        !ReadString(In, AnimationPath) ||
        !ReadBool(In, bLoop))
    {
        return false;
    }

    AnimationPath = FPaths::Normalize(AnimationPath);
    if (OutAnimationPath)
    {
        *OutAnimationPath = AnimationPath;
    }

    return !OutAsset || OutAsset->AddStateWithId(StateId, FName(StateName), FName(AnimationName), bLoop, AnimationPath);
}

bool FAnimStateMachineSerializer::SkipTransitionRecord(std::ifstream& In) const
{
    uint32 IgnoredUInt = 0;
    float IgnoredFloat = 0.0f;
    int32 IgnoredInt = 0;
    uint32 ConditionCount = 0;
    if (!ReadUInt32LE(In, IgnoredUInt) ||
        !ReadUInt32LE(In, IgnoredUInt) ||
        !ReadUInt32LE(In, IgnoredUInt) ||
        !ReadFloatLE(In, IgnoredFloat) ||
        !ReadUInt32LE(In, IgnoredUInt) ||
        !ReadInt32LE(In, IgnoredInt) ||
        !ReadUInt32LE(In, ConditionCount) ||
        ConditionCount > MAX_CONDITION_COUNT)
    {
        return false;
    }

    for (uint32 Index = 0; Index < ConditionCount; ++Index)
    {
        FAnimTransitionCondition Condition;
        if (!ReadCondition(In, Condition))
        {
            return false;
        }
    }

    return true;
}

bool FAnimStateMachineSerializer::IsValidHeader(const FAnimStateMachineBinaryHeader& Header) const
{
    return Header.MagicNumber == ANIM_STATE_MACHINE_MAGIC &&
        Header.Version == ANIM_STATE_MACHINE_VERSION &&
        Header.StateCount <= MAX_STATE_COUNT &&
        Header.TransitionCount <= MAX_TRANSITION_COUNT &&
        Header.ConditionCount <= MAX_CONDITION_COUNT &&
        Header.ParameterCount <= MAX_PARAMETER_COUNT &&
        Header.StateMetadataCount <= MAX_METADATA_COUNT &&
        Header.TransitionMetadataCount <= MAX_METADATA_COUNT;
}
