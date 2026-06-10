#pragma once

#include "Materials/MaterialInstance.h"
#include "Source/Engine/Materials/MaterialInstanceDynamic.generated.h"

// Runtime-only material instance. Use this for hit flash, dissolve, animation-driven
// material values, timeline values, and graph parameters that must not mutate the shared asset.
UCLASS()
class UMaterialInstanceDynamic : public UMaterialInstance
{
public:
    GENERATED_BODY()
    ~UMaterialInstanceDynamic() override = default;

    static UMaterialInstanceDynamic* Create(UMaterial* InParent, UObject* InOwner = nullptr, const FString& DebugName = FString());

    bool IsDynamicMaterialInstance() const override
    {
        return true;
    }

    UObject* GetOwnerObject() const
    {
        return Owner;
    }

    bool SetScalarParameterValue(const FString& ParamName, float Value)
    {
        return SetScalarParameter(ParamName, Value);
    }

    bool SetVector3ParameterValue(const FString& ParamName, const FVector& Value)
    {
        return SetVector3Parameter(ParamName, Value);
    }

    bool SetVectorParameterValue(const FString& ParamName, const FVector4& Value)
    {
        return SetVector4Parameter(ParamName, Value);
    }

    bool SetTextureParameterValue(const FString& ParamName, UTexture2D* Texture)
    {
        return SetTextureParameter(ParamName, Texture);
    }

    void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
    UObject* Owner = nullptr;
};