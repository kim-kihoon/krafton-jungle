#include "Runtime/Script/ScriptManager.h"

#include "Animation/ActorSequence.h"
#include "Animation/AnimStateMachineAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimStateMachineNode.h"
#include "Asset/CurveFloatAsset.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/ResourceManager.h"
#include "Object/Object.h"
#include "Runtime/Script/ScriptComponent.h"
#include "Runtime/Script/ScriptUtils.h"

namespace
{
EAnimBlendEaseOption LuaParseAnimBlendEaseOption(const FString& EaseOptionName)
{
    if (EaseOptionName == "EaseIn")
    {
        return EAnimBlendEaseOption::EaseIn;
    }

    if (EaseOptionName == "EaseOut")
    {
        return EAnimBlendEaseOption::EaseOut;
    }

    if (EaseOptionName == "EaseInOut")
    {
        return EAnimBlendEaseOption::EaseInOut;
    }

    return EAnimBlendEaseOption::Linear;
}

EAnimCompareOp LuaParseAnimCompareOp(const FString& OpName)
{
    if (OpName == "NotEqual" || OpName == "!=")
    {
        return EAnimCompareOp::NotEqual;
    }

    if (OpName == "Greater" || OpName == ">")
    {
        return EAnimCompareOp::Greater;
    }

    if (OpName == "GreaterEqual" || OpName == ">=")
    {
        return EAnimCompareOp::GreaterEqual;
    }

    if (OpName == "Less" || OpName == "<")
    {
        return EAnimCompareOp::Less;
    }

    if (OpName == "LessEqual" || OpName == "<=")
    {
        return EAnimCompareOp::LessEqual;
    }

    if (OpName == "IsTrue")
    {
        return EAnimCompareOp::IsTrue;
    }

    if (OpName == "IsFalse")
    {
        return EAnimCompareOp::IsFalse;
    }

    return EAnimCompareOp::Equal;
}

bool LuaTryParseAnimParameterType(const FString& TypeName, EAnimParameterType& OutType)
{
    if (TypeName == "Bool")
    {
        OutType = EAnimParameterType::Bool;
        return true;
    }

    if (TypeName == "Int")
    {
        OutType = EAnimParameterType::Int;
        return true;
    }

    if (TypeName == "Float")
    {
        OutType = EAnimParameterType::Float;
        return true;
    }

    if (TypeName == "Vector")
    {
        OutType = EAnimParameterType::Vector;
        return true;
    }

    if (TypeName == "Trigger")
    {
        OutType = EAnimParameterType::Trigger;
        return true;
    }

    return false;
}

FString LuaGetStringField(const sol::table& Table, const char* UpperName, const char* LowerName, const FString& DefaultValue = "")
{
    sol::object Value = Table[UpperName];
    if (Value.valid() && Value != sol::nil && Value.get_type() == sol::type::string)
    {
        return Value.as<FString>();
    }

    Value = Table[LowerName];
    if (Value.valid() && Value != sol::nil && Value.get_type() == sol::type::string)
    {
        return Value.as<FString>();
    }

    return DefaultValue;
}

sol::object LuaGetField(const sol::table& Table, const char* UpperName, const char* LowerName)
{
    sol::object Value = Table[UpperName];
    if (Value.valid() && Value != sol::nil)
    {
        return Value;
    }

    return Table[LowerName];
}

bool LuaTryBuildAnimTransitionCondition(const sol::table& Table, FAnimTransitionCondition& OutCondition)
{
    const FString ParameterName = LuaGetStringField(Table, "ParameterName", "parameterName");
    const FString ParameterTypeName = LuaGetStringField(Table, "Type", "type");
    if (ParameterName.empty() || ParameterTypeName.empty())
    {
        return false;
    }

    EAnimParameterType ParameterType = EAnimParameterType::Bool;
    if (!LuaTryParseAnimParameterType(ParameterTypeName, ParameterType))
    {
        return false;
    }

    OutCondition.ParameterName = FName(ParameterName);
    OutCondition.CompareOp = LuaParseAnimCompareOp(LuaGetStringField(Table, "CompareOp", "compareOp", "Equal"));

    if (ParameterType == EAnimParameterType::Trigger)
    {
        OutCondition.CompareOp = EAnimCompareOp::IsTrue;
        return true;
    }

    sol::object Value = LuaGetField(Table, "Value", "value");
    if (!Value.valid() || Value == sol::nil)
    {
        return false;
    }

    if (ParameterType == EAnimParameterType::Bool)
    {
        OutCondition.CompareValue.BoolValue = Value.as<bool>();
        return true;
    }

    if (ParameterType == EAnimParameterType::Int)
    {
        OutCondition.CompareValue.IntValue = Value.as<int32>();
        return true;
    }

    if (ParameterType == EAnimParameterType::Float)
    {
        OutCondition.CompareValue.FloatValue = Value.as<float>();
        return true;
    }

    if (ParameterType == EAnimParameterType::Vector)
    {
        OutCondition.CompareValue.VectorValue = Value.as<FVector>();
        return true;
    }

    return false;
}

bool LuaTryBuildAnimTransitionConditions(const sol::table& ConditionsTable, TArray<FAnimTransitionCondition>& OutConditions)
{
    OutConditions.clear();

    for (const auto& Pair : ConditionsTable)
    {
        sol::object ConditionObj = Pair.second;
        if (!ConditionObj.valid() || ConditionObj.get_type() != sol::type::table)
        {
            continue;
        }

        FAnimTransitionCondition Condition;
        if (!LuaTryBuildAnimTransitionCondition(ConditionObj.as<sol::table>(), Condition))
        {
            return false;
        }

        OutConditions.push_back(Condition);
    }

    return !OutConditions.empty();
}
} // namespace

