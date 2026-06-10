#include "LuaBlueprint/LuaBlueprintAsset.h"
#include "Input/InputKeyCodes.h"

#include "Core/Types/PropertyTypes.h"
#include "LuaBlueprint/LuaBlueprintCompiler.h"
#include "Object/GarbageCollection.h"
#include "Object/Reflection/UClass.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cstring>

namespace
{
    constexpr uint32 LuaBlueprintAssetMagic         = 0x4C425031; // LBP1
    constexpr uint32 LuaBlueprintAssetFormatVersion = 5;
    constexpr uint32 LuaBlueprintCompilerVersion    = 21;
}

FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintPin>& Array);
FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintLink>& Array);
FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintVariable>& Array);
FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintNode>& Array);
FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintDiagnostic>& Array);

FArchive& operator<<(FArchive& Ar, FLuaBlueprintPin& Pin)
{
    Ar << Pin.PinId;
    Ar << Pin.OwningNodeId;
    Ar << Pin.Kind;
    Ar << Pin.Type;
    Ar << Pin.DisplayName;
    Ar << Pin.DefaultBool;
    Ar << Pin.DefaultInt;
    Ar << Pin.DefaultFloat;
    Ar << Pin.DefaultString;
    Ar << Pin.DefaultVector;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FLuaBlueprintLink& Link)
{
    Ar << Link.LinkId;
    Ar << Link.FromPinId;
    Ar << Link.ToPinId;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FLuaBlueprintVariable& Variable)
{
    Ar << Variable.Name;
    Ar << Variable.Type;
    Ar << Variable.bStrongObject;
    Ar << Variable.BoolValue;
    Ar << Variable.IntValue;
    Ar << Variable.FloatValue;
    Ar << Variable.StringValue;
    Ar << Variable.VectorValue;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FLuaBlueprintNode& Node)
{
    Ar << Node.NodeId;
    Ar << Node.Type;
    Ar << Node.DisplayName;
    Ar << Node.PosX;
    Ar << Node.PosY;
    Ar << Node.Pins;
    Ar << Node.NameValue;
    Ar << Node.StringValue;
    Ar << Node.BoolValue;
    Ar << Node.IntValue;
    Ar << Node.FloatValue;
    Ar << Node.VectorValue;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FLuaBlueprintDiagnostic& Diagnostic)
{
    Ar << Diagnostic.Severity;
    Ar << Diagnostic.NodeId;
    Ar << Diagnostic.Message;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintPin>& Array)
{
    uint32 N = static_cast<uint32>(Array.size());
    Ar << N;
    if (Ar.IsLoading()) Array.resize(N);
    for (auto& Item : Array) Ar << Item;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintLink>& Array)
{
    uint32 N = static_cast<uint32>(Array.size());
    Ar << N;
    if (Ar.IsLoading()) Array.resize(N);
    for (auto& Item : Array) Ar << Item;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintVariable>& Array)
{
    uint32 N = static_cast<uint32>(Array.size());
    Ar << N;
    if (Ar.IsLoading()) Array.resize(N);
    for (auto& Item : Array) Ar << Item;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintNode>& Array)
{
    uint32 N = static_cast<uint32>(Array.size());
    Ar << N;
    if (Ar.IsLoading()) Array.resize(N);
    for (auto& Item : Array) Ar << Item;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, TArray<FLuaBlueprintDiagnostic>& Array)
{
    uint32 N = static_cast<uint32>(Array.size());
    Ar << N;
    if (Ar.IsLoading()) Array.resize(N);
    for (auto& Item : Array) Ar << Item;
    return Ar;
}

