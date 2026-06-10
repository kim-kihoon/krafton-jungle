#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Object/FName.h"

class FArchive;

// Lua Blueprint 는 별도 VM 을 만들지 않고, 시각 그래프를 Lua source 로 컴파일해
// 기존 FLuaScriptManager/sol 런타임에서 실행한다. 이 타입들은 에디터 그래프와
// 컴파일러가 공유하는 순수 데이터 모델이다.

enum class ELuaBlueprintPinKind : uint8
{
    Input,
    Output
};

enum class ELuaBlueprintPinType : uint8
{
    Exec,
    Bool,
    Int,
    Float,
    String,
    Vector,
    Object,
    Any,
    Array,
    Actor,
    Pawn,
    PlayerController,
    ActorComponent,
    SceneComponent,
    PrimitiveComponent,
    Rotator,
    LinearColor,
    Vector4,
    Matrix,
    Class,
    Enum,
    Name,
    StaticMeshComponent,
    SkinnedMeshComponent,
    SkeletalMeshComponent,
    CameraComponent,
    CineCameraComponent,
    Material,
    Texture,
    AnimInstance,
    StaticMesh,
    LuaBlueprintComponent,
    LuaScriptComponent,
    LuaFunction
};

enum class ELuaBlueprintNodeType : uint8
{
    EventBeginPlay,
    EventTick,
    EventEndPlay,
    EventOverlap,
    EventEndOverlap,
    EventHit,
    EventEndHit,
    Sequence,
    Branch,
    ForLoop,
    WhileLoop,
    PrintString,
    LiteralBool,
    LiteralInt,
    LiteralFloat,
    LiteralString,
    LiteralVector,
    GetVariable,
    SetVariable,
    GetProperty,
    SetProperty,
    CallFunction,
    CallFunctionSignature,
    Self,
    // Float math
    AddFloat,
    SubtractFloat,
    MultiplyFloat,
    DivideFloat,
    // Int math
    AddInt,
    SubtractInt,
    MultiplyInt,
    DivideInt,
    ModInt,
    // Float compare
    EqualFloat,
    NotEqualFloat,
    LessFloat,
    GreaterFloat,
    LessEqualFloat,
    GreaterEqualFloat,
    // Int compare
    EqualInt,
    NotEqualInt,
    LessInt,
    GreaterInt,
    // Bool ops
    And,
    Or,
    Not,
    // String
    AppendString,
    // Vector
    MakeVector,
    BreakVector,
    AddVector,
    SubtractVector,
    ScaleVector,
    DotVector,
    CrossVector,
    VectorLength,
    NormalizeVector,
    // ── Actor ──
    SpawnActor,
    SpawnPawn,
    SpawnPawnAndPossess,
    DestroyActor,
    FindActorByName,
    FindActorByClass,
    FindActorByTag,
    FindActorsByTag,
    FindActorsByClass,
    GetActorLocation,
    SetActorLocation,
    GetActorRotation,
    SetActorRotation,
    GetActorScale,
    SetActorScale,
    GetActorForward,
    GetActorRight,
    AddActorWorldOffset,
    ActorHasTag,
    ActorAddTag,
    ActorRemoveTag,
    GetActorName,
    GetOwnerActor,
    AddForceToRoot,
    AddTorqueToRoot,
    AddImpulseToRoot,
    GetRootLinearVelocity,
    SetRootLinearVelocity,
    SetRootSimulatePhysics,
    // ── Object utility ──
    IsValid,
    Cast,
    // ── Component ──
    GetRootComponent,
    GetRootPrimitiveComponent,
    GetComponentByName,
    GetPrimitiveComponent,
    ActivateComponent,
    DeactivateComponent,
    AddForce,
    AddTorque,
    GetLinearVelocity,
    SetLinearVelocity,
    GetMass,
    SetSimulatePhysics,
    AddImpulse,
    // ── Math util ──
    Lerp,
    Clamp,
    Min,
    Max,
    RandomFloat,
    RandomInt,
    Sin,
    Cos,
    Sqrt,
    AbsFloat,
    Floor,
    Ceil,
    Distance,
    // ── Time ──
    GetGameTime,
    // ── ForEach ──
    ForEachActorByClass,
    ForEachActorByTag,
    ForEachArray,
    // ── Graph utility ──
    Reroute,
    Comment,
    CustomEvent,
    CallCustomEvent,
    Delay,
    // ── Explicit conversion ──
    ToBool,
    ToInt,
    ToFloat,
    ToString,
    ToVector,
    // ── Delegates (Reflection event binding) ──
    BindEvent,
    UnbindEvent,
    HasEventBinding,
    EventInputAction,
    EventInputAxis,
    // ── Game framework ──
    GetPlayerController,
    GetController,
    GetControlledPawn,
    Possess,
    UnPossess,
    IsPawnPossessed,
    GetInputComponent,
    // ── Vehicle ──
    VehicleSetThrottle,
    VehicleSetBrake,
    VehicleSetSteering,
    VehicleSetHandbrake,
    VehicleReset,
    VehicleGetForwardSpeed,
    // ── Particle ──
    ParticleSetTemplateByPath,
    ParticleActivate,
    ParticleDeactivate,
    ParticleReset,
    ParticleRebuild,
    ParticleSetLOD,
    // ── Timer ──
    SetTimer,
    ClearTimer,
    IsTimerActive,
    SetTimerForNextTick,
    // ── Trace / Collision query ──
    LineTrace,
    // ── UI ──
    CreateWidget,
    AddWidgetToViewport,
    RemoveWidgetFromParent,
    SetWidgetText,
    BindWidgetClick,
    // ── Audio ──
    LoadAudio,
    AudioPlaySound,
    PlayBGM,
    StopBGM,
    PlayAudioLoop,
    StopAudioLoop,
    SetAudioMasterVolume,
    // ── Skeletal mesh / animation ──
    GetSkeletalMeshComponent,
    SetSkeletalMeshByPath,
    ClearSkeletalMesh,
    PlayAnimationByPath,
    StopAnimation,
    SetAnimationByPath,
    SetAnimationPlayRate,
    SetAnimationLooping,
    SetAnimationPlaying,
    GetAnimInstance,
    // ── Materials ──
    LoadMaterial,
    GetMaterial,
    SetMaterial,
    SetMaterialByPath,
    CreateDynamicMaterialInstance,
    SetMaterialScalarParameter,
    SetMaterialVectorParameter,
    SetMaterialColorParameter,
    SetMaterialTextureParameter,
    LoadTexture,
    // ── Camera / cinematic ──
    GetCameraComponent,
    GetActiveCamera,
    PossessCamera,
    SetActiveCameraWithBlend,
    SetViewTargetWithBlend,
    SetCameraFOV,
    CameraLookAt,
    FadeIn,
    FadeOut,
    SetVignette,
    ClearVignette,
    StartCameraShakeAsset,
    SetDepthOfField,
    SetBokeh,
    ClearDepthOfField,
    SetLetterbox,
    ClearLetterbox,
    // ── Static mesh ──
    LoadStaticMesh,
    GetStaticMeshComponent,
    GetStaticMesh,
    SetStaticMesh,
    SetStaticMeshByPath,
    ClearStaticMesh,
    // ── Script / Blueprint invocation ──
    GetLuaBlueprintComponent,
    GetLuaScriptComponent,
    CallLuaBlueprintFunction,
    CallLuaScriptFunction,
    CallLuaBlueprintFileFunction,
    CallLuaScriptFileFunction,
    // ── User-authored Lua functions ──
    CustomLuaFunction,
    CallCustomLuaFunction,
    // ── Late lifecycle events (appended to preserve serialized enum compatibility) ──
    EventPostBeginPlay,
    EventPostStartMatch,
    EventPlayerCameraReady,
    // ── AnimGraph variable bridge (appended to preserve serialized enum compatibility) ──
    SetAnimGraphVariableFloat,
    SetAnimGraphVariableBool,
    SetAnimGraphVariableInt,
    GetAnimGraphVariableFloat,
    GetAnimGraphVariableBool,
    GetAnimGraphVariableInt,
    // ── Scene component socket nodes (appended to preserve serialized enum compatibility) ──
    AttachToComponent,
    GetAttachSocketName,
    HasSocket,
    GetSocketWorldLocation,
    GetSocketWorldRotation,
    GetSocketWorldScale,
    GetSocketForwardVector,
    GetSocketRightVector,
    GetSocketUpVector,
    Count
};