void FScriptManager::BindAnimationTypes()
{
    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UCurveFloatAsset, "CurveFloatAsset", UObject)
    LUA_METHOD(Evaluate, Evaluate);
    LUA_METHOD(GetAssetPath, GetAssetPath);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UActorSequence, "ActorSequence", UObject)
    LUA_FIELD(StartTime, StartTime);
    LUA_FIELD(Duration, Duration);
    LUA_FIELD(Loop, bLoop);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UActorSequencePlayer, "ActorSequencePlayer", UObject)
    LUA_METHOD(Play, Play);
    LUA_METHOD(Pause, Pause);
    LUA_METHOD(Stop, Stop);
    LUA_METHOD(SetCurrentTime, SetCurrentTime);
    LUA_METHOD(GetCurrentTime, GetCurrentTime);
    LUA_METHOD(IsPlaying, IsPlaying);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR(GLuaState, FLuaTimeline, "LuaTimeline")
    LUA_METHOD(Play, Play);
    LUA_METHOD(Pause, Pause);
    LUA_METHOD(Stop, Stop);
    LUA_METHOD(Tick, Tick);
    LUA_METHOD(SetPlayRate, SetPlayRate);
    LUA_METHOD(SetLoop, SetLoop);
    LUA_METHOD(SetCurrentTime, SetCurrentTime);
    LUA_METHOD(GetCurrentTime, GetCurrentTime);
    LUA_METHOD(AddFloatTrack, AddFloatTrack);
    LUA_METHOD(ClearTracks, ClearTracks);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR(GLuaState, FAnimStateMachineNode, "AnimStateMachineNode")
    LUA_SET(GetCurrentState, [](const FAnimStateMachineNode& Self)
    {
        return Self.GetCurrentState().ToString();
    });
    LUA_SET(GetPreviousState, [](const FAnimStateMachineNode& Self)
    {
        return Self.GetPreviousState().ToString();
    });
    LUA_METHOD(GetStateElapsedTime, GetStateElapsedTime);
    LUA_METHOD(IsLooping, IsLooping);
    LUA_METHOD(IsReversePlaying, IsReversePlaying);
    LUA_METHOD(GetAsset, GetAsset);
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UAnimStateMachineAsset, "AnimStateMachineAsset", UObject)
    LUA_SET(SetEntryState, [](UAnimStateMachineAsset& Self, const FString& StateName)
    {
        Self.SetEntryState(FName(StateName));
    });
    LUA_SET(AddState, [](UAnimStateMachineAsset& Self, const FString& StateName, const FString& AnimationName, bool bLoop)
    {
        return Self.AddState(FName(StateName), FName(AnimationName), bLoop);
    });
    LUA_SET(AddStateWithPath, [](
        UAnimStateMachineAsset& Self,
        const FString& StateName,
        const FString& AnimationName,
        const FString& AnimationPath,
        bool bLoop)
    {
        return Self.AddState(FName(StateName), FName(AnimationName), bLoop, AnimationPath);
    });
    LUA_SET(SetStateAnimationPath, [](
        UAnimStateMachineAsset& Self,
        const FString& StateName,
        const FString& AnimationPath)
    {
        return Self.SetStateAnimationPath(FName(StateName), AnimationPath);
    });
    LUA_SET(AddParameter, [](UAnimStateMachineAsset& Self, const FString& ParameterName, const FString& ParameterType)
    {
        EAnimParameterType ParsedType = EAnimParameterType::Bool;
        return LuaTryParseAnimParameterType(ParameterType, ParsedType) &&
            Self.AddParameter(FName(ParameterName), ParsedType);
    });
    LUA_SET(RenameParameter, [](UAnimStateMachineAsset& Self, const FString& OldParameterName, const FString& NewParameterName)
    {
        return Self.RenameParameter(FName(OldParameterName), FName(NewParameterName));
    });
    LUA_SET(SetParameterType, [](UAnimStateMachineAsset& Self, const FString& ParameterName, const FString& ParameterType)
    {
        EAnimParameterType ParsedType = EAnimParameterType::Bool;
        return LuaTryParseAnimParameterType(ParameterType, ParsedType) &&
            Self.SetParameterType(FName(ParameterName), ParsedType);
    });
    LUA_SET(RemoveParameter, [](UAnimStateMachineAsset& Self, const FString& ParameterName)
    {
        return Self.RemoveParameter(FName(ParameterName));
    });
    LUA_SET(RemoveParameterAndConditions, [](UAnimStateMachineAsset& Self, const FString& ParameterName)
    {
        return Self.RemoveParameterAndConditions(FName(ParameterName));
    });
    LUA_SET(RemoveUnusedParameters, [](UAnimStateMachineAsset& Self)
    {
        return Self.RemoveUnusedParameters();
    });
    auto AddTypedTransition = [](
        UAnimStateMachineAsset& Self,
        const FString& FromState,
        const FString& ToState,
        const FString& ParameterName,
        const FString& ParameterType,
        const FString& CompareOp,
        sol::object Value,
        float BlendTime,
        int32 Priority,
        sol::optional<FString> EaseOption)
    {
        const EAnimBlendEaseOption BlendEaseOption = LuaParseAnimBlendEaseOption(EaseOption.value_or("Linear"));
        const EAnimCompareOp ParsedCompareOp = LuaParseAnimCompareOp(CompareOp);
        EAnimParameterType ParsedType = EAnimParameterType::Bool;
        if (!LuaTryParseAnimParameterType(ParameterType, ParsedType))
        {
            return false;
        }

        if (ParsedType == EAnimParameterType::Trigger)
        {
            return Self.AddTriggerTransition(FName(FromState), FName(ToState), FName(ParameterName), BlendTime, Priority, BlendEaseOption);
        }

        if (ParsedType == EAnimParameterType::Int)
        {
            return Self.AddIntTransition(
                FName(FromState),
                FName(ToState),
                FName(ParameterName),
                ParsedCompareOp,
                Value.as<int32>(),
                BlendTime,
                Priority,
                BlendEaseOption);
        }

        if (ParsedType == EAnimParameterType::Float)
        {
            return Self.AddFloatTransition(
                FName(FromState),
                FName(ToState),
                FName(ParameterName),
                ParsedCompareOp,
                Value.as<float>(),
                BlendTime,
                Priority,
                BlendEaseOption);
        }

        if (ParsedType == EAnimParameterType::Vector)
        {
            return Self.AddVectorTransition(
                FName(FromState),
                FName(ToState),
                FName(ParameterName),
                ParsedCompareOp,
                Value.as<FVector>(),
                BlendTime,
                Priority,
                BlendEaseOption);
        }

        return Self.AddBoolTransition(
            FName(FromState),
            FName(ToState),
            FName(ParameterName),
            ParsedCompareOp,
            Value.as<bool>(),
            BlendTime,
            Priority,
            BlendEaseOption);
    };
    auto AddConditionTableTransition = [](
        UAnimStateMachineAsset& Self,
        const FString& FromState,
        const FString& ToState,
        sol::table ConditionsTable,
        float BlendTime,
        int32 Priority,
        sol::optional<FString> EaseOption)
    {
        TArray<FAnimTransitionCondition> Conditions;
        if (!LuaTryBuildAnimTransitionConditions(ConditionsTable, Conditions))
        {
            return false;
        }

        return Self.AddTransition(
            FName(FromState),
            FName(ToState),
            Conditions,
            BlendTime,
            Priority,
            LuaParseAnimBlendEaseOption(EaseOption.value_or("Linear")));
    };
    LUA_SET(AddTransition, sol::overload(AddConditionTableTransition, AddTypedTransition));
    LUA_SET(AddTransitionWithConditions, AddConditionTableTransition);
    LUA_SET(AddBoolTransition, [](
        UAnimStateMachineAsset& Self,
        const FString& FromState,
        const FString& ToState,
        const FString& ParameterName,
        const FString& CompareOp,
        bool Value,
        float BlendTime,
        int32 Priority,
        sol::optional<FString> EaseOption)
    {
        return Self.AddBoolTransition(
            FName(FromState),
            FName(ToState),
            FName(ParameterName),
            LuaParseAnimCompareOp(CompareOp),
            Value,
            BlendTime,
            Priority,
            LuaParseAnimBlendEaseOption(EaseOption.value_or("Linear")));
    });
    LUA_SET(AddIntTransition, [](
        UAnimStateMachineAsset& Self,
        const FString& FromState,
        const FString& ToState,
        const FString& ParameterName,
        const FString& CompareOp,
        int32 Value,
        float BlendTime,
        int32 Priority,
        sol::optional<FString> EaseOption)
    {
        return Self.AddIntTransition(
            FName(FromState),
            FName(ToState),
            FName(ParameterName),
            LuaParseAnimCompareOp(CompareOp),
            Value,
            BlendTime,
            Priority,
            LuaParseAnimBlendEaseOption(EaseOption.value_or("Linear")));
    });
    LUA_SET(AddFloatTransition, [](
        UAnimStateMachineAsset& Self,
        const FString& FromState,
        const FString& ToState,
        const FString& ParameterName,
        const FString& CompareOp,
        float Value,
        float BlendTime,
        int32 Priority,
        sol::optional<FString> EaseOption)
    {
        return Self.AddFloatTransition(
            FName(FromState),
            FName(ToState),
            FName(ParameterName),
            LuaParseAnimCompareOp(CompareOp),
            Value,
            BlendTime,
            Priority,
            LuaParseAnimBlendEaseOption(EaseOption.value_or("Linear")));
    });
    LUA_SET(AddVectorTransition, [](
        UAnimStateMachineAsset& Self,
        const FString& FromState,
        const FString& ToState,
        const FString& ParameterName,
        const FString& CompareOp,
        const FVector& Value,
        float BlendTime,
        int32 Priority,
        sol::optional<FString> EaseOption)
    {
        return Self.AddVectorTransition(
            FName(FromState),
            FName(ToState),
            FName(ParameterName),
            LuaParseAnimCompareOp(CompareOp),
            Value,
            BlendTime,
            Priority,
            LuaParseAnimBlendEaseOption(EaseOption.value_or("Linear")));
    });
    LUA_SET(AddTriggerTransition, [](
        UAnimStateMachineAsset& Self,
        const FString& FromState,
        const FString& ToState,
        const FString& ParameterName,
        float BlendTime,
        int32 Priority,
        sol::optional<FString> EaseOption)
    {
        return Self.AddTriggerTransition(
            FName(FromState),
            FName(ToState),
            FName(ParameterName),
            BlendTime,
            Priority,
            LuaParseAnimBlendEaseOption(EaseOption.value_or("Linear")));
    });
    LUA_SET(Validate, [](const UAnimStateMachineAsset& Self)
    {
        return Self.Validate();
    });
    LUA_SET(Save, [](const UAnimStateMachineAsset& Self, const FString& AssetPath)
    {
        return Self.SaveToFile(AssetPath);
    });
    LUA_END_TYPE();

    LUA_BEGIN_TYPE_NO_CTOR_BASE(GLuaState, UAnimInstance, "AnimInstance", UObject)
    LUA_SET(RegisterAnimationPath, [](UAnimInstance& Self, const FString& AnimationName, const FString& AnimationPath)
    {
        return Self.RegisterAnimationPath(FName(AnimationName), AnimationPath);
    });
    LUA_SET(SetStateMachineAsset, [](UAnimInstance& Self, UAnimStateMachineAsset* StateMachineAsset)
    {
        Self.SetStateMachineAsset(StateMachineAsset);
    });
    LUA_SET(PlayAnimationByName, [](UAnimInstance& Self, const FString& AnimationName, bool bLoop)
    {
        return Self.PlayAnimationByName(FName(AnimationName), bLoop);
    });
    LUA_SET(BlendToAnimationByName, [](
        UAnimInstance& Self,
        const FString& AnimationName,
        bool bLoop,
        float BlendTime,
        sol::optional<FString> EaseOption)
    {
        EAnimBlendEaseOption BlendEaseOption = EAnimBlendEaseOption::Linear;
        const FString EaseOptionName = EaseOption.value_or("Linear");
        if (EaseOptionName == "EaseIn")
        {
            BlendEaseOption = EAnimBlendEaseOption::EaseIn;
        }
        else if (EaseOptionName == "EaseOut")
        {
            BlendEaseOption = EAnimBlendEaseOption::EaseOut;
        }
        else if (EaseOptionName == "EaseInOut")
        {
            BlendEaseOption = EAnimBlendEaseOption::EaseInOut;
        }

        return Self.BlendToAnimationByName(FName(AnimationName), bLoop, BlendTime, BlendEaseOption);
    });
    LUA_METHOD(SetLooping, SetLooping);
    LUA_METHOD(IsLooping, IsLooping);
    LUA_SET(SetAnimBoolParameter, [](UAnimInstance& Self, const FString& Name, bool Value)
    {
        Self.SetAnimBoolParameter(FName(Name), Value);
    });
    LUA_SET(SetAnimIntParameter, [](UAnimInstance& Self, const FString& Name, int32 Value)
    {
        Self.SetAnimIntParameter(FName(Name), Value);
    });
    LUA_SET(SetAnimFloatParameter, [](UAnimInstance& Self, const FString& Name, float Value)
    {
        Self.SetAnimFloatParameter(FName(Name), Value);
    });
    LUA_SET(SetAnimVectorParameter, [](UAnimInstance& Self, const FString& Name, const FVector& Value)
    {
        Self.SetAnimVectorParameter(FName(Name), Value);
    });
    LUA_SET(SetAnimTriggerParameter, [](UAnimInstance& Self, const FString& Name)
    {
        Self.SetAnimTriggerParameter(FName(Name));
    });
    LUA_SET(GetStateMachine, [](UAnimInstance& Self)
    {
        return Self.GetStateMachine();
    });
    LUA_METHOD(GetStateMachineAsset, GetStateMachineAsset);
    LUA_SET(GetCurrentState, [](const UAnimInstance& Self)
    {
        const FAnimStateMachineNode* StateMachine = Self.GetStateMachine();
        return StateMachine ? StateMachine->GetCurrentState().ToString() : FString();
    });
    LUA_SET(GetPreviousState, [](const UAnimInstance& Self)
    {
        const FAnimStateMachineNode* StateMachine = Self.GetStateMachine();
        return StateMachine ? StateMachine->GetPreviousState().ToString() : FString();
    });
    LUA_SET(GetStateElapsedTime, [](const UAnimInstance& Self)
    {
        const FAnimStateMachineNode* StateMachine = Self.GetStateMachine();
        return StateMachine ? StateMachine->GetStateElapsedTime() : 0.0f;
    });
    LUA_METHOD(GetSkelMeshComponent, GetSkelMeshComponent);
    LUA_END_TYPE();

    GLuaState->set_function("CreateAnimStateMachineAsset", []() -> UAnimStateMachineAsset*
    {
        return UObjectManager::Get().CreateObject<UAnimStateMachineAsset>();
    });
    GLuaState->set_function("LoadAnimStateMachineAsset", [](const FString& AssetPath) -> UAnimStateMachineAsset*
    {
        return FResourceManager::Get().LoadAnimStateMachineAsset(AssetPath);
    });
}