namespace
{
    const char* NodeTypeDisplayName(ELuaBlueprintNodeType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintNodeType::EventBeginPlay:
            return "Event BeginPlay";
        case ELuaBlueprintNodeType::EventTick:
            return "Event Tick";
        case ELuaBlueprintNodeType::EventEndPlay:
            return "Event EndPlay";
        case ELuaBlueprintNodeType::EventOverlap:
            return "Event Overlap";
        case ELuaBlueprintNodeType::EventEndOverlap:
            return "Event EndOverlap";
        case ELuaBlueprintNodeType::EventHit:
            return "Event Hit";
        case ELuaBlueprintNodeType::EventEndHit:
            return "Event EndHit";
        case ELuaBlueprintNodeType::EventPostBeginPlay:
            return "Event PostBeginPlay";
        case ELuaBlueprintNodeType::EventPostStartMatch:
            return "Event PostStartMatch";
        case ELuaBlueprintNodeType::EventPlayerCameraReady:
            return "Event OnPlayerCameraReady";
        case ELuaBlueprintNodeType::EventInputAction:
            return "Event InputAction";
        case ELuaBlueprintNodeType::EventInputAxis:
            return "Event InputAxis";
        case ELuaBlueprintNodeType::Sequence:
            return "Sequence";
        case ELuaBlueprintNodeType::Branch:
            return "Branch";
        case ELuaBlueprintNodeType::ForLoop:
            return "For Loop";
        case ELuaBlueprintNodeType::WhileLoop:
            return "While Loop";
        case ELuaBlueprintNodeType::PrintString:
            return "Print String";
        case ELuaBlueprintNodeType::LiteralBool:
            return "Bool";
        case ELuaBlueprintNodeType::LiteralInt:
            return "Int";
        case ELuaBlueprintNodeType::LiteralFloat:
            return "Float";
        case ELuaBlueprintNodeType::LiteralString:
            return "String";
        case ELuaBlueprintNodeType::LiteralVector:
            return "Vector";
        case ELuaBlueprintNodeType::GetVariable:
            return "Get Variable";
        case ELuaBlueprintNodeType::SetVariable:
            return "Set Variable";
        case ELuaBlueprintNodeType::GetProperty:
            return "Get Property";
        case ELuaBlueprintNodeType::SetProperty:
            return "Set Property";
        case ELuaBlueprintNodeType::CallFunction:
            return "Call Function";
        case ELuaBlueprintNodeType::CallFunctionSignature:
            return "Call Signature";
        case ELuaBlueprintNodeType::Self:
            return "Self (Owning Actor)";
        case ELuaBlueprintNodeType::AddFloat:
            return "Float + Float";
        case ELuaBlueprintNodeType::SubtractFloat:
            return "Float - Float";
        case ELuaBlueprintNodeType::MultiplyFloat:
            return "Float * Float";
        case ELuaBlueprintNodeType::DivideFloat:
            return "Float / Float";
        case ELuaBlueprintNodeType::AddInt:
            return "Int + Int";
        case ELuaBlueprintNodeType::SubtractInt:
            return "Int - Int";
        case ELuaBlueprintNodeType::MultiplyInt:
            return "Int * Int";
        case ELuaBlueprintNodeType::DivideInt:
            return "Int / Int";
        case ELuaBlueprintNodeType::ModInt:
            return "Int % Int";
        case ELuaBlueprintNodeType::EqualFloat:
            return "Float == Float";
        case ELuaBlueprintNodeType::NotEqualFloat:
            return "Float != Float";
        case ELuaBlueprintNodeType::LessFloat:
            return "Float < Float";
        case ELuaBlueprintNodeType::GreaterFloat:
            return "Float > Float";
        case ELuaBlueprintNodeType::LessEqualFloat:
            return "Float <= Float";
        case ELuaBlueprintNodeType::GreaterEqualFloat:
            return "Float >= Float";
        case ELuaBlueprintNodeType::EqualInt:
            return "Int == Int";
        case ELuaBlueprintNodeType::NotEqualInt:
            return "Int != Int";
        case ELuaBlueprintNodeType::LessInt:
            return "Int < Int";
        case ELuaBlueprintNodeType::GreaterInt:
            return "Int > Int";
        case ELuaBlueprintNodeType::And:
            return "Bool AND Bool";
        case ELuaBlueprintNodeType::Or:
            return "Bool OR Bool";
        case ELuaBlueprintNodeType::Not:
            return "NOT Bool";
        case ELuaBlueprintNodeType::AppendString:
            return "Append String";
        case ELuaBlueprintNodeType::MakeVector:
            return "Make Vector";
        case ELuaBlueprintNodeType::BreakVector:
            return "Break Vector";
        case ELuaBlueprintNodeType::AddVector:
            return "Vector + Vector";
        case ELuaBlueprintNodeType::SubtractVector:
            return "Vector - Vector";
        case ELuaBlueprintNodeType::ScaleVector:
            return "Vector * Float";
        case ELuaBlueprintNodeType::DotVector:
            return "Dot";
        case ELuaBlueprintNodeType::CrossVector:
            return "Cross";
        case ELuaBlueprintNodeType::VectorLength:
            return "Vector Length";
        case ELuaBlueprintNodeType::NormalizeVector:
            return "Normalize";
        case ELuaBlueprintNodeType::SpawnActor:
            return "Spawn Actor";
        case ELuaBlueprintNodeType::SpawnPawn:
            return "Spawn Pawn";
        case ELuaBlueprintNodeType::SpawnPawnAndPossess:
            return "Spawn Pawn and Possess";
        case ELuaBlueprintNodeType::DestroyActor:
            return "Destroy Actor";
        case ELuaBlueprintNodeType::FindActorByName:
            return "Find Actor by Name";
        case ELuaBlueprintNodeType::FindActorByClass:
            return "Find Actor of Class";
        case ELuaBlueprintNodeType::FindActorByTag:
            return "Find Actor with Tag";
        case ELuaBlueprintNodeType::FindActorsByTag:
            return "Find Actors with Tag";
        case ELuaBlueprintNodeType::FindActorsByClass:
            return "Find Actors of Class";
        case ELuaBlueprintNodeType::GetActorLocation:
            return "Get Actor Location";
        case ELuaBlueprintNodeType::SetActorLocation:
            return "Set Actor Location";
        case ELuaBlueprintNodeType::GetActorRotation:
            return "Get Actor Rotation";
        case ELuaBlueprintNodeType::SetActorRotation:
            return "Set Actor Rotation";
        case ELuaBlueprintNodeType::GetActorScale:
            return "Get Actor Scale";
        case ELuaBlueprintNodeType::SetActorScale:
            return "Set Actor Scale";
        case ELuaBlueprintNodeType::GetActorForward:
            return "Get Actor Forward";
        case ELuaBlueprintNodeType::GetActorRight:
            return "Get Actor Right";
        case ELuaBlueprintNodeType::AddActorWorldOffset:
            return "Add Actor World Offset";
        case ELuaBlueprintNodeType::ActorHasTag:
            return "Actor Has Tag";
        case ELuaBlueprintNodeType::ActorAddTag:
            return "Actor Add Tag";
        case ELuaBlueprintNodeType::ActorRemoveTag:
            return "Actor Remove Tag";
        case ELuaBlueprintNodeType::GetActorName:
            return "Get Actor Name";
        case ELuaBlueprintNodeType::GetOwnerActor:
            return "Get Owner Actor";
        case ELuaBlueprintNodeType::GetPlayerController:
            return "Get Player Controller";
        case ELuaBlueprintNodeType::GetController:
            return "Get Controller";
        case ELuaBlueprintNodeType::GetControlledPawn:
            return "Get Controlled Pawn";
        case ELuaBlueprintNodeType::Possess:
            return "Possess";
        case ELuaBlueprintNodeType::UnPossess:
            return "UnPossess";
        case ELuaBlueprintNodeType::IsPawnPossessed:
            return "Is Pawn Possessed";
        case ELuaBlueprintNodeType::GetInputComponent:
            return "Get Input Component";
        case ELuaBlueprintNodeType::VehicleSetThrottle:
            return "Vehicle Set Throttle";
        case ELuaBlueprintNodeType::VehicleSetBrake:
            return "Vehicle Set Brake";
        case ELuaBlueprintNodeType::VehicleSetSteering:
            return "Vehicle Set Steering";
        case ELuaBlueprintNodeType::VehicleSetHandbrake:
            return "Vehicle Set Handbrake";
        case ELuaBlueprintNodeType::VehicleReset:
            return "Vehicle Reset";
        case ELuaBlueprintNodeType::VehicleGetForwardSpeed:
            return "Vehicle Get Forward Speed";
        case ELuaBlueprintNodeType::ParticleSetTemplateByPath:
            return "Particle Set Template By Path";
        case ELuaBlueprintNodeType::ParticleActivate:
            return "Particle Activate";
        case ELuaBlueprintNodeType::ParticleDeactivate:
            return "Particle Deactivate";
        case ELuaBlueprintNodeType::ParticleReset:
            return "Particle Reset";
        case ELuaBlueprintNodeType::ParticleRebuild:
            return "Particle Rebuild";
        case ELuaBlueprintNodeType::ParticleSetLOD:
            return "Particle Set LOD";
        case ELuaBlueprintNodeType::IsValid:
            return "Is Valid";
        case ELuaBlueprintNodeType::Cast:
            return "Cast";
        case ELuaBlueprintNodeType::GetRootComponent:
            return "Get Root Component";
        case ELuaBlueprintNodeType::GetRootPrimitiveComponent:
            return "Get Root Primitive Component";
        case ELuaBlueprintNodeType::GetComponentByName:
            return "Get Component by Name";
        case ELuaBlueprintNodeType::GetPrimitiveComponent:
            return "Get Primitive Component";
        case ELuaBlueprintNodeType::ActivateComponent:
            return "Activate";
        case ELuaBlueprintNodeType::DeactivateComponent:
            return "Deactivate";
        case ELuaBlueprintNodeType::AddForce:
            return "Add Force";
        case ELuaBlueprintNodeType::AddTorque:
            return "Add Torque";
        case ELuaBlueprintNodeType::AddImpulse:
            return "Add Impulse";
        case ELuaBlueprintNodeType::GetLinearVelocity:
            return "Get Linear Velocity";
        case ELuaBlueprintNodeType::SetLinearVelocity:
            return "Set Linear Velocity";
        case ELuaBlueprintNodeType::GetMass:
            return "Get Mass";
        case ELuaBlueprintNodeType::SetSimulatePhysics:
            return "Set Simulate Physics";
        case ELuaBlueprintNodeType::Lerp:
            return "Lerp";
        case ELuaBlueprintNodeType::Clamp:
            return "Clamp";
        case ELuaBlueprintNodeType::Min:
            return "Min";
        case ELuaBlueprintNodeType::Max:
            return "Max";
        case ELuaBlueprintNodeType::RandomFloat:
            return "Random Float";
        case ELuaBlueprintNodeType::RandomInt:
            return "Random Int";
        case ELuaBlueprintNodeType::Sin:
            return "Sin";
        case ELuaBlueprintNodeType::Cos:
            return "Cos";
        case ELuaBlueprintNodeType::Sqrt:
            return "Sqrt";
        case ELuaBlueprintNodeType::AbsFloat:
            return "Abs";
        case ELuaBlueprintNodeType::Floor:
            return "Floor";
        case ELuaBlueprintNodeType::Ceil:
            return "Ceil";
        case ELuaBlueprintNodeType::Distance:
            return "Distance";
        case ELuaBlueprintNodeType::GetGameTime:
            return "Get Game Time";
        case ELuaBlueprintNodeType::ForEachActorByClass:
            return "For Each Actor (Class)";
        case ELuaBlueprintNodeType::ForEachActorByTag:
            return "For Each Actor (Tag)";
        case ELuaBlueprintNodeType::ForEachArray:
            return "For Each Array";
        case ELuaBlueprintNodeType::Reroute:
            return "Reroute";
        case ELuaBlueprintNodeType::Comment:
            return "Comment";
        case ELuaBlueprintNodeType::CustomEvent:
            return "Custom Event";
        case ELuaBlueprintNodeType::CallCustomEvent:
            return "Call Custom Event";
        case ELuaBlueprintNodeType::Delay:
            return "Delay";
        case ELuaBlueprintNodeType::ToBool:
            return "To Bool";
        case ELuaBlueprintNodeType::ToInt:
            return "To Int";
        case ELuaBlueprintNodeType::ToFloat:
            return "To Float";
        case ELuaBlueprintNodeType::ToString:
            return "To String";
        case ELuaBlueprintNodeType::ToVector:
            return "To Vector";
        case ELuaBlueprintNodeType::BindEvent:
            return "Bind Event";
        case ELuaBlueprintNodeType::UnbindEvent:
            return "Unbind Event";
        case ELuaBlueprintNodeType::HasEventBinding:
            return "Has Event Binding";
        case ELuaBlueprintNodeType::SetTimer:
            return "Set Timer";
        case ELuaBlueprintNodeType::ClearTimer:
            return "Clear Timer";
        case ELuaBlueprintNodeType::IsTimerActive:
            return "Is Timer Active";
        case ELuaBlueprintNodeType::SetTimerForNextTick:
            return "Set Timer For Next Tick";
        case ELuaBlueprintNodeType::LineTrace:
            return "Line Trace";
        case ELuaBlueprintNodeType::CreateWidget:
            return "Create Widget";
        case ELuaBlueprintNodeType::AddWidgetToViewport:
            return "Add Widget To Viewport";
        case ELuaBlueprintNodeType::RemoveWidgetFromParent:
            return "Remove Widget From Parent";
        case ELuaBlueprintNodeType::SetWidgetText:
            return "Set Widget Text";
        case ELuaBlueprintNodeType::BindWidgetClick:
            return "Bind Widget Click";
        case ELuaBlueprintNodeType::LoadAudio:
            return "Load Audio";
        case ELuaBlueprintNodeType::AudioPlaySound:
            return "Play Sound";
        case ELuaBlueprintNodeType::PlayBGM:
            return "Play BGM";
        case ELuaBlueprintNodeType::StopBGM:
            return "Stop BGM";
        case ELuaBlueprintNodeType::PlayAudioLoop:
            return "Play Audio Loop";
        case ELuaBlueprintNodeType::StopAudioLoop:
            return "Stop Audio Loop";
        case ELuaBlueprintNodeType::SetAudioMasterVolume:
            return "Set Audio Master Volume";
        case ELuaBlueprintNodeType::LoadStaticMesh:
            return "Load Static Mesh";
        case ELuaBlueprintNodeType::GetStaticMeshComponent:
            return "Get Static Mesh Component";
        case ELuaBlueprintNodeType::GetStaticMesh:
            return "Get Static Mesh";
        case ELuaBlueprintNodeType::SetStaticMesh:
            return "Set Static Mesh";
        case ELuaBlueprintNodeType::SetStaticMeshByPath:
            return "Set Static Mesh By Path";
        case ELuaBlueprintNodeType::ClearStaticMesh:
            return "Clear Static Mesh";
        case ELuaBlueprintNodeType::GetLuaBlueprintComponent:
            return "Get Lua Blueprint Component";
        case ELuaBlueprintNodeType::GetLuaScriptComponent:
            return "Get Lua Script Component";
        case ELuaBlueprintNodeType::CallLuaBlueprintFunction:
            return "Call Lua Blueprint Function";
        case ELuaBlueprintNodeType::CallLuaScriptFunction:
            return "Call Lua Script Function";
        case ELuaBlueprintNodeType::CallLuaBlueprintFileFunction:
            return "Call Lua Blueprint File Function";
        case ELuaBlueprintNodeType::CallLuaScriptFileFunction:
            return "Call Lua Script File Function";
        case ELuaBlueprintNodeType::CustomLuaFunction:
            return "Custom Lua Function";
        case ELuaBlueprintNodeType::CallCustomLuaFunction:
            return "Call Custom Lua Function";
        case ELuaBlueprintNodeType::GetSkeletalMeshComponent:
            return "Get Skeletal Mesh Component";
        case ELuaBlueprintNodeType::SetSkeletalMeshByPath:
            return "Set Skeletal Mesh By Path";
        case ELuaBlueprintNodeType::ClearSkeletalMesh:
            return "Clear Skeletal Mesh";
        case ELuaBlueprintNodeType::PlayAnimationByPath:
            return "Play Animation By Path";
        case ELuaBlueprintNodeType::StopAnimation:
            return "Stop Animation";
        case ELuaBlueprintNodeType::SetAnimationByPath:
            return "Set Animation By Path";
        case ELuaBlueprintNodeType::SetAnimationPlayRate:
            return "Set Animation Play Rate";
        case ELuaBlueprintNodeType::SetAnimationLooping:
            return "Set Animation Looping";
        case ELuaBlueprintNodeType::SetAnimationPlaying:
            return "Set Animation Playing";
        case ELuaBlueprintNodeType::GetAnimInstance:
            return "Get Anim Instance";
        case ELuaBlueprintNodeType::SetAnimGraphVariableFloat:
            return "Set Anim Graph Float";
        case ELuaBlueprintNodeType::SetAnimGraphVariableBool:
            return "Set Anim Graph Bool";
        case ELuaBlueprintNodeType::SetAnimGraphVariableInt:
            return "Set Anim Graph Int";
        case ELuaBlueprintNodeType::GetAnimGraphVariableFloat:
            return "Get Anim Graph Float";
        case ELuaBlueprintNodeType::GetAnimGraphVariableBool:
            return "Get Anim Graph Bool";
        case ELuaBlueprintNodeType::GetAnimGraphVariableInt:
            return "Get Anim Graph Int";
        case ELuaBlueprintNodeType::AttachToComponent:
            return "Attach To Component";
        case ELuaBlueprintNodeType::GetAttachSocketName:
            return "Get Attach Socket Name";
        case ELuaBlueprintNodeType::HasSocket:
            return "Has Socket";
        case ELuaBlueprintNodeType::GetSocketWorldLocation:
            return "Get Socket World Location";
        case ELuaBlueprintNodeType::GetSocketWorldRotation:
            return "Get Socket World Rotation";
        case ELuaBlueprintNodeType::GetSocketWorldScale:
            return "Get Socket World Scale";
        case ELuaBlueprintNodeType::GetSocketForwardVector:
            return "Get Socket Forward Vector";
        case ELuaBlueprintNodeType::GetSocketRightVector:
            return "Get Socket Right Vector";
        case ELuaBlueprintNodeType::GetSocketUpVector:
            return "Get Socket Up Vector";
        case ELuaBlueprintNodeType::LoadMaterial:
            return "Load Material";
        case ELuaBlueprintNodeType::GetMaterial:
            return "Get Material";
        case ELuaBlueprintNodeType::SetMaterial:
            return "Set Material";
        case ELuaBlueprintNodeType::SetMaterialByPath:
            return "Set Material By Path";
        case ELuaBlueprintNodeType::CreateDynamicMaterialInstance:
            return "Create Dynamic Material Instance";
        case ELuaBlueprintNodeType::SetMaterialScalarParameter:
            return "Set Material Scalar Parameter";
        case ELuaBlueprintNodeType::SetMaterialVectorParameter:
            return "Set Material Vector Parameter";
        case ELuaBlueprintNodeType::SetMaterialColorParameter:
            return "Set Material Color Parameter";
        case ELuaBlueprintNodeType::SetMaterialTextureParameter:
            return "Set Material Texture Parameter";
        case ELuaBlueprintNodeType::LoadTexture:
            return "Load Texture";
        case ELuaBlueprintNodeType::GetCameraComponent:
            return "Get Camera Component";
        case ELuaBlueprintNodeType::GetActiveCamera:
            return "Get Active Camera";
        case ELuaBlueprintNodeType::PossessCamera:
            return "Possess Camera";
        case ELuaBlueprintNodeType::SetActiveCameraWithBlend:
            return "Set Active Camera With Blend";
        case ELuaBlueprintNodeType::SetViewTargetWithBlend:
            return "Set View Target With Blend";
        case ELuaBlueprintNodeType::SetCameraFOV:
            return "Set Camera FOV";
        case ELuaBlueprintNodeType::CameraLookAt:
            return "Camera Look At";
        case ELuaBlueprintNodeType::FadeIn:
            return "Fade In";
        case ELuaBlueprintNodeType::FadeOut:
            return "Fade Out";
        case ELuaBlueprintNodeType::SetVignette:
            return "Set Vignette";
        case ELuaBlueprintNodeType::ClearVignette:
            return "Clear Vignette";
        case ELuaBlueprintNodeType::StartCameraShakeAsset:
            return "Start Camera Shake Asset";
        case ELuaBlueprintNodeType::SetDepthOfField:
            return "Set Depth Of Field";
        case ELuaBlueprintNodeType::SetBokeh:
            return "Set Bokeh";
        case ELuaBlueprintNodeType::ClearDepthOfField:
            return "Clear Depth Of Field";
        case ELuaBlueprintNodeType::SetLetterbox:
            return "Set Letterbox";
        case ELuaBlueprintNodeType::ClearLetterbox:
            return "Clear Letterbox";
        }
        return "Node";
    }

    bool IsLuaBlueprintObjectLikePinType(ELuaBlueprintPinType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintPinType::Object:
        case ELuaBlueprintPinType::Actor:
        case ELuaBlueprintPinType::Pawn:
        case ELuaBlueprintPinType::PlayerController:
        case ELuaBlueprintPinType::ActorComponent:
        case ELuaBlueprintPinType::SceneComponent:
        case ELuaBlueprintPinType::PrimitiveComponent:
        case ELuaBlueprintPinType::StaticMesh:
        case ELuaBlueprintPinType::StaticMeshComponent:
        case ELuaBlueprintPinType::SkinnedMeshComponent:
        case ELuaBlueprintPinType::SkeletalMeshComponent:
        case ELuaBlueprintPinType::CameraComponent:
        case ELuaBlueprintPinType::CineCameraComponent:
        case ELuaBlueprintPinType::Material:
        case ELuaBlueprintPinType::Texture:
        case ELuaBlueprintPinType::AnimInstance:
        case ELuaBlueprintPinType::LuaBlueprintComponent:
        case ELuaBlueprintPinType::LuaScriptComponent:
        case ELuaBlueprintPinType::Class:
            return true;
        default:
            return false;
        }
    }

    ELuaBlueprintPinType LuaBlueprintPinTypeFromObjectClass(UClass* Class)
    {
        if (!Class)
        {
            return ELuaBlueprintPinType::Object;
        }

        auto IsClassOrChildOf = [Class](const char* ClassName) -> bool
        {
            UClass* BaseClass = UClass::FindByName(ClassName);
            return BaseClass && Class->IsA(BaseClass);
        };

        // Check more-derived classes before their bases so function return pins keep
        // the most useful LuaBlueprint type instead of degrading to Object.
        if (IsClassOrChildOf("UCineCameraComponent"))
        {
            return ELuaBlueprintPinType::CineCameraComponent;
        }
        if (IsClassOrChildOf("UCameraComponent"))
        {
            return ELuaBlueprintPinType::CameraComponent;
        }
        if (IsClassOrChildOf("USkeletalMeshComponent"))
        {
            return ELuaBlueprintPinType::SkeletalMeshComponent;
        }
        if (IsClassOrChildOf("USkinnedMeshComponent"))
        {
            return ELuaBlueprintPinType::SkinnedMeshComponent;
        }
        if (IsClassOrChildOf("UStaticMeshComponent"))
        {
            return ELuaBlueprintPinType::StaticMeshComponent;
        }
        if (IsClassOrChildOf("UPrimitiveComponent"))
        {
            return ELuaBlueprintPinType::PrimitiveComponent;
        }
        if (IsClassOrChildOf("USceneComponent"))
        {
            return ELuaBlueprintPinType::SceneComponent;
        }
        if (IsClassOrChildOf("ULuaBlueprintComponent"))
        {
            return ELuaBlueprintPinType::LuaBlueprintComponent;
        }
        if (IsClassOrChildOf("ULuaScriptComponent"))
        {
            return ELuaBlueprintPinType::LuaScriptComponent;
        }
        if (IsClassOrChildOf("UActorComponent"))
        {
            return ELuaBlueprintPinType::ActorComponent;
        }
        if (IsClassOrChildOf("APlayerController"))
        {
            return ELuaBlueprintPinType::PlayerController;
        }
        if (IsClassOrChildOf("APawn"))
        {
            return ELuaBlueprintPinType::Pawn;
        }
        if (IsClassOrChildOf("AActor"))
        {
            return ELuaBlueprintPinType::Actor;
        }
        if (IsClassOrChildOf("UAnimInstance"))
        {
            return ELuaBlueprintPinType::AnimInstance;
        }
        if (IsClassOrChildOf("UStaticMesh"))
        {
            return ELuaBlueprintPinType::StaticMesh;
        }
        if (IsClassOrChildOf("UMaterial"))
        {
            return ELuaBlueprintPinType::Material;
        }
        if (IsClassOrChildOf("UTexture2D"))
        {
            return ELuaBlueprintPinType::Texture;
        }

        return ELuaBlueprintPinType::Object;
    }

    ELuaBlueprintPinType LuaBlueprintPinTypeFromProperty(const FProperty* Property)
    {
        if (!Property)
        {
            return ELuaBlueprintPinType::Any;
        }

        switch (Property->GetType())
        {
        case EPropertyType::Bool:
        case EPropertyType::ByteBool:
            return ELuaBlueprintPinType::Bool;
        case EPropertyType::Int:
            return ELuaBlueprintPinType::Int;
        case EPropertyType::Float:
            return ELuaBlueprintPinType::Float;
        case EPropertyType::String:
            return ELuaBlueprintPinType::String;
        case EPropertyType::Name:
            return ELuaBlueprintPinType::Name;
        case EPropertyType::Vec3:
            return ELuaBlueprintPinType::Vector;
        case EPropertyType::Rotator:
            return ELuaBlueprintPinType::Rotator;
        case EPropertyType::Vec4:
            return ELuaBlueprintPinType::Vector4;
        case EPropertyType::Color4:
            return ELuaBlueprintPinType::LinearColor;
        case EPropertyType::ObjectRef:
            if (const FObjectPropertyBase* ObjectProperty = Property->AsObjectPropertyBase())
            {
                return LuaBlueprintPinTypeFromObjectClass(ObjectProperty->GetAllowedClassType());
            }
            return ELuaBlueprintPinType::Object;
        case EPropertyType::ClassRef:
            return ELuaBlueprintPinType::Class;
        case EPropertyType::Enum:
            return ELuaBlueprintPinType::Enum;
        case EPropertyType::Array:
            return ELuaBlueprintPinType::Array;
        case EPropertyType::Struct:
        case EPropertyType::SoftObjectRef:
        default:
            return ELuaBlueprintPinType::Any;
        }
    }

    const FFunction* FindLuaBlueprintFunctionBySignature(const FString& Signature)
    {
        if (Signature.empty())
        {
            return nullptr;
        }

        for (UClass* Class : UClass::GetAllClasses())
        {
            if (!Class)
            {
                continue;
            }
            if (const FFunction* Function = Class->FindFunctionBySignature(Signature.c_str(), true))
            {
                return Function;
            }
        }

        for (UClass* Class : UClass::GetAllClasses())
        {
            if (!Class)
            {
                continue;
            }
            if (const FFunction* Function = Class->FindFunctionByName(Signature.c_str(), true))
            {
                return Function;
            }
        }
        return nullptr;
    }

    FLuaBlueprintPin MakeLuaBlueprintPin(uint32 PinId, uint32 OwningNodeId, ELuaBlueprintPinKind Kind, ELuaBlueprintPinType Type, const FName& DisplayName)
    {
        FLuaBlueprintPin Pin;
        Pin.PinId        = PinId;
        Pin.OwningNodeId = OwningNodeId;
        Pin.Kind         = Kind;
        Pin.Type         = Type;
        Pin.DisplayName  = DisplayName;
        return Pin;
    }
}