enum class ELuaBlueprintDiagnosticSeverity : uint8
{
    Info,
    Warning,
    Error
};

struct FLuaBlueprintPin
{
    uint32               PinId        = 0;
    uint32               OwningNodeId = 0;
    ELuaBlueprintPinKind Kind         = ELuaBlueprintPinKind::Input;
    ELuaBlueprintPinType Type         = ELuaBlueprintPinType::Exec;
    FName                DisplayName;

    // 실제 Blueprint 처럼, 연결이 없는 데이터 input pin 은 inline literal 을 가질 수 있다.
    // 컴파일러는 link 가 없으면 이 값을 사용한다.
    bool    DefaultBool  = false;
    int32   DefaultInt   = 0;
    float   DefaultFloat = 0.0f;
    FString DefaultString;
    FVector DefaultVector;

    friend FArchive& operator<<(FArchive& Ar, FLuaBlueprintPin& Pin);
};

struct FLuaBlueprintLink
{
    uint32 LinkId    = 0;
    uint32 FromPinId = 0; // Output pin
    uint32 ToPinId   = 0; // Input pin

    friend FArchive& operator<<(FArchive& Ar, FLuaBlueprintLink& Link);
};

struct FLuaBlueprintVariable
{
    FName                Name;
    ELuaBlueprintPinType Type          = ELuaBlueprintPinType::Float;
    bool                 bStrongObject = false; // true 면 component 쪽에서 강한 UObject 변수로 승격 가능
    bool                 BoolValue     = false;
    int32                IntValue      = 0;
    float                FloatValue    = 0.0f;
    FString              StringValue;
    FVector              VectorValue;

    friend FArchive& operator<<(FArchive& Ar, FLuaBlueprintVariable& Variable);
};

struct FLuaBlueprintNode
{
    uint32                   NodeId = 0;
    ELuaBlueprintNodeType    Type   = ELuaBlueprintNodeType::PrintString;
    FName                    DisplayName;
    float                    PosX = 0.0f;
    float                    PosY = 0.0f;
    TArray<FLuaBlueprintPin> Pins;

    // 노드별 공용 payload.
    // Variable: NameValue, Property: NameValue, Function: NameValue/StringValue(signature), Literal: *Value.
    FName   NameValue;
    FString StringValue;
    bool    BoolValue  = false;
    int32   IntValue   = 0;
    float   FloatValue = 0.0f;
    FVector VectorValue;

    // Editor/runtime debugger state. Serialized separately by ULuaBlueprintAsset so old assets remain readable.
    bool bBreakpointEnabled = false;

    friend FArchive& operator<<(FArchive& Ar, FLuaBlueprintNode& Node);
};

struct FLuaBlueprintDiagnostic
{
    ELuaBlueprintDiagnosticSeverity Severity = ELuaBlueprintDiagnosticSeverity::Info;
    uint32                          NodeId   = 0;
    FString                         Message;

    friend FArchive& operator<<(FArchive& Ar, FLuaBlueprintDiagnostic& Diagnostic);
};