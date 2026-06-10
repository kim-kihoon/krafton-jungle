#pragma once

#include "Core/Types/CoreTypes.h"
#include "Materials/MaterialDomain.h"
#include "Materials/Graph/MaterialGraphTypes.h"
#include "Math/Vector.h"
#include "Serialization/Archive.h"

// SourceKind 는 material 이 어떤 authoring source 에 의해 관리되는지 나타낸다.
// 기존 asset 호환을 위해 CustomShader flag 는 남겨두되, 새 graph material 은 별도 source kind 로 취급한다.
enum class EMaterialSourceKind : uint8
{
    EngineDefault,
    CustomShader,
    Graph,
};

enum class EMaterialShadingModel : uint8
{
    Unlit,
    DefaultLit,
    Phong,
    Lambert,
};

enum class EMaterialValueType : uint8
{
    Float,
    Float2,
    Float3,
    Float4,
    Color,
    Bool,
    Texture2D,
};

enum class EMaterialDynamicValueSource : uint8
{
    Constant,
    MaterialParameter,
    MaterialInstanceOverride,
    DynamicInstanceOverride,
    FrameTime,
    CameraWorldPosition,
    ObjectColor,
    ObjectPosition,
    ObjectCustomData,
    VertexColor,
    ParticleColor,
    ParticleSubImageIndex,
    ParticleDynamicParameter,
};

struct FMaterialStableId
{
    uint64 A = 0;
    uint64 B = 0;

    bool IsValid() const
    {
        return A != 0 || B != 0;
    }

    bool operator==(const FMaterialStableId& Other) const
    {
        return A == Other.A && B == Other.B;
    }

    bool operator!=(const FMaterialStableId& Other) const
    {
        return !(*this == Other);
    }

    friend FArchive& operator<<(FArchive& Ar, FMaterialStableId& Id)
    {
        Ar << Id.A;
        Ar << Id.B;
        return Ar;
    }
};

struct FMaterialSettings
{
    EMaterialDomain       Domain       = EMaterialDomain::Surface;
    EBlendMode            BlendMode    = EBlendMode::Opaque;
    EMaterialShadingModel ShadingModel = EMaterialShadingModel::Unlit;

    bool bTwoSided        = false;
    bool bReceiveLighting = false;
    bool bCastShadow      = true;

    float OpacityMaskClipValue = 0.333f;

    friend FArchive& operator<<(FArchive& Ar, FMaterialSettings& Settings)
    {
        Ar << Settings.Domain;
        Ar << Settings.BlendMode;
        Ar << Settings.ShadingModel;
        Ar << Settings.bTwoSided;
        Ar << Settings.bReceiveLighting;
        Ar << Settings.bCastShadow;
        Ar << Settings.OpacityMaskClipValue;
        return Ar;
    }
};

struct FMaterialParameterDefinition
{
    FMaterialStableId  Id;
    FString            Name;
    EMaterialValueType Type = EMaterialValueType::Float4;

    FVector4 DefaultValue = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
    FString  DefaultTexturePath;

    FString Group;
    int32   SortPriority = 0;

    bool bExposeToInstance = true;
    bool bRuntimeEditable  = true;

    bool  bUseSlider = false;
    float SliderMin  = 0.0f;
    float SliderMax  = 1.0f;

    friend FArchive& operator<<(FArchive& Ar, FMaterialParameterDefinition& Definition)
    {
        Ar << Definition.Id;
        Ar << Definition.Name;
        Ar << Definition.Type;
        Ar << Definition.DefaultValue;
        Ar << Definition.DefaultTexturePath;
        Ar << Definition.Group;
        Ar << Definition.SortPriority;
        Ar << Definition.bExposeToInstance;
        Ar << Definition.bRuntimeEditable;
        Ar << Definition.bUseSlider;
        Ar << Definition.SliderMin;
        Ar << Definition.SliderMax;
        return Ar;
    }
};

struct FMaterialParameterValue
{
    FMaterialStableId  ParameterId;
    FString            FallbackName;
    EMaterialValueType Type = EMaterialValueType::Float4;

    FVector4 Value = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
    FString  TexturePath;

    friend FArchive& operator<<(FArchive& Ar, FMaterialParameterValue& ParameterValue)
    {
        Ar << ParameterValue.ParameterId;
        Ar << ParameterValue.FallbackName;
        Ar << ParameterValue.Type;
        Ar << ParameterValue.Value;
        Ar << ParameterValue.TexturePath;
        return Ar;
    }
};

struct FMaterialRuntimeParameterStore
{
    TArray<FMaterialParameterValue> Values;

    FMaterialParameterValue* FindByName(const FString& Name)
    {
        for (FMaterialParameterValue& Value : Values)
        {
            if (Value.FallbackName == Name)
            {
                return &Value;
            }
        }
        return nullptr;
    }

    const FMaterialParameterValue* FindByName(const FString& Name) const
    {
        for (const FMaterialParameterValue& Value : Values)
        {
            if (Value.FallbackName == Name)
            {
                return &Value;
            }
        }
        return nullptr;
    }

    void SetValue(const FString& Name, EMaterialValueType Type, const FVector4& InValue, const FString& TexturePath = FString())
    {
        if (FMaterialParameterValue* Existing = FindByName(Name))
        {
            Existing->Type        = Type;
            Existing->Value       = InValue;
            Existing->TexturePath = TexturePath;
            return;
        }

        FMaterialParameterValue NewValue;
        NewValue.FallbackName = Name;
        NewValue.Type         = Type;
        NewValue.Value        = InValue;
        NewValue.TexturePath  = TexturePath;
        Values.push_back(NewValue);
    }

    friend FArchive& operator<<(FArchive& Ar, FMaterialRuntimeParameterStore& Store)
    {
        Ar << Store.Values;
        return Ar;
    }
};

struct FMaterialGraphDocument
{
    bool                 bEnabled = false;
    EMaterialGraphTarget Target   = EMaterialGraphTarget::Surface;
    FMaterialGraph       Graph;

    // ax::NodeEditor 의 view state (zoom/pan/selection) — 원래 디스크의 JSON 파일에 저장되던 것을
    // 머티리얼 .uasset 안으로 가져온 것. 빈 문자열이면 NavigateToContent 가 첫 프레임에 fit 한다.
    FString EditorSettings;

    FString LastSavedGraphHash;
    FString LastCompiledGraphHash;
    FString LastCompiledShaderPath;
    FString LastCompileError;

    bool bAutoPreview  = false;
    bool bPreviewDirty = false;

    friend FArchive& operator<<(FArchive& Ar, FMaterialGraphDocument& Document)
    {
        Ar << Document.bEnabled;
        Ar << Document.Target;
        Ar << Document.Graph;
        Ar << Document.EditorSettings;
        Ar << Document.LastSavedGraphHash;
        Ar << Document.LastCompiledGraphHash;
        Ar << Document.LastCompiledShaderPath;
        Ar << Document.LastCompileError;
        Ar << Document.bAutoPreview;
        Ar << Document.bPreviewDirty;
        return Ar;
    }
};

struct FMaterialCompileRecord
{
    FString SourceHash;
    FString CompilerVersion;
    FString GeneratedShaderPath;
    FString ErrorSummary;
    bool    bSucceeded = false;

    friend FArchive& operator<<(FArchive& Ar, FMaterialCompileRecord& Record)
    {
        Ar << Record.SourceHash;
        Ar << Record.CompilerVersion;
        Ar << Record.GeneratedShaderPath;
        Ar << Record.ErrorSummary;
        Ar << Record.bSucceeded;
        return Ar;
    }
};