FLuaBlueprintNode* ULuaBlueprintAsset::AddNode(ELuaBlueprintNodeType Type, const FName& DisplayName, float X, float Y)
{
    FLuaBlueprintNode Node;
    Node.NodeId      = AllocateId();
    Node.Type        = Type;
    Node.DisplayName = DisplayName;
    Node.PosX        = X;
    Node.PosY        = Y;
    Nodes.push_back(std::move(Node));
    BumpVersion();
    return &Nodes.back();
}

FLuaBlueprintPin* ULuaBlueprintAsset::AddPin(
    FLuaBlueprintNode&   Node,
    ELuaBlueprintPinKind Kind,
    ELuaBlueprintPinType PinType,
    const FName&         DisplayName
)
{
    FLuaBlueprintPin Pin;
    Pin.PinId        = AllocateId();
    Pin.OwningNodeId = Node.NodeId;
    Pin.Kind         = Kind;
    Pin.Type         = PinType;
    Pin.DisplayName  = DisplayName;
    Node.Pins.push_back(std::move(Pin));
    BumpVersion();
    return &Node.Pins.back();
}

namespace
{
    ELuaBlueprintPinType NormalizeLuaBlueprintVariableType(ELuaBlueprintPinType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintPinType::Bool:
        case ELuaBlueprintPinType::Int:
        case ELuaBlueprintPinType::Float:
        case ELuaBlueprintPinType::String:
        case ELuaBlueprintPinType::Vector:
        case ELuaBlueprintPinType::Object:
        case ELuaBlueprintPinType::Array:
        case ELuaBlueprintPinType::Actor:
        case ELuaBlueprintPinType::Pawn:
        case ELuaBlueprintPinType::PlayerController:
        case ELuaBlueprintPinType::ActorComponent:
        case ELuaBlueprintPinType::SceneComponent:
        case ELuaBlueprintPinType::PrimitiveComponent:
        case ELuaBlueprintPinType::StaticMesh:
        case ELuaBlueprintPinType::StaticMeshComponent:
        case ELuaBlueprintPinType::SkinnedMeshComponent:
        case ELuaBlueprintPinType::SkeletalMeshComponent:
        case ELuaBlueprintPinType::CameraComponent:
        case ELuaBlueprintPinType::CineCameraComponent:
        case ELuaBlueprintPinType::Material:
        case ELuaBlueprintPinType::Texture:
        case ELuaBlueprintPinType::AnimInstance:
        case ELuaBlueprintPinType::LuaBlueprintComponent:
        case ELuaBlueprintPinType::LuaScriptComponent:
        case ELuaBlueprintPinType::Rotator:
        case ELuaBlueprintPinType::LinearColor:
        case ELuaBlueprintPinType::Vector4:
        case ELuaBlueprintPinType::Class:
        case ELuaBlueprintPinType::Enum:
        case ELuaBlueprintPinType::Name:
            return Type;
        default:
            return ELuaBlueprintPinType::Float;
        }
    }
}

FLuaBlueprintLink* ULuaBlueprintAsset::AddLink(uint32 FromPinId, uint32 ToPinId)
{
    RefreshAllNodePinTypes();

    uint32 ResolvedFromPinId = 0;
    uint32 ResolvedToPinId   = 0;
    if (!CanLinkPins(FromPinId, ToPinId, &ResolvedFromPinId, &ResolvedToPinId))
    {
        return nullptr;
    }

    if (const FLuaBlueprintPin* ToPin = FindPin(ResolvedToPinId))
    {
        if (ToPin->Kind == ELuaBlueprintPinKind::Input && ToPin->Type != ELuaBlueprintPinType::Exec)
        {
            Links.erase(
                std::remove_if(
                    Links.begin(),
                    Links.end(),
                    [ResolvedToPinId](const FLuaBlueprintLink& Existing)
                    {
                        return Existing.ToPinId == ResolvedToPinId;
                    }
                ),
                Links.end()
            );
        }
    }

    FLuaBlueprintLink Link;
    Link.LinkId    = AllocateId();
    Link.FromPinId = ResolvedFromPinId;
    Link.ToPinId   = ResolvedToPinId;
    Links.push_back(std::move(Link));
    ApplyResolvedPinTypesForLink(ResolvedFromPinId, ResolvedToPinId);
    BumpVersion();
    return &Links.back();
}

FLuaBlueprintVariable* ULuaBlueprintAsset::AddVariable(const FName& Name, ELuaBlueprintPinType Type)
{
    FString BaseName = Name == FName::None ? FString("Variable") : Name.ToString();
    if (BaseName.empty())
    {
        BaseName = "Variable";
    }

    FString UniqueName = BaseName;
    int32   Suffix     = 1;
    bool    bUnique    = false;
    while (!bUnique)
    {
        bUnique = true;
        for (const FLuaBlueprintVariable& Existing : Variables)
        {
            if (Existing.Name.ToString() == UniqueName)
            {
                bUnique    = false;
                UniqueName = BaseName + std::to_string(Suffix++);
                break;
            }
        }
    }

    FLuaBlueprintVariable Variable;
    Variable.Name = FName(UniqueName);
    Variable.Type = NormalizeLuaBlueprintVariableType(Type);
    Variables.push_back(std::move(Variable));
    BumpVersion();
    return &Variables.back();
}

bool ULuaBlueprintAsset::IsConcreteDataPinType(ELuaBlueprintPinType Type)
{
    return Type != ELuaBlueprintPinType::Exec && Type != ELuaBlueprintPinType::Any;
}

