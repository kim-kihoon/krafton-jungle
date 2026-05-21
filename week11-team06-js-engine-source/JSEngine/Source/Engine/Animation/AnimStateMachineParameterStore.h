#pragma once

#include "Animation/AnimStateMachineTypes.h"

struct FAnimParameter
{
    FName Name;
    EAnimParameterType Type = EAnimParameterType::Bool;
    FAnimParameterValue Value;
    bool bTriggerSet = false;
};

class FAnimStateMachineParameterStore
{
public:
    void EnsureDefaults(const TArray<FAnimStateMachineParameterDesc>& Declarations);

    void SetBool(const FName& Name, bool Value);
    void SetInt(const FName& Name, int32 Value);
    void SetFloat(const FName& Name, float Value);
    void SetVector(const FName& Name, const FVector& Value);
    void SetTrigger(const FName& Name);

    bool GetBool(const FName& Name, bool& OutValue) const;
    bool GetInt(const FName& Name, int32& OutValue) const;
    bool GetFloat(const FName& Name, float& OutValue) const;
    bool GetVector(const FName& Name, FVector& OutValue) const;
    bool ConsumeTrigger(const FName& Name);

private:
    FAnimParameter* FindParameter(const FName& Name);
    const FAnimParameter* FindParameter(const FName& Name) const;
    FAnimParameter& FindOrAddParameter(const FName& Name, EAnimParameterType Type);
    static void ResetToDefault(FAnimParameter& Parameter, EAnimParameterType Type);

private:
    TArray<FAnimParameter> Parameters;
};
