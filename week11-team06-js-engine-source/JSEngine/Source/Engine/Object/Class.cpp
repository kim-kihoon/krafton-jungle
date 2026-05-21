#include "Class.h"
#include "Object/Object.h"

UClass::UClass(const char* InName, UClass* InSuperClass, size_t InClassSize, uint32 InClassFlags, FCreateObjectFunc InCreateFunc)
    : Name(InName), SuperClass(InSuperClass), ClassSize(InClassSize), ClassFlags(InClassFlags), CreateFunc(InCreateFunc)
{
}

bool UClass::IsChildOf(const UClass* Other) const
{
    if (!Other)
    {
        return false;
    }

    for (const UClass* Class = this; Class; Class = Class->SuperClass)
    {
        if (Class == Other)
        {
            return true;
        }
    }

    return false;
}

UObject* UClass::CreateObject() const
{
    return CreateFunc ? CreateFunc() : nullptr;
}

void UClass::GetAllProperties(TArray<const FProperty*>& OutProperties) const
{
    if (SuperClass)
    {
        SuperClass->GetAllProperties(OutProperties);
    }

    for (const std::unique_ptr<FProperty>& Property : Properties)
    {
        if (Property)
        {
            OutProperties.push_back(Property.get());
        }
    }
}

void UClass::GetAllFuntions(TArray<const UFunction*>& OutFuntions) const
{
    if (SuperClass)
    {
        SuperClass->GetAllFuntions(OutFuntions);
    }

    for (const std::unique_ptr<UFunction>& Function : Functions)
    {
        if (Function)
        {
            OutFuntions.push_back(Function.get());
        }
    }
}

void UClass::AddProperty(std::unique_ptr<FProperty> Property)
{
    if (!Property || !Property->Name)
    {
        return;
    }

    for (const std::unique_ptr<FProperty>& ExistingProperty : Properties)
    {
        if (ExistingProperty && ExistingProperty->Name && std::strcmp(ExistingProperty->Name, Property->Name) == 0)
        {
            return;
        }
    }

    const size_t ExpectedSize = Property->GetValueSize();
    if (ExpectedSize != 0 && Property->Size != ExpectedSize)
    {
        return;
    }

    Properties.push_back(std::move(Property));
}

const FProperty* UClass::FindProperty(const char* PropertyName) const
{
    if (!PropertyName)
    {
        return nullptr;
    }

    for (const std::unique_ptr<FProperty>& Property : Properties)
    {
        if (Property && Property->Name && std::strcmp(Property->Name, PropertyName) == 0)
        {
            return Property.get();
        }
    }

    return SuperClass ? SuperClass->FindProperty(PropertyName) : nullptr;
}


void UClass::AddFunction(std::unique_ptr<UFunction> Function)
{
    if (!Function || !Function->GetName())
    {
        return;
    }

    for (const std::unique_ptr<UFunction>& ExistingFunction : Functions)
    {
        if (ExistingFunction && ExistingFunction->GetName() && std::strcmp(ExistingFunction->GetName(), Function->GetName()) == 0)
        {
            return;
        }
    }

    Functions.push_back(std::move(Function));
}

const UFunction* UClass::FindFunction(const char* FunctionName) const
{
    if (!FunctionName)
    {
        return nullptr;
    }

    for (const std::unique_ptr<UFunction>& ExistingFunction : Functions)
    {
        if (ExistingFunction && ExistingFunction->GetName() && std::strcmp(ExistingFunction->GetName(), FunctionName) == 0)
        {
            return ExistingFunction.get();
        }
    }

	return SuperClass ? SuperClass->FindFunction(FunctionName) : nullptr;
}