bool ULuaBlueprintAsset::CanConvertPinTypes(ELuaBlueprintPinType FromType, ELuaBlueprintPinType ToType)
{
    if (FromType == ToType) return true;
    if (FromType == ELuaBlueprintPinType::Any || ToType == ELuaBlueprintPinType::Any) return true;
    if (FromType == ELuaBlueprintPinType::Exec || ToType == ELuaBlueprintPinType::Exec) return false;

    auto IsObjectLike = [](ELuaBlueprintPinType Type)
    {
        switch (Type)
        {
        case ELuaBlueprintPinType::Object:
        case ELuaBlueprintPinType::Actor:
        case ELuaBlueprintPinType::Pawn:
        case ELuaBlueprintPinType::PlayerController:
        case ELuaBlueprintPinType::ActorComponent:
        case ELuaBlueprintPinType::SceneComponent:
        case ELuaBlueprintPinType::PrimitiveComponent:
        case ELuaBlueprintPinType::StaticMesh:
        case ELuaBlueprintPinType::StaticMeshComponent:
        case ELuaBlueprintPinType::SkinnedMeshComponent:
        case ELuaBlueprintPinType::SkeletalMeshComponent:
        case ELuaBlueprintPinType::CameraComponent:
        case ELuaBlueprintPinType::CineCameraComponent:
        case ELuaBlueprintPinType::Material:
        case ELuaBlueprintPinType::Texture:
        case ELuaBlueprintPinType::AnimInstance:
        case ELuaBlueprintPinType::LuaBlueprintComponent:
        case ELuaBlueprintPinType::LuaScriptComponent:
        case ELuaBlueprintPinType::Class:
            return true;
        default:
            return false;
        }
    };

    auto IsDerivedObjectType = [](ELuaBlueprintPinType Derived, ELuaBlueprintPinType Base)
    {
        if (Derived == Base) return true;
        if (Base == ELuaBlueprintPinType::Object)
        {
            return Derived == ELuaBlueprintPinType::Actor ||
                    Derived == ELuaBlueprintPinType::Pawn ||
                    Derived == ELuaBlueprintPinType::PlayerController ||
                    Derived == ELuaBlueprintPinType::ActorComponent ||
                    Derived == ELuaBlueprintPinType::SceneComponent ||
                    Derived == ELuaBlueprintPinType::PrimitiveComponent ||
                    Derived == ELuaBlueprintPinType::StaticMesh ||
                    Derived == ELuaBlueprintPinType::StaticMeshComponent ||
                    Derived == ELuaBlueprintPinType::SkinnedMeshComponent ||
                    Derived == ELuaBlueprintPinType::SkeletalMeshComponent ||
                    Derived == ELuaBlueprintPinType::CameraComponent ||
                    Derived == ELuaBlueprintPinType::CineCameraComponent ||
                    Derived == ELuaBlueprintPinType::Material ||
                    Derived == ELuaBlueprintPinType::Texture ||
                    Derived == ELuaBlueprintPinType::AnimInstance ||
                    Derived == ELuaBlueprintPinType::LuaBlueprintComponent ||
                    Derived == ELuaBlueprintPinType::LuaScriptComponent;
        }
        if (Base == ELuaBlueprintPinType::Actor)
        {
            return Derived == ELuaBlueprintPinType::Pawn || Derived == ELuaBlueprintPinType::PlayerController;
        }
        if (Base == ELuaBlueprintPinType::ActorComponent)
        {
            return Derived == ELuaBlueprintPinType::SceneComponent ||
                    Derived == ELuaBlueprintPinType::PrimitiveComponent ||
                    Derived == ELuaBlueprintPinType::StaticMeshComponent ||
                    Derived == ELuaBlueprintPinType::SkinnedMeshComponent ||
                    Derived == ELuaBlueprintPinType::SkeletalMeshComponent ||
                    Derived == ELuaBlueprintPinType::CameraComponent ||
                    Derived == ELuaBlueprintPinType::CineCameraComponent ||
                    Derived == ELuaBlueprintPinType::LuaBlueprintComponent ||
                    Derived == ELuaBlueprintPinType::LuaScriptComponent;
        }
        if (Base == ELuaBlueprintPinType::SceneComponent)
        {
            return Derived == ELuaBlueprintPinType::PrimitiveComponent ||
                    Derived == ELuaBlueprintPinType::StaticMeshComponent ||
                    Derived == ELuaBlueprintPinType::SkinnedMeshComponent ||
                    Derived == ELuaBlueprintPinType::SkeletalMeshComponent ||
                    Derived == ELuaBlueprintPinType::CameraComponent ||
                    Derived == ELuaBlueprintPinType::CineCameraComponent;
        }
        if (Base == ELuaBlueprintPinType::PrimitiveComponent)
        {
            return Derived == ELuaBlueprintPinType::StaticMeshComponent ||
                    Derived == ELuaBlueprintPinType::SkinnedMeshComponent ||
                    Derived == ELuaBlueprintPinType::SkeletalMeshComponent;
        }
        if (Base == ELuaBlueprintPinType::SkinnedMeshComponent)
        {
            return Derived == ELuaBlueprintPinType::SkeletalMeshComponent;
        }
        if (Base == ELuaBlueprintPinType::CameraComponent)
        {
            return Derived == ELuaBlueprintPinType::CineCameraComponent;
        }
        return false;
    };

    // Strict object hierarchy: Actor and Component are not interchangeable.
    if (IsObjectLike(FromType) || IsObjectLike(ToType))
    {
        return IsDerivedObjectType(FromType, ToType);
    }

    if ((FromType == ELuaBlueprintPinType::Int && ToType == ELuaBlueprintPinType::Float) ||
        (FromType == ELuaBlueprintPinType::Float && ToType == ELuaBlueprintPinType::Int))
    {
        return true;
    }
    if (ToType == ELuaBlueprintPinType::String || ToType == ELuaBlueprintPinType::Bool) return true;
    if ((ToType == ELuaBlueprintPinType::Float || ToType == ELuaBlueprintPinType::Int) &&
        (FromType == ELuaBlueprintPinType::String || FromType == ELuaBlueprintPinType::Bool || FromType == ELuaBlueprintPinType::Enum))
        return true;
    if ((ToType == ELuaBlueprintPinType::Name || ToType == ELuaBlueprintPinType::Class || ToType == ELuaBlueprintPinType::Enum) &&
        FromType == ELuaBlueprintPinType::String)
        return true;
    if (ToType == ELuaBlueprintPinType::Vector &&
        (FromType == ELuaBlueprintPinType::Float || FromType == ELuaBlueprintPinType::Int || FromType == ELuaBlueprintPinType::String || FromType == ELuaBlueprintPinType::Rotator))
        return true;
    if ((ToType == ELuaBlueprintPinType::Rotator || ToType == ELuaBlueprintPinType::Vector4 || ToType == ELuaBlueprintPinType::LinearColor) &&
        (FromType == ELuaBlueprintPinType::Vector || FromType == ELuaBlueprintPinType::String))
        return true;
    if (ToType == ELuaBlueprintPinType::Any || FromType == ELuaBlueprintPinType::Any) return true;
    return false;
}

bool ULuaBlueprintAsset::ArePinTypesCompatibleForLink(ELuaBlueprintPinType FromType, ELuaBlueprintPinType ToType)
{
    if (FromType == ELuaBlueprintPinType::Exec || ToType == ELuaBlueprintPinType::Exec)
    {
        return FromType == ELuaBlueprintPinType::Exec && ToType == ELuaBlueprintPinType::Exec;
    }
    return CanConvertPinTypes(FromType, ToType);
}

