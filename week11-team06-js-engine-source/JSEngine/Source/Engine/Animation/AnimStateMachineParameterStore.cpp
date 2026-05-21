#include "Animation/AnimStateMachineParameterStore.h"

void FAnimStateMachineParameterStore::EnsureDefaults(const TArray<FAnimStateMachineParameterDesc>& Declarations)
{
    for (const FAnimStateMachineParameterDesc& Declaration : Declarations)
    {
        if (!Declaration.Name.IsValid())
        {
            continue;
        }

        FAnimParameter* ExistingParameter = FindParameter(Declaration.Name);
        if (ExistingParameter)
        {
            if (ExistingParameter->Type != Declaration.Type)
            {
                ResetToDefault(*ExistingParameter, Declaration.Type);
            }
            continue;
        }

        FAnimParameter Parameter;
        Parameter.Name = Declaration.Name;
        ResetToDefault(Parameter, Declaration.Type);
        Parameters.push_back(Parameter);
    }
}

void FAnimStateMachineParameterStore::SetBool(const FName& Name, bool Value)
{
    FAnimParameter& Parameter = FindOrAddParameter(Name, EAnimParameterType::Bool);
    Parameter.Value.BoolValue = Value;
}

void FAnimStateMachineParameterStore::SetInt(const FName& Name, int32 Value)
{
    FAnimParameter& Parameter = FindOrAddParameter(Name, EAnimParameterType::Int);
    Parameter.Value.IntValue = Value;
}

void FAnimStateMachineParameterStore::SetFloat(const FName& Name, float Value)
{
    FAnimParameter& Parameter = FindOrAddParameter(Name, EAnimParameterType::Float);
    Parameter.Value.FloatValue = Value;
}

void FAnimStateMachineParameterStore::SetVector(const FName& Name, const FVector& Value)
{
    FAnimParameter& Parameter = FindOrAddParameter(Name, EAnimParameterType::Vector);
    Parameter.Value.VectorValue = Value;
}

void FAnimStateMachineParameterStore::SetTrigger(const FName& Name)
{
    FAnimParameter& Parameter = FindOrAddParameter(Name, EAnimParameterType::Trigger);
    Parameter.bTriggerSet = true;
}

bool FAnimStateMachineParameterStore::GetBool(const FName& Name, bool& OutValue) const
{
    const FAnimParameter* Parameter = FindParameter(Name);
    if (!Parameter || Parameter->Type != EAnimParameterType::Bool)
    {
        return false;
    }

    OutValue = Parameter->Value.BoolValue;
    return true;
}

bool FAnimStateMachineParameterStore::GetInt(const FName& Name, int32& OutValue) const
{
    const FAnimParameter* Parameter = FindParameter(Name);
    if (!Parameter || Parameter->Type != EAnimParameterType::Int)
    {
        return false;
    }

    OutValue = Parameter->Value.IntValue;
    return true;
}

bool FAnimStateMachineParameterStore::GetFloat(const FName& Name, float& OutValue) const
{
    const FAnimParameter* Parameter = FindParameter(Name);
    if (!Parameter || Parameter->Type != EAnimParameterType::Float)
    {
        return false;
    }

    OutValue = Parameter->Value.FloatValue;
    return true;
}

bool FAnimStateMachineParameterStore::GetVector(const FName& Name, FVector& OutValue) const
{
    const FAnimParameter* Parameter = FindParameter(Name);
    if (!Parameter || Parameter->Type != EAnimParameterType::Vector)
    {
        return false;
    }

    OutValue = Parameter->Value.VectorValue;
    return true;
}

bool FAnimStateMachineParameterStore::ConsumeTrigger(const FName& Name)
{
    FAnimParameter* Parameter = FindParameter(Name);
    if (!Parameter || Parameter->Type != EAnimParameterType::Trigger || !Parameter->bTriggerSet)
    {
        return false;
    }

    Parameter->bTriggerSet = false;
    return true;
}

FAnimParameter* FAnimStateMachineParameterStore::FindParameter(const FName& Name)
{
    for (FAnimParameter& Parameter : Parameters)
    {
        if (Parameter.Name == Name)
        {
            return &Parameter;
        }
    }

    return nullptr;
}

const FAnimParameter* FAnimStateMachineParameterStore::FindParameter(const FName& Name) const
{
    for (const FAnimParameter& Parameter : Parameters)
    {
        if (Parameter.Name == Name)
        {
            return &Parameter;
        }
    }

    return nullptr;
}

FAnimParameter& FAnimStateMachineParameterStore::FindOrAddParameter(const FName& Name, EAnimParameterType Type)
{
    FAnimParameter* ExistingParameter = FindParameter(Name);
    if (ExistingParameter)
    {
        ExistingParameter->Type = Type;
        return *ExistingParameter;
    }

    FAnimParameter Parameter;
    Parameter.Name = Name;
    Parameter.Type = Type;
    Parameters.push_back(Parameter);
    return Parameters.back();
}

void FAnimStateMachineParameterStore::ResetToDefault(FAnimParameter& Parameter, EAnimParameterType Type)
{
    Parameter.Type = Type;
    Parameter.Value = FAnimParameterValue();
    Parameter.bTriggerSet = false;
}