FLuaBlueprintNode* ULuaBlueprintAsset::AddNodeOfType(ELuaBlueprintNodeType Type, float X, float Y)
{
    FLuaBlueprintNode* N = AddNode(Type, FName(NodeTypeDisplayName(Type)), X, Y);
    if (!N)
    {
        return nullptr;
    }

    switch (Type)
    {
    case ELuaBlueprintNodeType::EventBeginPlay:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::EventTick:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("DeltaTime"));
        break;
    case ELuaBlueprintNodeType::EventEndPlay:
    case ELuaBlueprintNodeType::EventPostBeginPlay:
    case ELuaBlueprintNodeType::EventPostStartMatch:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::EventPlayerCameraReady:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::PlayerController, FName("PlayerController"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("CameraManager"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::CameraComponent, FName("ActiveCamera"));
        break;
    case ELuaBlueprintNodeType::EventOverlap:
    case ELuaBlueprintNodeType::EventEndOverlap:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("OtherActor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("OverlappedComponent"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("OtherComp"));
        break;
    case ELuaBlueprintNodeType::EventHit:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("OtherActor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("HitComponent"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("OtherComp"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("NormalImpulse"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("HitResult"));
        break;
    case ELuaBlueprintNodeType::EventEndHit:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("OtherActor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("HitComponent"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("OtherComp"));
        break;
    case ELuaBlueprintNodeType::EventInputAction:
        N->NameValue = FName("Jump");
        N->StringValue = "Pressed";
        N->IntValue    = ResolveInputKeyCode("Space"); // 0이면 기존 ActionMapping 이름에만 bind.
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::EventInputAxis:
        N->NameValue = FName("MoveForward");
        N->StringValue = "Key"; // Key, MouseX, MouseY, MouseWheel.
        N->IntValue    = 0;     // 0이면 기존 AxisMapping 이름에만 bind.
        N->FloatValue  = 1.0f;
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::Sequence:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then0"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then1"));
        break;
    case ELuaBlueprintNodeType::Branch:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("Condition"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("True"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("False"));
        break;
    case ELuaBlueprintNodeType::ForLoop:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("First"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("Last"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Loop"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Int, FName("Index"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Completed"));
        break;
    case ELuaBlueprintNodeType::WhileLoop:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("Condition"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Loop"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Completed"));
        break;
    case ELuaBlueprintNodeType::PrintString:
        N->StringValue = "Hello Lua Blueprint";
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Text"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetVariable:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetProperty:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::CallFunction:
    case ELuaBlueprintNodeType::CallFunctionSignature:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Target"));
        for (int32 ArgIndex = 0; ArgIndex < 8; ++ArgIndex)
        {
            AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName(FString("Arg") + std::to_string(ArgIndex)));
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Result"));
        break;
    case ELuaBlueprintNodeType::GetVariable:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Value"));
        break;
    case ELuaBlueprintNodeType::LiteralBool:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Value"));
        break;
    case ELuaBlueprintNodeType::LiteralInt:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Int, FName("Value"));
        break;
    case ELuaBlueprintNodeType::LiteralFloat:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::LiteralString:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::String, FName("Value"));
        break;
    case ELuaBlueprintNodeType::LiteralVector:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Value"));
        break;
    case ELuaBlueprintNodeType::GetProperty:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Value"));
        break;
    case ELuaBlueprintNodeType::AddFloat:
    case ELuaBlueprintNodeType::SubtractFloat:
    case ELuaBlueprintNodeType::MultiplyFloat:
    case ELuaBlueprintNodeType::DivideFloat:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::AddInt:
    case ELuaBlueprintNodeType::SubtractInt:
    case ELuaBlueprintNodeType::MultiplyInt:
    case ELuaBlueprintNodeType::DivideInt:
    case ELuaBlueprintNodeType::ModInt:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Int, FName("Value"));
        break;
    case ELuaBlueprintNodeType::EqualFloat:
    case ELuaBlueprintNodeType::NotEqualFloat:
    case ELuaBlueprintNodeType::LessFloat:
    case ELuaBlueprintNodeType::GreaterFloat:
    case ELuaBlueprintNodeType::LessEqualFloat:
    case ELuaBlueprintNodeType::GreaterEqualFloat:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Result"));
        break;
    case ELuaBlueprintNodeType::EqualInt:
    case ELuaBlueprintNodeType::NotEqualInt:
    case ELuaBlueprintNodeType::LessInt:
    case ELuaBlueprintNodeType::GreaterInt:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Result"));
        break;
    case ELuaBlueprintNodeType::And:
    case ELuaBlueprintNodeType::Or:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Result"));
        break;
    case ELuaBlueprintNodeType::Not:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Result"));
        break;
    case ELuaBlueprintNodeType::AppendString:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::String, FName("Value"));
        break;
    case ELuaBlueprintNodeType::MakeVector:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("X"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Y"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Z"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Value"));
        break;
    case ELuaBlueprintNodeType::BreakVector:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("V"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("X"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Y"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Z"));
        break;
    case ELuaBlueprintNodeType::AddVector:
    case ELuaBlueprintNodeType::SubtractVector:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Value"));
        break;
    case ELuaBlueprintNodeType::ScaleVector:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("V"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Scale"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Value"));
        break;
    case ELuaBlueprintNodeType::DotVector:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::CrossVector:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Value"));
        break;
    case ELuaBlueprintNodeType::VectorLength:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("V"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::NormalizeVector:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("V"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Value"));
        break;
    case ELuaBlueprintNodeType::Self:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Actor, FName("Actor"));
        break;

    // ── Actor ──
    case ELuaBlueprintNodeType::SpawnActor:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Location"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Rotation"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Scale"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Actor, FName("Actor"));
        break;
    case ELuaBlueprintNodeType::SpawnPawn:
    case ELuaBlueprintNodeType::SpawnPawnAndPossess:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Location"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Rotation"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Scale"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Pawn, FName("Pawn"));
        break;
    case ELuaBlueprintNodeType::DestroyActor:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::FindActorByName:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Name"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Actor, FName("Actor"));
        break;
    case ELuaBlueprintNodeType::FindActorByClass:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Actor, FName("Actor"));
        break;
    case ELuaBlueprintNodeType::FindActorByTag:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Tag"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Actor, FName("Actor"));
        break;
    case ELuaBlueprintNodeType::FindActorsByTag:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Tag"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Array, FName("Actors"));
        break;
    case ELuaBlueprintNodeType::FindActorsByClass:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Array, FName("Actors"));
        break;
    case ELuaBlueprintNodeType::GetActorLocation:
    case ELuaBlueprintNodeType::GetActorRotation:
    case ELuaBlueprintNodeType::GetActorScale:
    case ELuaBlueprintNodeType::GetActorForward:
    case ELuaBlueprintNodeType::GetActorRight:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Value"));
        break;
    case ELuaBlueprintNodeType::SetActorLocation:
    case ELuaBlueprintNodeType::SetActorRotation:
    case ELuaBlueprintNodeType::SetActorScale:
    case ELuaBlueprintNodeType::AddActorWorldOffset:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::ActorHasTag:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Tag"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Result"));
        break;
    case ELuaBlueprintNodeType::ActorAddTag:
    case ELuaBlueprintNodeType::ActorRemoveTag:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Tag"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::GetActorName:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::String, FName("Name"));
        break;
    case ELuaBlueprintNodeType::GetOwnerActor:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("Owner"));
        break;

    // ── Game framework ──
    case ELuaBlueprintNodeType::GetPlayerController:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("Controller"));
        break;
    case ELuaBlueprintNodeType::GetController:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Pawn"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("Controller"));
        break;
    case ELuaBlueprintNodeType::GetControlledPawn:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Controller"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("Pawn"));
        break;
    case ELuaBlueprintNodeType::Possess:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Controller"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Pawn"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::UnPossess:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Controller"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::IsPawnPossessed:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Pawn"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Possessed"));
        break;
    case ELuaBlueprintNodeType::GetInputComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Pawn, FName("Pawn"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::ActorComponent, FName("InputComponent"));
        break;

    case ELuaBlueprintNodeType::AddForceToRoot:
    case ELuaBlueprintNodeType::AddTorqueToRoot:
    case ELuaBlueprintNodeType::AddImpulseToRoot:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Vector"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::GetRootLinearVelocity:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Velocity"));
        break;
    case ELuaBlueprintNodeType::SetRootLinearVelocity:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Velocity"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetRootSimulatePhysics:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("Simulate"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;

    case ELuaBlueprintNodeType::VehicleSetThrottle:
    case ELuaBlueprintNodeType::VehicleSetBrake:
    case ELuaBlueprintNodeType::VehicleSetSteering:
    case ELuaBlueprintNodeType::VehicleSetHandbrake:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::VehicleReset:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::VehicleGetForwardSpeed:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Speed"));
        break;

    case ELuaBlueprintNodeType::ParticleSetTemplateByPath:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Path"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::ParticleActivate:
    case ELuaBlueprintNodeType::ParticleDeactivate:
    case ELuaBlueprintNodeType::ParticleReset:
    case ELuaBlueprintNodeType::ParticleRebuild:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::ParticleSetLOD:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("LOD"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;

    // ── Object utility ──
    case ELuaBlueprintNodeType::IsValid:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Valid"));
        break;
    case ELuaBlueprintNodeType::Cast:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Success"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Failed"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("Result"));
        break;

    // ── Component ──
    case ELuaBlueprintNodeType::GetRootComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::SceneComponent, FName("Component"));
        break;
    case ELuaBlueprintNodeType::GetRootPrimitiveComponent:
    case ELuaBlueprintNodeType::GetPrimitiveComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        break;
    case ELuaBlueprintNodeType::GetComponentByName:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Name"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::SceneComponent, FName("Component"));
        break;
    case ELuaBlueprintNodeType::ActivateComponent:
    case ELuaBlueprintNodeType::DeactivateComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::ActorComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::AddForce:
    case ELuaBlueprintNodeType::AddTorque:
    case ELuaBlueprintNodeType::AddImpulse:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Vector"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::GetLinearVelocity:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Velocity"));
        break;
    case ELuaBlueprintNodeType::SetLinearVelocity:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Velocity"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::GetMass:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Mass"));
        break;
    case ELuaBlueprintNodeType::SetSimulatePhysics:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("Simulate"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;

    // ── Math util ──
    case ELuaBlueprintNodeType::Lerp:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Alpha"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::Clamp:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Min"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Max"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::Min:
    case ELuaBlueprintNodeType::Max:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::RandomFloat:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Min"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Max"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::RandomInt:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("Min"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("Max"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Int, FName("Value"));
        break;
    case ELuaBlueprintNodeType::Sin:
    case ELuaBlueprintNodeType::Cos:
    case ELuaBlueprintNodeType::Sqrt:
    case ELuaBlueprintNodeType::AbsFloat:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        break;
    case ELuaBlueprintNodeType::Floor:
    case ELuaBlueprintNodeType::Ceil:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Int, FName("Value"));
        break;
    case ELuaBlueprintNodeType::Distance:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("A"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("B"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Distance"));
        break;
    case ELuaBlueprintNodeType::GetGameTime:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Seconds"));
        break;

    // ── ForEach ──
    case ELuaBlueprintNodeType::ForEachActorByClass:
    case ELuaBlueprintNodeType::ForEachActorByTag:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Loop"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Int, FName("Index"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Completed"));
        break;
    case ELuaBlueprintNodeType::ForEachArray:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Array, FName("Array"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Loop"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Item"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Int, FName("Index"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Completed"));
        break;
    case ELuaBlueprintNodeType::Reroute:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Out"));
        break;
    case ELuaBlueprintNodeType::Comment:
        N->StringValue = "Comment";
        // VectorValue 의 X/Y 를 group 박스 너비/높이로 재활용. Z 는 미사용.
        N->VectorValue = FVector(320.0f, 200.0f, 0.0f);
        break;
    case ELuaBlueprintNodeType::CustomEvent:
        N->NameValue = FName("CustomEvent");
        // Function entry: Then 으로 본체 시작, Arg0..Arg3 는 caller 가 넘긴 파라미터.
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Arg0"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Arg1"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Arg2"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Arg3"));
        break;
    case ELuaBlueprintNodeType::CallCustomEvent:
        N->NameValue = FName("CustomEvent");
        // Caller: Arg0..Arg3 input 에 묶인 표현식을 함수 인자로 넘긴다.
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Arg0"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Arg1"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Arg2"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Arg3"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::Delay:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Seconds"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetTimer:
        N->NameValue = FName("CustomEvent");
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("TimerId"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Seconds"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("Looping"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::ClearTimer:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("TimerId"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::IsTimerActive:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("TimerId"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Active"));
        break;
    case ELuaBlueprintNodeType::SetTimerForNextTick:
        N->NameValue = FName("CustomEvent");
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::LineTrace:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Start"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("End"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("IgnoreActor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Hit"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Location"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Normal"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Distance"));
        break;
    case ELuaBlueprintNodeType::CreateWidget:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("DocumentPath"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Object, FName("Widget"));
        break;
    case ELuaBlueprintNodeType::AddWidgetToViewport:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Widget"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("ZOrder"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::RemoveWidgetFromParent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Widget"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetWidgetText:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Widget"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("ElementId"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Text"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::BindWidgetClick:
        N->NameValue = FName("CustomEvent");
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Widget"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("ElementId"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::LoadAudio:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Key"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Path"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("Loop"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::AudioPlaySound:
    case ELuaBlueprintNodeType::PlayBGM:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Key"));
        if (FLuaBlueprintPin* VolumePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Volume")))
        {
            VolumePin->DefaultFloat = 1.0f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::StopBGM:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::PlayAudioLoop:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Key"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("LoopName"));
        if (FLuaBlueprintPin* VolumePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Volume")))
        {
            VolumePin->DefaultFloat = 1.0f;
        }
        if (FLuaBlueprintPin* PitchPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Pitch")))
        {
            PitchPin->DefaultFloat = 1.0f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::StopAudioLoop:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("LoopName"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetAudioMasterVolume:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        if (FLuaBlueprintPin* VolumePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Volume")))
        {
            VolumePin->DefaultFloat = 1.0f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;

    case ELuaBlueprintNodeType::LoadStaticMesh:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Path"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::StaticMesh, FName("Mesh"));
        break;
    case ELuaBlueprintNodeType::GetStaticMeshComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::StaticMeshComponent, FName("Component"));
        break;
    case ELuaBlueprintNodeType::GetStaticMesh:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::StaticMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::StaticMesh, FName("Mesh"));
        break;
    case ELuaBlueprintNodeType::SetStaticMesh:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::StaticMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::StaticMesh, FName("Mesh"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::SetStaticMeshByPath:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::StaticMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("MeshPath"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::ClearStaticMesh:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::StaticMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;

    case ELuaBlueprintNodeType::GetLuaBlueprintComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::LuaBlueprintComponent, FName("Component"));
        break;
    case ELuaBlueprintNodeType::GetLuaScriptComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::LuaScriptComponent, FName("Component"));
        break;
    case ELuaBlueprintNodeType::CallLuaBlueprintFunction:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::LuaBlueprintComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("FunctionName"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::CallLuaScriptFunction:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::LuaScriptComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("FunctionName"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::CallLuaBlueprintFileFunction:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("BlueprintPath"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("FunctionName"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::CallLuaScriptFileFunction:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("ScriptFile"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("FunctionName"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;

    case ELuaBlueprintNodeType::CustomLuaFunction:
        N->NameValue = FName("CustomLuaFunction");
        N->StringValue = "-- Arg0, Arg1, Arg2, Arg3 are available.\n-- Return one value to expose it through Call Custom Lua Function.Return.\nreturn nil";
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::LuaFunction, FName("Function"));
        break;
    case ELuaBlueprintNodeType::CallCustomLuaFunction:
        N->NameValue = FName("CustomLuaFunction");
        N->IntValue = 0; // Optional stable target CustomLuaFunction node id. NameValue remains the readable/fallback function name.
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::LuaFunction, FName("Function"));
        if (FLuaBlueprintPin* FunctionNamePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("FunctionName")))
        {
            FunctionNamePin->DefaultString = N->NameValue.ToString();
        }
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Arg0"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Arg1"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Arg2"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Arg3"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, FName("Return"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;

    case ELuaBlueprintNodeType::GetSkeletalMeshComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::SkeletalMeshComponent, FName("Component"));
        break;
    case ELuaBlueprintNodeType::SetSkeletalMeshByPath:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SkeletalMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("MeshPath"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::ClearSkeletalMesh:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SkeletalMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::PlayAnimationByPath:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SkeletalMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("AnimationPath"));
        if (FLuaBlueprintPin* LoopPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("Looping")))
        {
            LoopPin->DefaultBool = true;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::StopAnimation:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SkeletalMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetAnimationByPath:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SkeletalMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("AnimationPath"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::SetAnimationPlayRate:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SkeletalMeshComponent, FName("Component"));
        if (FLuaBlueprintPin* RatePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Rate")))
        {
            RatePin->DefaultFloat = 1.0f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetAnimationLooping:
    case ELuaBlueprintNodeType::SetAnimationPlaying:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SkeletalMeshComponent, FName("Component"));
        if (FLuaBlueprintPin* EnabledPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("Enabled")))
        {
            EnabledPin->DefaultBool = true;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::GetAnimInstance:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SkeletalMeshComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::AnimInstance, FName("AnimInstance"));
        break;
    case ELuaBlueprintNodeType::SetAnimGraphVariableFloat:
        N->NameValue = FName("Speed");
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::AnimInstance, FName("AnimInstance"));
        if (FLuaBlueprintPin* VariablePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("Variable")))
        {
            VariablePin->DefaultString = N->NameValue.ToString();
        }
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::SetAnimGraphVariableBool:
        N->NameValue = FName("bIsFalling");
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::AnimInstance, FName("AnimInstance"));
        if (FLuaBlueprintPin* VariablePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("Variable")))
        {
            VariablePin->DefaultString = N->NameValue.ToString();
        }
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::SetAnimGraphVariableInt:
        N->NameValue = FName("State");
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::AnimInstance, FName("AnimInstance"));
        if (FLuaBlueprintPin* VariablePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("Variable")))
        {
            VariablePin->DefaultString = N->NameValue.ToString();
        }
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::GetAnimGraphVariableFloat:
        N->NameValue = FName("Speed");
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::AnimInstance, FName("AnimInstance"));
        if (FLuaBlueprintPin* VariablePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("Variable")))
        {
            VariablePin->DefaultString = N->NameValue.ToString();
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Found"));
        break;
    case ELuaBlueprintNodeType::GetAnimGraphVariableBool:
        N->NameValue = FName("bIsFalling");
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::AnimInstance, FName("AnimInstance"));
        if (FLuaBlueprintPin* VariablePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("Variable")))
        {
            VariablePin->DefaultString = N->NameValue.ToString();
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Found"));
        break;
    case ELuaBlueprintNodeType::GetAnimGraphVariableInt:
        N->NameValue = FName("State");
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::AnimInstance, FName("AnimInstance"));
        if (FLuaBlueprintPin* VariablePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("Variable")))
        {
            VariablePin->DefaultString = N->NameValue.ToString();
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Int, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Found"));
        break;
    case ELuaBlueprintNodeType::LoadMaterial:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Path"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Material, FName("Material"));
        break;
    case ELuaBlueprintNodeType::GetMaterial:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("ElementIndex"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Material, FName("Material"));
        break;
    case ELuaBlueprintNodeType::SetMaterial:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("ElementIndex"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Material, FName("Material"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::SetMaterialByPath:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("ElementIndex"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("MaterialPath"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::CreateDynamicMaterialInstance:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::PrimitiveComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Int, FName("ElementIndex"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("DebugName"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Material, FName("Material"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::SetMaterialScalarParameter:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Material, FName("Material"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Parameter"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::SetMaterialVectorParameter:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Material, FName("Material"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Parameter"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::SetMaterialColorParameter:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Material, FName("Material"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Parameter"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::LinearColor, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::SetMaterialTextureParameter:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Material, FName("Material"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Parameter"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Texture, FName("Texture"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::LoadTexture:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("Path"));
        if (FLuaBlueprintPin* SRGBPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Bool, FName("SRGB")))
        {
            SRGBPin->DefaultBool = true;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Texture, FName("Texture"));
        break;
    case ELuaBlueprintNodeType::GetCameraComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Actor"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::CameraComponent, FName("Camera"));
        break;
    case ELuaBlueprintNodeType::GetActiveCamera:
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::CameraComponent, FName("Camera"));
        break;
    case ELuaBlueprintNodeType::PossessCamera:
    case ELuaBlueprintNodeType::SetActiveCameraWithBlend:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::CameraComponent, FName("Camera"));
        if (Type == ELuaBlueprintNodeType::SetActiveCameraWithBlend)
        {
            AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("BlendTime"));
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetViewTargetWithBlend:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Actor, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("BlendTime"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetCameraFOV:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::CameraComponent, FName("Camera"));
        if (FLuaBlueprintPin* FovPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("FOV")))
        {
            FovPin->DefaultFloat = 1.04719758f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::CameraLookAt:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::CameraComponent, FName("Camera"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Vector, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::FadeIn:
    case ELuaBlueprintNodeType::FadeOut:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Duration"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetVignette:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Intensity"));
        if (FLuaBlueprintPin* RadiusPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Radius")))
        {
            RadiusPin->DefaultFloat = 0.75f;
        }
        if (FLuaBlueprintPin* SoftnessPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Softness")))
        {
            SoftnessPin->DefaultFloat = 0.35f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::ClearVignette:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::StartCameraShakeAsset:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::String, FName("AssetPath"));
        if (FLuaBlueprintPin* ScalePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Scale")))
        {
            ScalePin->DefaultFloat = 1.0f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetDepthOfField:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        if (FLuaBlueprintPin* FocusPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("FocusDistance")))
        {
            FocusPin->DefaultFloat = 500.0f;
        }
        if (FLuaBlueprintPin* RangePin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("FocusRange")))
        {
            RangePin->DefaultFloat = 200.0f;
        }
        if (FLuaBlueprintPin* BlurPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("MaxBlurRadius")))
        {
            BlurPin->DefaultFloat = 4.0f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetBokeh:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        if (FLuaBlueprintPin* RadiusPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("RadiusThreshold")))
        {
            RadiusPin->DefaultFloat = 2.5f;
        }
        if (FLuaBlueprintPin* LumaPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("LumaThreshold")))
        {
            LumaPin->DefaultFloat = 0.45f;
        }
        if (FLuaBlueprintPin* IntensityPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Intensity")))
        {
            IntensityPin->DefaultFloat = 0.65f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::ClearDepthOfField:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::SetLetterbox:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::CameraComponent, FName("Camera"));
        if (FLuaBlueprintPin* AmountPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Amount")))
        {
            AmountPin->DefaultFloat = 1.0f;
        }
        if (FLuaBlueprintPin* ThicknessPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Float, FName("Thickness")))
        {
            ThicknessPin->DefaultFloat = 0.12f;
        }
        if (FLuaBlueprintPin* ColorPin = AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::LinearColor, FName("Color")))
        {
            ColorPin->DefaultVector = FVector(0.0f, 0.0f, 0.0f);
            ColorPin->DefaultFloat = 1.0f;
        }
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::ClearLetterbox:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::CameraComponent, FName("Camera"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;

    case ELuaBlueprintNodeType::AttachToComponent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SceneComponent, FName("Child"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SceneComponent, FName("Parent"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("SocketName"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::GetAttachSocketName:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SceneComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Name, FName("SocketName"));
        break;
    case ELuaBlueprintNodeType::HasSocket:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SceneComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("SocketName"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Result"));
        break;
    case ELuaBlueprintNodeType::GetSocketWorldLocation:
    case ELuaBlueprintNodeType::GetSocketWorldRotation:
    case ELuaBlueprintNodeType::GetSocketWorldScale:
    case ELuaBlueprintNodeType::GetSocketForwardVector:
    case ELuaBlueprintNodeType::GetSocketRightVector:
    case ELuaBlueprintNodeType::GetSocketUpVector:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::SceneComponent, FName("Component"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, FName("SocketName"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Value"));
        break;

    case ELuaBlueprintNodeType::ToBool:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Result"));
        break;
    case ELuaBlueprintNodeType::ToInt:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Int, FName("Result"));
        break;
    case ELuaBlueprintNodeType::ToFloat:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Float, FName("Result"));
        break;
    case ELuaBlueprintNodeType::ToString:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::String, FName("Result"));
        break;
    case ELuaBlueprintNodeType::ToVector:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, FName("Value"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Vector, FName("Result"));
        break;

    // ── Delegates: NameValue 는 target 의 event/function 이름, StringValue 는 우리 CustomEvent 이름. ──
    case ELuaBlueprintNodeType::BindEvent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Success"));
        break;
    case ELuaBlueprintNodeType::UnbindEvent:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, FName("In"));
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, FName("Then"));
        break;
    case ELuaBlueprintNodeType::HasEventBinding:
        AddPin(*N, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, FName("Target"));
        AddPin(*N, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, FName("Bound"));
        break;
    }
    return N;
}

void ULuaBlueprintAsset::InitializeDefault()
{
    Nodes.clear();
    Links.clear();
    Variables.clear();
    Diagnostics.clear();
    GeneratedLuaSource.clear();
    LastGoodGeneratedLuaSource  = GeneratedLuaSource;
    NextId                      = 1;
    Version                     = 0;
    RuntimeVersion              = 0;
    LastCompiledCompilerVersion = 0;
    bLastCompileSucceeded       = false;

    uint32 BeginThenPin = 0;
    if (FLuaBlueprintNode* Begin = AddNodeOfType(ELuaBlueprintNodeType::EventBeginPlay, -280.0f, 0.0f))
    {
        if (!Begin->Pins.empty()) BeginThenPin = Begin->Pins.front().PinId;
    }

    uint32 PrintInPin = 0;
    if (FLuaBlueprintNode* Print = AddNodeOfType(ELuaBlueprintNodeType::PrintString, 0.0f, 0.0f))
    {
        Print->StringValue = "Lua Blueprint BeginPlay";
        for (FLuaBlueprintPin& Pin : Print->Pins)
        {
            if (Pin.Kind == ELuaBlueprintPinKind::Input && Pin.Type == ELuaBlueprintPinType::Exec)
            {
                PrintInPin = Pin.PinId;
            }
            else if (Pin.Kind == ELuaBlueprintPinKind::Input && Pin.Type == ELuaBlueprintPinType::String && Pin.
                                                                                                            DisplayName.ToString() == "Text")
            {
                Pin.DefaultString = Print->StringValue;
            }
        }
    }

    if (BeginThenPin && PrintInPin)
    {
        AddLink(BeginThenPin, PrintInPin);
    }

    Compile();
}

bool ULuaBlueprintAsset::Compile()
{
    FLuaBlueprintCompileResult Result = FLuaBlueprintCompiler::Compile(*this);
    SetCompileResult(Result.GeneratedLuaSource, std::move(Result.Diagnostics), Result.bSuccess);
    return Result.bSuccess;
}

bool ULuaBlueprintAsset::RemoveNode(uint32 NodeId)
{
    if (NodeId == 0) return false;

    TArray<uint32> PinIds;
    for (const FLuaBlueprintNode& Node : Nodes)
    {
        if (Node.NodeId != NodeId) continue;
        for (const FLuaBlueprintPin& Pin : Node.Pins) PinIds.push_back(Pin.PinId);
        break;
    }

    const size_t BeforeNodes = Nodes.size();
    Links.erase(
        std::remove_if(
            Links.begin(),
            Links.end(),
            [&PinIds](const FLuaBlueprintLink& Link)
            {
                for (uint32 PinId : PinIds)
                {
                    if (Link.FromPinId == PinId || Link.ToPinId == PinId) return true;
                }
                return false;
            }
        ),
        Links.end()
    );

    Nodes.erase(
        std::remove_if(
            Nodes.begin(),
            Nodes.end(),
            [NodeId](const FLuaBlueprintNode& Node)
            {
                return Node.NodeId == NodeId;
            }
        ),
        Nodes.end()
    );

    const bool bRemoved = Nodes.size() != BeforeNodes;
    if (bRemoved)
    {
        RefreshAllNodePinTypes();
        BumpVersion();
    }
    return bRemoved;
}

bool ULuaBlueprintAsset::RemoveLink(uint32 LinkId)
{
    if (LinkId == 0) return false;
    const size_t Before = Links.size();
    Links.erase(
        std::remove_if(
            Links.begin(),
            Links.end(),
            [LinkId](const FLuaBlueprintLink& Link)
            {
                return Link.LinkId == LinkId;
            }
        ),
        Links.end()
    );
    const bool bRemoved = Links.size() != Before;
    if (bRemoved)
    {
        RefreshAllNodePinTypes();
        BumpVersion();
    }
    return bRemoved;
}

bool ULuaBlueprintAsset::RemoveInvalidLinks()
{
    const size_t Before = Links.size();
    Links.erase(
        std::remove_if(
            Links.begin(),
            Links.end(),
            [this](const FLuaBlueprintLink& Link)
            {
                const FLuaBlueprintPin* From = FindPin(Link.FromPinId);
                const FLuaBlueprintPin* To   = FindPin(Link.ToPinId);
                if (!From || !To) return true;
                if (From->Kind != ELuaBlueprintPinKind::Output || To->Kind != ELuaBlueprintPinKind::Input) return true;
                if (From->OwningNodeId == To->OwningNodeId) return true;
                return !ArePinTypesCompatibleForLink(From->Type, To->Type);
            }
        ),
        Links.end()
    );

    const bool bRemoved = Links.size() != Before;
    if (bRemoved)
    {
        RefreshAllNodePinTypes();
        BumpVersion();
    }
    return bRemoved;
}

bool ULuaBlueprintAsset::CanLinkPins(uint32 PinAId, uint32 PinBId, uint32* OutFromPinId, uint32* OutToPinId) const
{
    if (PinAId == 0 || PinBId == 0 || PinAId == PinBId) return false;

    const FLuaBlueprintPin* A = FindPin(PinAId);
    const FLuaBlueprintPin* B = FindPin(PinBId);
    if (!A || !B) return false;
    if (A->OwningNodeId == B->OwningNodeId) return false;
    if (A->Kind == B->Kind) return false;

    const FLuaBlueprintPin* From = (A->Kind == ELuaBlueprintPinKind::Output) ? A : B;
    const FLuaBlueprintPin* To   = (From == A) ? B : A;

    if (!ArePinTypesCompatibleForLink(From->Type, To->Type))
    {
        return false;
    }

    if (From->Type != ELuaBlueprintPinType::Exec && To->Type != ELuaBlueprintPinType::Exec)
    {
        const uint32   SourceNodeId = From->OwningNodeId;
        const uint32   TargetNodeId = To->OwningNodeId;
        TArray<uint32> Stack;
        TSet<uint32>   Visited;
        Stack.push_back(TargetNodeId);
        while (!Stack.empty())
        {
            const uint32 CurrentNodeId = Stack.back();
            Stack.pop_back();
            if (CurrentNodeId == SourceNodeId)
            {
                return false;
            }
            if (!Visited.insert(CurrentNodeId).second)
            {
                continue;
            }
            for (const FLuaBlueprintLink& ExistingLink : Links)
            {
                const FLuaBlueprintPin* ExistingFrom = FindPin(ExistingLink.FromPinId);
                const FLuaBlueprintPin* ExistingTo   = FindPin(ExistingLink.ToPinId);
                if (!ExistingFrom || !ExistingTo)
                {
                    continue;
                }
                if (ExistingFrom->Type == ELuaBlueprintPinType::Exec || ExistingTo->Type == ELuaBlueprintPinType::Exec)
                {
                    continue;
                }
                if (ExistingFrom->OwningNodeId == CurrentNodeId)
                {
                    Stack.push_back(ExistingTo->OwningNodeId);
                }
            }
        }
    }

    // 연결 규칙 (UE Blueprint 기준):
    //   - 데이터 입력: fan-in 1개. 단, 이미 연결돼 있어도 거부하지 않고 "덮어쓰기"를 허용한다
    //     (AddLink 가 기존 링크를 제거하고 새로 연결). 그래서 여기서 막지 않는다.
    //   - Exec 입력: fan-in 다중 허용 (여러 곳에서 같은 노드 실행).
    //   - 데이터 출력: fan-out 다중 허용.
    //   - Exec 출력: fan-out 1개만 (분기는 Sequence 노드로).
    const bool bExecLink = (From->Type == ELuaBlueprintPinType::Exec);
    for (const FLuaBlueprintLink& Link : Links)
    {
        // 완전히 동일한 링크(같은 출력 → 같은 입력)는 중복.
        if (Link.FromPinId == From->PinId && Link.ToPinId == To->PinId) return false;
        // Exec 출력은 fan-out 1개만.
        if (bExecLink && Link.FromPinId == From->PinId) return false;
    }

    if (OutFromPinId) *OutFromPinId = From->PinId;
    if (OutToPinId) *OutToPinId = To->PinId;
    return true;
}

FLuaBlueprintNode* ULuaBlueprintAsset::FindNode(uint32 NodeId)
{
    if (NodeId == 0) return nullptr;
    for (FLuaBlueprintNode& Node : Nodes)
    {
        if (Node.NodeId == NodeId) return &Node;
    }
    return nullptr;
}

const FLuaBlueprintNode* ULuaBlueprintAsset::FindNode(uint32 NodeId) const
{
    if (NodeId == 0) return nullptr;
    for (const FLuaBlueprintNode& Node : Nodes)
    {
        if (Node.NodeId == NodeId) return &Node;
    }
    return nullptr;
}

FLuaBlueprintPin* ULuaBlueprintAsset::FindPin(uint32 PinId)
{
    if (PinId == 0) return nullptr;
    for (FLuaBlueprintNode& Node : Nodes)
    {
        for (FLuaBlueprintPin& Pin : Node.Pins)
        {
            if (Pin.PinId == PinId) return &Pin;
        }
    }
    return nullptr;
}

const FLuaBlueprintPin* ULuaBlueprintAsset::FindPin(uint32 PinId) const
{
    if (PinId == 0) return nullptr;
    for (const FLuaBlueprintNode& Node : Nodes)
    {
        for (const FLuaBlueprintPin& Pin : Node.Pins)
        {
            if (Pin.PinId == PinId) return &Pin;
        }
    }
    return nullptr;
}

const FLuaBlueprintLink* ULuaBlueprintAsset::FindLinkToInput(uint32 InputPinId) const
{
    if (InputPinId == 0) return nullptr;
    for (const FLuaBlueprintLink& Link : Links)
    {
        if (Link.ToPinId == InputPinId) return &Link;
    }
    return nullptr;
}

const FLuaBlueprintLink* ULuaBlueprintAsset::FindFirstLinkFromOutput(uint32 OutputPinId) const
{
    if (OutputPinId == 0) return nullptr;
    for (const FLuaBlueprintLink& Link : Links)
    {
        if (Link.FromPinId == OutputPinId) return &Link;
    }
    return nullptr;
}

const FString& ULuaBlueprintAsset::GetRuntimeLuaSource() const
{
    return bLastCompileSucceeded ? GeneratedLuaSource : LastGoodGeneratedLuaSource;
}

bool ULuaBlueprintAsset::HasCompileErrors() const
{
    for (const FLuaBlueprintDiagnostic& Diagnostic : Diagnostics)
    {
        if (Diagnostic.Severity == ELuaBlueprintDiagnosticSeverity::Error)
        {
            return true;
        }
    }
    return false;
}

bool ULuaBlueprintAsset::HasRunnableLuaSource() const
{
    return !GetRuntimeLuaSource().empty();
}

void ULuaBlueprintAsset::RefreshNodePinTypes(FLuaBlueprintNode& Node)
{
    auto SetPinType = [&Node](const char* PinName, ELuaBlueprintPinKind Kind, ELuaBlueprintPinType Type)
    {
        for (FLuaBlueprintPin& Pin : Node.Pins)
        {
            if (Pin.Kind == Kind && Pin.DisplayName.ToString() == PinName)
            {
                Pin.Type = Type;
                return true;
            }
        }
        return false;
    };

    auto SetPinTypeBySlot = [&Node](ELuaBlueprintPinKind Kind, int32 SlotIndex, ELuaBlueprintPinType Type)
    {
        int32 CurrentIndex = 0;
        for (FLuaBlueprintPin& Pin : Node.Pins)
        {
            if (Pin.Kind != Kind) continue;
            if (CurrentIndex++ == SlotIndex)
            {
                Pin.Type = Type;
                return true;
            }
        }
        return false;
    };

    auto SetPinTypeNamedOrSlot = [&](const char* PinName, ELuaBlueprintPinKind Kind, int32 SlotIndex, ELuaBlueprintPinType Type)
    {
        if (!SetPinType(PinName, Kind, Type))
        {
            SetPinTypeBySlot(Kind, SlotIndex, Type);
        }
    };
    auto EnsurePin = [this, &Node](ELuaBlueprintPinKind Kind, ELuaBlueprintPinType Type, const char* PinName) -> FLuaBlueprintPin*
    {
        for (FLuaBlueprintPin& Pin : Node.Pins)
        {
            if (Pin.Kind == Kind && Pin.DisplayName.ToString() == PinName)
            {
                Pin.Type = Type;
                return &Pin;
            }
        }
        Node.Pins.push_back(MakeLuaBlueprintPin(AllocateId(), Node.NodeId, Kind, Type, FName(PinName)));
        return &Node.Pins.back();
    };


    auto RebuildCallFunctionSignaturePins = [this, &Node]()
    {
        const FString    Signature = !Node.StringValue.empty() ? Node.StringValue : Node.NameValue.ToString();
        const FFunction* Function  = FindLuaBlueprintFunctionBySignature(Signature);
        if (!Function)
        {
            return;
        }

        auto FindExistingPin = [&Node](ELuaBlueprintPinKind Kind, const FString& Name) -> const FLuaBlueprintPin*
        {
            for (const FLuaBlueprintPin& Pin : Node.Pins)
            {
                if (Pin.Kind == Kind && Pin.DisplayName.ToString() == Name)
                {
                    return &Pin;
                }
            }
            return nullptr;
        };

        auto AddSchemaPin = [this, &Node, &FindExistingPin](TArray<FLuaBlueprintPin>& OutPins, ELuaBlueprintPinKind Kind, ELuaBlueprintPinType Type, const FString& Name)
        {
            if (const FLuaBlueprintPin* Existing = FindExistingPin(Kind, Name))
            {
                FLuaBlueprintPin PreservedPin = *Existing;
                PreservedPin.OwningNodeId     = Node.NodeId;
                PreservedPin.Kind             = Kind;
                PreservedPin.Type             = Type;
                PreservedPin.DisplayName      = FName(Name);
                OutPins.push_back(std::move(PreservedPin));
                return;
            }

            OutPins.push_back(MakeLuaBlueprintPin(AllocateId(), Node.NodeId, Kind, Type, FName(Name)));
        };

        TArray<FLuaBlueprintPin> NewPins;
        AddSchemaPin(NewPins, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, "In");
        AddSchemaPin(NewPins, ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Object, "Target");

        int32 ArgIndex = 0;
        for (const FProperty* Parameter : Function->GetParameters())
        {
            if (!Parameter)
            {
                continue;
            }
            const bool bOutOnly = (Parameter->Flags & PF_OutParm) != 0 && (Parameter->Flags & PF_ConstParm) == 0;
            if (bOutOnly)
            {
                continue;
            }
            FString PinName = Parameter->Name && std::strlen(Parameter->Name) > 0 ? FString(Parameter->Name) : FString("Arg") + std::to_string(ArgIndex);
            AddSchemaPin(NewPins, ELuaBlueprintPinKind::Input, LuaBlueprintPinTypeFromProperty(Parameter), PinName);
            ++ArgIndex;
        }

        AddSchemaPin(NewPins, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, "Then");
        if (const FProperty* ReturnProperty = Function->GetReturnProperty())
        {
            AddSchemaPin(NewPins, ELuaBlueprintPinKind::Output, LuaBlueprintPinTypeFromProperty(ReturnProperty), "Result");
        }
        else
        {
            AddSchemaPin(NewPins, ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, "Result");
        }
        for (const FProperty* Parameter : Function->GetParameters())
        {
            if (!Parameter || (Parameter->Flags & PF_OutParm) == 0 || (Parameter->Flags & PF_ConstParm) != 0)
            {
                continue;
            }
            FString PinName = Parameter->Name && std::strlen(Parameter->Name) > 0 ? FString(Parameter->Name) : FString("OutParam");
            AddSchemaPin(NewPins, ELuaBlueprintPinKind::Output, LuaBlueprintPinTypeFromProperty(Parameter), PinName);
        }

        Node.Pins = std::move(NewPins);
    };

    if (Node.Type == ELuaBlueprintNodeType::CallFunctionSignature)
    {
        RebuildCallFunctionSignaturePins();
    }

    if (Node.Type == ELuaBlueprintNodeType::GetVariable || Node.Type == ELuaBlueprintNodeType::SetVariable)
    {
        ELuaBlueprintPinType VarType = ELuaBlueprintPinType::Any;
        const FString        VarName = Node.NameValue.ToString();
        for (const FLuaBlueprintVariable& Variable : Variables)
        {
            if (Variable.Name.ToString() == VarName)
            {
                VarType = Variable.Type;
                break;
            }
        }
        if (Node.Type == ELuaBlueprintNodeType::GetVariable)
        {
            SetPinType("Value", ELuaBlueprintPinKind::Output, VarType);
        }
        else
        {
            SetPinType("Value", ELuaBlueprintPinKind::Input, VarType);
        }
    }
    else if (Node.Type == ELuaBlueprintNodeType::Reroute)
    {
        ELuaBlueprintPinType ResolvedType = ELuaBlueprintPinType::Any;
        for (const FLuaBlueprintPin& Pin : Node.Pins)
        {
            if (Pin.Type != ELuaBlueprintPinType::Any && Pin.Type != ELuaBlueprintPinType::Exec)
            {
                ResolvedType = Pin.Type;
                break;
            }
        }
        SetPinType("In", ELuaBlueprintPinKind::Input, ResolvedType);
        SetPinType("Out", ELuaBlueprintPinKind::Output, ResolvedType);
    }

    // Hard schema repair: legacy assets may keep Object on pins even after enum/schema changes.
    // Slot repair prevents Actor outputs from remaining connectable to component inputs.
    switch (Node.Type)
    {
    case ELuaBlueprintNodeType::Self:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Actor);
        break;
    case ELuaBlueprintNodeType::AddForce:
    case ELuaBlueprintNodeType::AddTorque:
    case ELuaBlueprintNodeType::AddImpulse:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("Component", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::PrimitiveComponent);
        SetPinTypeNamedOrSlot("Vector", ELuaBlueprintPinKind::Input, 2, ELuaBlueprintPinType::Vector);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        break;
    case ELuaBlueprintNodeType::SetLinearVelocity:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("Component", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::PrimitiveComponent);
        SetPinTypeNamedOrSlot("Velocity", ELuaBlueprintPinKind::Input, 2, ELuaBlueprintPinType::Vector);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        break;
    case ELuaBlueprintNodeType::GetLinearVelocity:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::PrimitiveComponent);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Vector);
        break;
    case ELuaBlueprintNodeType::GetMass:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::PrimitiveComponent);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Float);
        break;
    case ELuaBlueprintNodeType::SetSimulatePhysics:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("Component", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::PrimitiveComponent);
        SetPinTypeNamedOrSlot("Simulate", ELuaBlueprintPinKind::Input, 2, ELuaBlueprintPinType::Bool);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        break;
    case ELuaBlueprintNodeType::GetRootComponent:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Actor);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::SceneComponent);
        break;
    case ELuaBlueprintNodeType::GetRootPrimitiveComponent:
    case ELuaBlueprintNodeType::GetPrimitiveComponent:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Actor);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::PrimitiveComponent);
        break;
    case ELuaBlueprintNodeType::GetComponentByName:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Actor);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::String);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::SceneComponent);
        break;
    case ELuaBlueprintNodeType::GetOwnerActor:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::ActorComponent);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Actor);
        break;
    case ELuaBlueprintNodeType::LoadStaticMesh:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::String);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::StaticMesh);
        break;
    case ELuaBlueprintNodeType::GetStaticMeshComponent:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Actor);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::StaticMeshComponent);
        break;
    case ELuaBlueprintNodeType::GetStaticMesh:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::StaticMeshComponent);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::StaticMesh);
        break;
    case ELuaBlueprintNodeType::SetStaticMesh:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("Component", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::StaticMeshComponent);
        SetPinTypeNamedOrSlot("Mesh", ELuaBlueprintPinKind::Input, 2, ELuaBlueprintPinType::StaticMesh);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 1, ELuaBlueprintPinType::Bool);
        break;
    case ELuaBlueprintNodeType::SetStaticMeshByPath:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("Component", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::StaticMeshComponent);
        SetPinTypeNamedOrSlot("MeshPath", ELuaBlueprintPinKind::Input, 2, ELuaBlueprintPinType::String);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 1, ELuaBlueprintPinType::Bool);
        break;
    case ELuaBlueprintNodeType::ClearStaticMesh:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("Component", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::StaticMeshComponent);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        break;

    case ELuaBlueprintNodeType::GetLuaBlueprintComponent:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Actor);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::LuaBlueprintComponent);
        break;
    case ELuaBlueprintNodeType::GetLuaScriptComponent:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Actor);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::LuaScriptComponent);
        break;
    case ELuaBlueprintNodeType::CallLuaBlueprintFunction:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("Component", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::LuaBlueprintComponent);
        SetPinTypeNamedOrSlot("FunctionName", ELuaBlueprintPinKind::Input, 2, ELuaBlueprintPinType::Name);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 1, ELuaBlueprintPinType::Bool);
        break;
    case ELuaBlueprintNodeType::CallLuaScriptFunction:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("Component", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::LuaScriptComponent);
        SetPinTypeNamedOrSlot("FunctionName", ELuaBlueprintPinKind::Input, 2, ELuaBlueprintPinType::Name);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 1, ELuaBlueprintPinType::Bool);
        break;
    case ELuaBlueprintNodeType::CallLuaBlueprintFileFunction:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("BlueprintPath", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::String);
        SetPinTypeNamedOrSlot("FunctionName", ELuaBlueprintPinKind::Input, 2, ELuaBlueprintPinType::Name);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 1, ELuaBlueprintPinType::Bool);
        break;
    case ELuaBlueprintNodeType::CallLuaScriptFileFunction:
        SetPinTypeBySlot(ELuaBlueprintPinKind::Input, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeNamedOrSlot("ScriptFile", ELuaBlueprintPinKind::Input, 1, ELuaBlueprintPinType::String);
        SetPinTypeNamedOrSlot("FunctionName", ELuaBlueprintPinKind::Input, 2, ELuaBlueprintPinType::Name);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 0, ELuaBlueprintPinType::Exec);
        SetPinTypeBySlot(ELuaBlueprintPinKind::Output, 1, ELuaBlueprintPinType::Bool);
        break;
    case ELuaBlueprintNodeType::CustomLuaFunction:
        EnsurePin(ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::LuaFunction, "Function");
        break;
    case ELuaBlueprintNodeType::CallCustomLuaFunction:
        EnsurePin(ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Exec, "In");
        EnsurePin(ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::LuaFunction, "Function");
        EnsurePin(ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Name, "FunctionName");
        EnsurePin(ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, "Arg0");
        EnsurePin(ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, "Arg1");
        EnsurePin(ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, "Arg2");
        EnsurePin(ELuaBlueprintPinKind::Input, ELuaBlueprintPinType::Any, "Arg3");
        EnsurePin(ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Exec, "Then");
        EnsurePin(ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Any, "Return");
        EnsurePin(ELuaBlueprintPinKind::Output, ELuaBlueprintPinType::Bool, "Success");
        break;
    default:
        break;
    }
}

void ULuaBlueprintAsset::RefreshAllNodePinTypes()
{
    // 1) Reset dynamic schema pins first. Without this, Reroute/Any pins keep stale
    // concrete types after unlink/delete and later refuse otherwise valid links.
    for (FLuaBlueprintNode& Node : Nodes)
    {
        if (Node.Type == ELuaBlueprintNodeType::Reroute)
        {
            for (FLuaBlueprintPin& Pin : Node.Pins)
            {
                if (Pin.Type != ELuaBlueprintPinType::Exec)
                {
                    Pin.Type = ELuaBlueprintPinType::Any;
                }
            }
        }
    }

    // 2) Re-apply variable schema from the variable table.
    for (FLuaBlueprintNode& Node : Nodes)
    {
        RefreshNodePinTypes(Node);
    }

    // 3) Propagate concrete types through Any/Reroute links until stable.
    bool  bChanged  = true;
    int32 Iteration = 0;
    while (bChanged && Iteration++ < 64)
    {
        bChanged = false;
        for (const FLuaBlueprintLink& Link : Links)
        {
            FLuaBlueprintPin* From = FindPin(Link.FromPinId);
            FLuaBlueprintPin* To   = FindPin(Link.ToPinId);
            if (!From || !To) continue;
            if (From->Type == ELuaBlueprintPinType::Exec || To->Type == ELuaBlueprintPinType::Exec) continue;

            if (From->Type == ELuaBlueprintPinType::Any && IsConcreteDataPinType(To->Type))
            {
                From->Type = To->Type;
                bChanged   = true;
            }
            if (To->Type == ELuaBlueprintPinType::Any && IsConcreteDataPinType(From->Type))
            {
                To->Type = From->Type;
                bChanged = true;
            }

            FLuaBlueprintNode* FromNode = FindNode(From->OwningNodeId);
            FLuaBlueprintNode* ToNode   = FindNode(To->OwningNodeId);
            if (FromNode && FromNode->Type == ELuaBlueprintNodeType::Reroute)
            {
                RefreshNodePinTypes(*FromNode);
            }
            if (ToNode && ToNode->Type == ELuaBlueprintNodeType::Reroute)
            {
                RefreshNodePinTypes(*ToNode);
            }
        }
    }
}

void ULuaBlueprintAsset::Serialize(FArchive& Ar)
{
    if (Ar.IsSaving())
    {
        uint32 Magic         = LuaBlueprintAssetMagic;
        uint32 FormatVersion = LuaBlueprintAssetFormatVersion;
        Ar << Magic;
        Ar << FormatVersion;
        Ar << LastCompiledCompilerVersion;
        Ar << NextId;
        Ar << Version;
        Ar << RuntimeVersion;
        Ar << bLastCompileSucceeded;
        Ar << Nodes;
        Ar << Links;
        Ar << Variables;
        Ar << GeneratedLuaSource;
        Ar << LastGoodGeneratedLuaSource;
        uint32 DebugNodeCount = static_cast<uint32>(Nodes.size());
        Ar << DebugNodeCount;
        for (FLuaBlueprintNode& Node : Nodes)
        {
            Ar << Node.NodeId;
            Ar << Node.bBreakpointEnabled;
        }
        return;
    }

    uint32 First = 0;
    Ar << First;
    if (First == LuaBlueprintAssetMagic)
    {
        uint32 FormatVersion = 0;
        Ar << FormatVersion;
        Ar << LastCompiledCompilerVersion;
        Ar << NextId;
        Ar << Version;
        Ar << RuntimeVersion;
        Ar << bLastCompileSucceeded;
        Ar << Nodes;
        Ar << Links;
        Ar << Variables;
        Ar << GeneratedLuaSource;
        if (FormatVersion >= 2)
        {
            Ar << LastGoodGeneratedLuaSource;
        }
        else
        {
            LastGoodGeneratedLuaSource = bLastCompileSucceeded ? GeneratedLuaSource : FString();
        }
        if (FormatVersion >= 5)
        {
            uint32 DebugNodeCount = 0;
            Ar << DebugNodeCount;
            for (uint32 Index = 0; Index < DebugNodeCount; ++Index)
            {
                uint32 NodeId             = 0;
                bool   bBreakpointEnabled = false;
                Ar << NodeId;
                Ar << bBreakpointEnabled;
                for (FLuaBlueprintNode& Node : Nodes)
                {
                    if (Node.NodeId == NodeId)
                    {
                        Node.bBreakpointEnabled = bBreakpointEnabled;
                        break;
                    }
                }
            }
        }
    }
    else
    {
        // Backward compatible path for the original LuaBlueprint asset layout where the
        // first serialized uint32 was NextId. Keep graph data, force recompilation, and
        // preserve the stored generated Lua as a last-known-good fallback while the new
        // compiler/validator migrates the graph.
        NextId = First;
        Ar << Nodes;
        Ar << Links;
        Ar << Variables;
        Ar << GeneratedLuaSource;
        LastGoodGeneratedLuaSource  = GeneratedLuaSource;
        LastCompiledCompilerVersion = 0;
        Version                     = 0;
        RuntimeVersion              = 0;
        bLastCompileSucceeded       = !GeneratedLuaSource.empty();
    }

    if (Ar.IsLoading())
    {
        for (FLuaBlueprintVariable& Variable : Variables)
        {
            Variable.Type = NormalizeLuaBlueprintVariableType(Variable.Type);
        }
        RefreshAllNodePinTypes();
        RemoveInvalidLinks();
        bCompileDirty = GeneratedLuaSource.empty() || LastCompiledCompilerVersion != LuaBlueprintCompilerVersion;
        Diagnostics.clear();
    }
}

void ULuaBlueprintAsset::AddReferencedObjects(FReferenceCollector& Collector)
{
    UObject::AddReferencedObjects(Collector);
}

void ULuaBlueprintAsset::SetCompileResult(
    const FString&                    InSource,
    TArray<FLuaBlueprintDiagnostic>&& InDiagnostics,
    bool                              bSuccess
)
{
    GeneratedLuaSource          = InSource;
    Diagnostics                 = std::move(InDiagnostics);
    bLastCompileSucceeded       = bSuccess;
    LastCompiledCompilerVersion = LuaBlueprintCompilerVersion;
    bCompileDirty               = false;

    if (bSuccess)
    {
        LastGoodGeneratedLuaSource = InSource;
    }
}

void ULuaBlueprintAsset::ApplyResolvedPinTypesForLink(uint32 FromPinId, uint32 ToPinId)
{
    FLuaBlueprintPin* From = FindPin(FromPinId);
    FLuaBlueprintPin* To   = FindPin(ToPinId);
    if (!From || !To) return;
    if (From->Type == ELuaBlueprintPinType::Exec || To->Type == ELuaBlueprintPinType::Exec) return;

    // Any pin 은 링크되는 순간 반대편 concrete type 을 받아 UI inline editor 와 후속 메뉴가 즉시 바뀐다.
    if (To->Type == ELuaBlueprintPinType::Any && IsConcreteDataPinType(From->Type))
    {
        To->Type = From->Type;
    }
    if (From->Type == ELuaBlueprintPinType::Any && IsConcreteDataPinType(To->Type))
    {
        From->Type = To->Type;
    }

    if (FLuaBlueprintNode* FromNode = FindNode(From->OwningNodeId))
    {
        RefreshNodePinTypes(*FromNode);
    }
    if (FLuaBlueprintNode* ToNode = FindNode(To->OwningNodeId))
    {
        RefreshNodePinTypes(*ToNode);
    }
}