#include "LuaBlueprintComponent.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/Camera/CameraComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/World.h"
#include "Runtime/Engine.h"
#include "Lua/LuaScriptManager.h"
#include "Lua/LuaDebugManager.h"
#include "LuaBlueprint/LuaBlueprintAsset.h"
#include "LuaBlueprint/LuaBlueprintManager.h"
#include "Input/InputKeyCodes.h"
#include "Object/GarbageCollection.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <cstring>
#include <cmath>

ULuaBlueprintComponent::ULuaBlueprintComponent()  = default;
ULuaBlueprintComponent::~ULuaBlueprintComponent() = default;

void ULuaBlueprintComponent::SetBlueprintPath(const FString& InPath)
{
    if (BlueprintPath == InPath)
    {
        return;
    }

    BlueprintPath                  = InPath;
    BlueprintAsset                 = nullptr;
    LoadedBlueprintVersion         = 0;
    LoadedBlueprintRuntimeVersion  = 0;

    if (Env.valid() || LuaBeginPlay || LuaTick || LuaEndPlay)
    {
        ReloadBlueprint();
    }
}

bool ULuaBlueprintComponent::ReloadBlueprint()
{
    ClearInputBindings();
    ClearCollisionBindings();
    const bool bInitialized = InitializeLua();
    if (bInitialized)
    {
        BindOwnerCollisionEvents();
        bInputBindingPending = !BindInputEvents();
    }
    return bInitialized;
}

bool ULuaBlueprintComponent::CallFunction(const FString& FunctionName)
{
    if (!Env.valid())
    {
        return false;
    }

    sol::object Target = Env[FunctionName.c_str()];
    if (!Target.valid() || Target.get_type() != sol::type::function)
    {
        return false;
    }

    sol::protected_function Fn = Target;
    bool                    bOk;
    {
        FLuaCallScope                  Scope(this);
        sol::protected_function_result Result = Fn();
        bOk                                   = Result.valid();
        if (!bOk)
        {
            sol::error Err = Result;
            UE_LOG("LuaBlueprint %s error in %s: %s", FunctionName.c_str(), GetRuntimeName().c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        }
    }
    return bOk;
}




sol::environment ULuaBlueprintComponent::CreateExternalLuaEnvironment(const FString& DebugName, uint32 Generation)
{
    sol::state& Lua = FLuaScriptManager::GetState();
    sol::environment ExternalEnv(Lua, sol::create, Lua.globals());
    sol::table ObjectVars = Lua.create_table();

    ExternalEnv["obj"] = GetOwner();
    ExternalEnv["this"] = this;
    ExternalEnv["component"] = this;
    ExternalEnv["__bp_external_object_vars"] = ObjectVars;

    ExternalEnv.set_function(
        "BP_InitVar",
        [ObjectVars](const FString& Name, bool /*bStrong*/) mutable
        {
            if (!Name.empty())
            {
                sol::object Existing = ObjectVars[Name.c_str()];
                if (!Existing.valid() || Existing.get_type() == sol::type::nil)
                {
                    ObjectVars[Name.c_str()] = sol::nil;
                }
            }
        }
    );
    ExternalEnv.set_function(
        "BP_SetVar",
        [ObjectVars](const FString& Name, sol::object Value) mutable
        {
            if (!Name.empty())
            {
                ObjectVars[Name.c_str()] = Value;
            }
        }
    );
    ExternalEnv.set_function(
        "BP_GetVar",
        [ObjectVars](const FString& Name) mutable -> UObject*
        {
            if (Name.empty())
            {
                return nullptr;
            }
            sol::object Value = ObjectVars[Name.c_str()];
            if (!Value.valid() || Value.get_type() == sol::type::nil || !Value.is<UObject*>())
            {
                return nullptr;
            }
            return Value.as<UObject*>();
        }
    );
    ExternalEnv.set_function(
        "BP_Delay",
        [this, Generation](float Seconds, sol::protected_function Callback)
        {
            ScheduleLuaDelay(Seconds, Callback, Generation);
        }
    );
    ExternalEnv.set_function(
        "BP_ToStringValue",
        [](sol::this_state State, sol::object Value) -> FString
        {
            return FLuaDebugManager::CoerceLuaValueToString(State, Value);
        }
    );
    ExternalEnv.set_function(
        "BP_ToDisplayValue",
        [](sol::this_state State, sol::object Value) -> FString
        {
            return FLuaDebugManager::DescribeLuaValueForDisplay(State, Value);
        }
    );
    ExternalEnv.set_function(
        "BP_DebugNode",
        [this, DebugName](sol::this_state State, uint32 NodeId, const FString& NodeName, const FString& SourceName, int32 Line, int32 Depth, sol::object VarsObject, sol::object NodeValuesObject) -> bool
        {
            lua_State* RawLuaState = State;
            return FLuaDebugManager::OnLuaBlueprintNode(
                RawLuaState,
                this,
                GetRuntimeName(),
                DebugName.empty() ? GetDebugBlueprintPath() : DebugName,
                NodeId,
                NodeName,
                SourceName,
                Line,
                Depth,
                VarsObject,
                NodeValuesObject
            );
        }
    );

    return ExternalEnv;
}

bool ULuaBlueprintComponent::LoadExternalLuaBlueprintRuntime(const FString& InBlueprintPath, sol::environment& OutEnv, FString& OutDebugName)
{
    const FString NormalizedPath = FPaths::MakeProjectRelative(InBlueprintPath);
    if (NormalizedPath.empty() || NormalizedPath == "None")
    {
        return false;
    }

    const FString Key = FString("BP:") + NormalizedPath;
    for (FLuaBlueprintExternalRuntime& Runtime : ExternalRuntimes)
    {
        if (Runtime.Key == Key && Runtime.Env.valid() && Runtime.BlueprintAsset && IsValid(Runtime.BlueprintAsset) &&
            Runtime.LoadedRuntimeVersion == Runtime.BlueprintAsset->GetRuntimeVersion())
        {
            OutEnv = Runtime.Env;
            OutDebugName = Runtime.DebugName;
            return true;
        }
    }

    ULuaBlueprintAsset* Asset = FLuaBlueprintManager::Get().Load(NormalizedPath);
    if (!Asset || !Asset->HasRunnableLuaSource())
    {
        UE_LOG("LuaBlueprint external asset could not be loaded or has no runnable source: %s", NormalizedPath.c_str());
        return false;
    }

    const FString& Source = Asset->GetRuntimeLuaSource();
    if (Source.empty())
    {
        return false;
    }

    ExternalRuntimes.erase(
        std::remove_if(ExternalRuntimes.begin(), ExternalRuntimes.end(), [&Key](const FLuaBlueprintExternalRuntime& Existing)
        {
            return Existing.Key == Key;
        }),
        ExternalRuntimes.end()
    );

    FLuaBlueprintExternalRuntime Runtime;
    Runtime.Key = Key;
    Runtime.DebugName = NormalizedPath;
    Runtime.bBlueprint = true;
    Runtime.BlueprintAsset = Asset;
    Runtime.LoadedRuntimeVersion = Asset->GetRuntimeVersion();
    Runtime.Env = CreateExternalLuaEnvironment(NormalizedPath, LuaRuntimeGeneration);

    sol::state& Lua = FLuaScriptManager::GetState();
    sol::protected_function_result Result = Lua.safe_script(Source, Runtime.Env, sol::script_pass_on_error, NormalizedPath);
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("Failed to load external LuaBlueprint %s: %s", NormalizedPath.c_str(), Err.what());
        FLuaDebugManager::OnLuaError(NormalizedPath, Err.what(), true);
        return false;
    }

    ExternalRuntimes.push_back(std::move(Runtime));
    FLuaBlueprintExternalRuntime& Stored = ExternalRuntimes.back();
    OutEnv = Stored.Env;
    OutDebugName = Stored.DebugName;
    return true;
}

bool ULuaBlueprintComponent::LoadExternalLuaScriptRuntime(const FString& InScriptFile, sol::environment& OutEnv, FString& OutDebugName)
{
    if (InScriptFile.empty() || InScriptFile == "None")
    {
        return false;
    }

    const FString ResolvedPath = FLuaScriptManager::ResolveScriptPath(InScriptFile);
    const FString Key = FString("Lua:") + ResolvedPath;
    for (FLuaBlueprintExternalRuntime& Runtime : ExternalRuntimes)
    {
        if (Runtime.Key == Key && Runtime.Env.valid())
        {
            OutEnv = Runtime.Env;
            OutDebugName = Runtime.DebugName;
            return true;
        }
    }

    FString Content;
    if (!FLuaScriptManager::ReadScriptFileContent(InScriptFile, Content))
    {
        UE_LOG("Failed to read external Lua script %s", ResolvedPath.c_str());
        return false;
    }

    ExternalRuntimes.erase(
        std::remove_if(ExternalRuntimes.begin(), ExternalRuntimes.end(), [&Key](const FLuaBlueprintExternalRuntime& Existing)
        {
            return Existing.Key == Key;
        }),
        ExternalRuntimes.end()
    );

    FLuaBlueprintExternalRuntime Runtime;
    Runtime.Key = Key;
    Runtime.DebugName = ResolvedPath;
    Runtime.bBlueprint = false;
    Runtime.Env = CreateExternalLuaEnvironment(ResolvedPath, LuaRuntimeGeneration);

    sol::state& Lua = FLuaScriptManager::GetState();
    sol::protected_function_result Result = Lua.safe_script(Content, Runtime.Env, sol::script_pass_on_error, ResolvedPath);
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("Failed to load external Lua script %s: %s", ResolvedPath.c_str(), Err.what());
        FLuaDebugManager::OnLuaError(ResolvedPath, Err.what(), false);
        return false;
    }

    ExternalRuntimes.push_back(std::move(Runtime));
    FLuaBlueprintExternalRuntime& Stored = ExternalRuntimes.back();
    OutEnv = Stored.Env;
    OutDebugName = Stored.DebugName;
    return true;
}

bool ULuaBlueprintComponent::CallLuaBlueprintFileFunction(const FString& InBlueprintPath, const FString& FunctionName)
{
    sol::environment TargetEnv;
    FString DebugName;
    if (!LoadExternalLuaBlueprintRuntime(InBlueprintPath, TargetEnv, DebugName))
    {
        return false;
    }

    sol::object Target = TargetEnv[FunctionName.c_str()];
    if (!Target.valid() || Target.get_type() != sol::type::function)
    {
        return false;
    }

    sol::protected_function Fn = Target;
    FLuaCallScope Scope(this);
    sol::protected_function_result Result = Fn();
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("LuaBlueprint external call %s in %s failed: %s", FunctionName.c_str(), DebugName.c_str(), Err.what());
        FLuaDebugManager::OnLuaError(DebugName, Err.what(), true);
        return false;
    }
    return true;
}

bool ULuaBlueprintComponent::CallLuaScriptFileFunction(const FString& InScriptFile, const FString& FunctionName)
{
    sol::environment TargetEnv;
    FString DebugName;
    if (!LoadExternalLuaScriptRuntime(InScriptFile, TargetEnv, DebugName))
    {
        return false;
    }

    sol::object Target = TargetEnv[FunctionName.c_str()];
    if (!Target.valid() || Target.get_type() != sol::type::function)
    {
        return false;
    }

    sol::protected_function Fn = Target;
    FLuaCallScope Scope(this);
    sol::protected_function_result Result = Fn();
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("Lua script external call %s in %s failed: %s", FunctionName.c_str(), DebugName.c_str(), Err.what());
        FLuaDebugManager::OnLuaError(DebugName, Err.what(), false);
        return false;
    }
    return true;
}

void ULuaBlueprintComponent::ClearExternalLuaRuntimes()
{
    for (FLuaBlueprintExternalRuntime& Runtime : ExternalRuntimes)
    {
        Runtime.Env = sol::environment();
        Runtime.BlueprintAsset = nullptr;
        Runtime.LoadedRuntimeVersion = 0;
    }
    ExternalRuntimes.clear();
}

bool ULuaBlueprintComponent::ResumeLuaDebugExecution()
{
    if (!Env.valid())
    {
        return false;
    }

    sol::object ResumeObject = Env["__bp_debug_resume_all"];
    if (!ResumeObject.valid() || ResumeObject.get_type() != sol::type::function)
    {
        return false;
    }

    sol::protected_function ResumeFunction = ResumeObject;
    FLuaCallScope Scope(this);
    sol::protected_function_result Result = ResumeFunction();
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("LuaBlueprint debug resume error in %s: %s", GetRuntimeName().c_str(), Err.what());
        FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        return false;
    }
    return true;
}

void ULuaBlueprintComponent::BeginPlay()
{
    bEndPlayRouted = false;
    UActorComponent::BeginPlay();
    if (!ReloadBlueprint())
    {
        return;
    }

    if (bWantsBeginPlay && LuaBeginPlay)
    {
        FLuaCallScope                  Scope(this);
        sol::protected_function_result Result = LuaBeginPlay();
        if (!Result.valid())
        {
            sol::error Err = Result;
            UE_LOG("LuaBlueprint BeginPlay error in %s: %s", GetRuntimeName().c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        }
    }
}

void ULuaBlueprintComponent::RoutePostBeginPlay()
{
    if (!bWantsPostBeginPlay || !LuaPostBeginPlay)
    {
        return;
    }
    InvokeLuaNoArgEvent("PostBeginPlay", LuaPostBeginPlay);
}

void ULuaBlueprintComponent::RoutePostStartMatch()
{
    if (!bWantsPostStartMatch || !LuaPostStartMatch)
    {
        return;
    }
    InvokeLuaNoArgEvent("PostStartMatch", LuaPostStartMatch);
}

void ULuaBlueprintComponent::RoutePlayerCameraReady(
    APlayerController*    PlayerController,
    APlayerCameraManager* CameraManager,
    UCameraComponent*     ActiveCamera
    )
{
    if (!bWantsPlayerCameraReady || !LuaOnPlayerCameraReady || !Env.valid() || bPendingLuaCleanup)
    {
        return;
    }

    APlayerController* SafePlayerController = IsValid(PlayerController) ? PlayerController : nullptr;
    APlayerCameraManager* SafeCameraManager = IsValid(CameraManager) ? CameraManager : nullptr;
    UCameraComponent* SafeActiveCamera = IsValid(ActiveCamera) ? ActiveCamera : nullptr;

    FLuaCallScope                  Scope(this);
    sol::protected_function_result Result = LuaOnPlayerCameraReady(SafePlayerController, SafeCameraManager, SafeActiveCamera);
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("LuaBlueprint OnPlayerCameraReady error in %s: %s", GetRuntimeName().c_str(), Err.what());
        FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
    }
}

void ULuaBlueprintComponent::EndPlay()
{
    if (bEndPlayRouted)
    {
        return;
    }
    bEndPlayRouted = true;

    UActorComponent::EndPlay();
    ClearInputBindings();
    ClearCollisionBindings();
    if (LuaCallDepth > 0)
    {
        bPendingLuaEndPlay = true;
        bPendingLuaCleanup = true;
        return;
    }
    InvokeLuaEndPlay();
    ClearLuaRuntime();
}

void ULuaBlueprintComponent::RouteComponentDestroyed()
{
    ClearInputBindings();
    ClearCollisionBindings();
    UActorComponent::RouteComponentDestroyed();
}

void ULuaBlueprintComponent::BeginDestroy()
{
    // UActorComponent::BeginDestroy() routes virtual EndPlay() first. Do not clear
    // Lua runtime before that, or Lua EndPlay can be skipped on direct component
    // destruction / GC sweep paths.
    UActorComponent::BeginDestroy();
}

void ULuaBlueprintComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
    UActorComponent::AddReferencedObjects(Collector);
    Collector.AddReferencedObject(BlueprintAsset, "ULuaBlueprintComponent::BlueprintAsset");
    for (FLuaBlueprintExternalRuntime& Runtime : ExternalRuntimes)
    {
        if (Runtime.BlueprintAsset)
        {
            Collector.AddReferencedObject(Runtime.BlueprintAsset, "ULuaBlueprintComponent::ExternalBlueprintAsset");
        }
    }
    for (FLuaBlueprintRuntimeObjectVariable& Variable : RuntimeObjectVariables)
    {
        if (Variable.bStrong)
        {
            Collector.AddReferencedObject(Variable.StrongValue, "ULuaBlueprintComponent::RuntimeObjectVariable");
        }
    }
}

void ULuaBlueprintComponent::PreGetEditableProperties()
{
    UActorComponent::PreGetEditableProperties();
    LoadBlueprintAsset();
}

void ULuaBlueprintComponent::PostEditProperty(const char* PropertyName)
{
    UActorComponent::PostEditProperty(PropertyName);
    if (PropertyName && std::strcmp(PropertyName, "BlueprintPath") == 0)
    {
        BlueprintAsset                = nullptr;
        LoadedBlueprintVersion        = 0;
        LoadedBlueprintRuntimeVersion = 0;
        ReloadBlueprint();
    }
}

void ULuaBlueprintComponent::TickComponent(
    float                        DeltaTime,
    ELevelTick                   TickType,
    FActorComponentTickFunction& ThisTickFunction
    )
{
    UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (ULuaBlueprintAsset* ValidAsset = GetValidBlueprintAsset())
    {
        if (LoadedBlueprintRuntimeVersion != ValidAsset->GetRuntimeVersion())
        {
            ReloadBlueprint();
        }
    }

    if (bInputBindingPending && Env.valid())
    {
        BindInputEvents();
    }

    TickLuaDelays(DeltaTime);

    if (bWantsTick && LuaTick)
    {
        FLuaCallScope                  Scope(this);
        sol::protected_function_result Result = LuaTick(DeltaTime);
        if (!Result.valid())
        {
            sol::error Err = Result;
            UE_LOG("LuaBlueprint Tick error in %s: %s", GetRuntimeName().c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        }
    }
}

ULuaBlueprintAsset* ULuaBlueprintComponent::GetValidBlueprintAsset() const
{
    // BlueprintAsset 는 raw 포인터다. 보통 FLuaBlueprintManager 가 루트로 잡아 살아있지만,
    // ClearCache() 이후 GC sweep 이 에셋을 회수하면 dangling 이 될 수 있다. 접근 직전마다
    // 검증하여 stale 포인터를 정리한 뒤 valid 포인터(또는 nullptr)만 노출한다.
    const_cast<ULuaBlueprintComponent*>(this)->ClearInvalidBlueprintAsset();
    return BlueprintAsset;
}

void ULuaBlueprintComponent::ClearInvalidBlueprintAsset()
{
    // IsValid 는 GetAliveObjectFromAddress 기반이라 dangling/freed 포인터가 들어와도
    // deref 없이 안전하게 살아있는 UObject 인지 검사한다.
    if (BlueprintAsset && !IsValid(BlueprintAsset))
    {
        BlueprintAsset                = nullptr;
        LoadedBlueprintVersion        = 0;
        LoadedBlueprintRuntimeVersion = 0;
    }
}

bool ULuaBlueprintComponent::LoadBlueprintAsset()
{
    ClearInvalidBlueprintAsset();

    if (BlueprintAsset && LoadedBlueprintRuntimeVersion == BlueprintAsset->GetRuntimeVersion())
    {
        return BlueprintAsset->HasRunnableLuaSource();
    }

    if (!BlueprintPath.empty() && BlueprintPath != "None")
    {
        BlueprintAsset = FLuaBlueprintManager::Get().Load(BlueprintPath);
    }
    else
    {
        BlueprintAsset                = nullptr;
        LoadedBlueprintVersion        = 0;
        LoadedBlueprintRuntimeVersion = 0;
    }

    if (!BlueprintAsset)
    {
        if (!BlueprintPath.empty() && BlueprintPath != "None")
        {
            UE_LOG("LuaBlueprint asset could not be loaded: %s", BlueprintPath.c_str());
        }
        return false;
    }

    if (BlueprintAsset->IsCompileDirty())
    {
        BlueprintAsset->Compile();
    }

    if (BlueprintAsset->HasCompileErrors())
    {
        UE_LOG(
            "LuaBlueprint compile errors exist: %s. Runtime will use last good source if available.",
            BlueprintPath.c_str()
        );
    }

    if (!BlueprintAsset->HasRunnableLuaSource())
    {
        UE_LOG("LuaBlueprint has no runnable Lua source: %s", BlueprintPath.c_str());
        return false;
    }

    LoadedBlueprintVersion        = BlueprintAsset->GetVersion();
    LoadedBlueprintRuntimeVersion = BlueprintAsset->GetRuntimeVersion();
    return true;
}

bool ULuaBlueprintComponent::InitializeLua()
{
    ClearLuaRuntime();
    const uint32 ThisRuntimeGeneration = ++LuaRuntimeGeneration;

    if (!LoadBlueprintAsset())
    {
        return false;
    }

    const FString DebugBlueprintPath = GetDebugBlueprintPath();
    FLuaDebugManager::ClearNodeBreakpoints(DebugBlueprintPath);
    for (const FLuaBlueprintNode& Node : BlueprintAsset->GetNodes())
    {
        if (Node.bBreakpointEnabled)
        {
            FLuaDebugManager::SetNodeBreakpoint(DebugBlueprintPath, Node.NodeId, true);
        }
    }

    const FString& Source = BlueprintAsset->GetRuntimeLuaSource();
    if (Source.empty())
    {
        UE_LOG("LuaBlueprint has empty generated source: %s", BlueprintPath.c_str());
        return false;
    }

    sol::state& Lua  = FLuaScriptManager::GetState();
    Env              = sol::environment(Lua, sol::create, Lua.globals());
    Env["obj"]       = GetOwner();
    Env["this"]      = this;
    Env["component"] = this;
    Env.set_function(
        "BP_InitVar",
        [this](const FString& Name, bool bStrong)
        {
            InitRuntimeObjectVariable(Name, bStrong);
        }
    );
    Env.set_function(
        "BP_SetVar",
        [this](const FString& Name, sol::object Value)
        {
            SetRuntimeObjectVariable(Name, Value);
        }
    );
    Env.set_function(
        "BP_GetVar",
        [this](const FString& Name) -> UObject*
        {
            return GetRuntimeObjectVariable(Name);
        }
    );
    Env.set_function(
        "BP_Delay",
        [this, ThisRuntimeGeneration](float Seconds, sol::protected_function Callback)
        {
            ScheduleLuaDelay(Seconds, Callback, ThisRuntimeGeneration);
        }
    );
    Env.set_function(
        "BP_ToStringValue",
        [](sol::this_state State, sol::object Value) -> FString
        {
            return FLuaDebugManager::CoerceLuaValueToString(State, Value);
        }
    );
    Env.set_function(
        "BP_ToDisplayValue",
        [](sol::this_state State, sol::object Value) -> FString
        {
            return FLuaDebugManager::DescribeLuaValueForDisplay(State, Value);
        }
    );
    Env.set_function(
        "BP_DebugNode",
        [this](sol::this_state State, uint32 NodeId, const FString& NodeName, const FString& SourceName, int32 Line, int32 Depth, sol::object VarsObject, sol::object NodeValuesObject) -> bool
        {
            lua_State* RawLuaState = State;
            return FLuaDebugManager::OnLuaBlueprintNode(
                RawLuaState,
                this,
                GetRuntimeName(),
                GetDebugBlueprintPath(),
                NodeId,
                NodeName,
                SourceName,
                Line,
                Depth,
                VarsObject,
                NodeValuesObject
            );
        }
    );

    InitializeRuntimeObjectVariables();

    const FString                  ChunkName = BlueprintPath.empty() ? GetRuntimeName() : BlueprintPath;
    sol::protected_function_result Result    = Lua.safe_script(Source, Env, sol::script_pass_on_error, ChunkName);
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("Failed to load LuaBlueprint %s: %s", ChunkName.c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        ClearLuaRuntime();
        return false;
    }

    bWantsBeginPlay         = ReadEventFlag("BeginPlay");
    bWantsPostBeginPlay     = ReadEventFlag("PostBeginPlay");
    bWantsTick              = ReadEventFlag("Tick");
    bWantsPostStartMatch    = ReadEventFlag("PostStartMatch");
    bWantsPlayerCameraReady = ReadEventFlag("OnPlayerCameraReady");
    bWantsEndPlay           = ReadEventFlag("EndPlay");
    bWantsOverlap           = ReadEventFlag("OnOverlap");
    bWantsEndOverlap        = ReadEventFlag("OnEndOverlap");
    bWantsHit               = ReadEventFlag("OnHit");
    bWantsEndHit            = ReadEventFlag("OnEndHit");
    bHasCalledLuaEndPlay = false;
    bPendingLuaEndPlay   = false;

    LuaBeginPlay           = sol::nil;
    LuaPostBeginPlay       = sol::nil;
    LuaTick                = sol::nil;
    LuaPostStartMatch      = sol::nil;
    LuaOnPlayerCameraReady = sol::nil;
    LuaEndPlay             = sol::nil;
    LuaOnOverlap           = sol::nil;
    LuaOnEndOverlap        = sol::nil;
    LuaOnHit               = sol::nil;
    LuaOnEndHit            = sol::nil;
    if (bWantsBeginPlay) LuaBeginPlay = Env["BeginPlay"];
    if (bWantsPostBeginPlay) LuaPostBeginPlay = Env["PostBeginPlay"];
    if (bWantsTick) LuaTick = Env["Tick"];
    if (bWantsPostStartMatch) LuaPostStartMatch = Env["PostStartMatch"];
    if (bWantsPlayerCameraReady) LuaOnPlayerCameraReady = Env["OnPlayerCameraReady"];
    if (bWantsEndPlay) LuaEndPlay = Env["EndPlay"];
    if (bWantsOverlap) LuaOnOverlap = Env["OnOverlap"];
    if (bWantsEndOverlap) LuaOnEndOverlap = Env["OnEndOverlap"];
    if (bWantsHit) LuaOnHit = Env["OnHit"];
    if (bWantsEndHit) LuaOnEndHit = Env["OnEndHit"];
    LoadedBlueprintVersion        = BlueprintAsset->GetVersion();
    LoadedBlueprintRuntimeVersion = BlueprintAsset->GetRuntimeVersion();
    return true;
}

void ULuaBlueprintComponent::ReleaseLuaRuntimeForShutdown()
{
    // lua_State 가 아직 valid 한 동안(FLuaScriptManager::Shutdown 의 Lua.reset() 이전) 호출되어,
    // 보유 중인 sol 핸들을 지금 deref 해 둔다. 이후 최종 GC sweep 이 이 컴포넌트를 destroy 하며
    // ClearLuaRuntime 을 다시 타더라도, 핸들이 비어 있어 닫힌 lua_State 를 건드리지 않는다.
    ClearLuaRuntime();
}

void ULuaBlueprintComponent::ClearLuaRuntime()
{
    ClearInputBindings();
    ++LuaRuntimeGeneration;
    LuaBeginPlay           = sol::nil;
    LuaPostBeginPlay       = sol::nil;
    LuaTick                = sol::nil;
    LuaPostStartMatch      = sol::nil;
    LuaOnPlayerCameraReady = sol::nil;
    LuaEndPlay             = sol::nil;
    LuaOnOverlap           = sol::nil;
    LuaOnEndOverlap        = sol::nil;
    LuaOnHit               = sol::nil;
    LuaOnEndHit            = sol::nil;
    Env             = sol::environment();
    RuntimeObjectVariables.clear();
    PendingLuaDelays.clear();
    ClearExternalLuaRuntimes();
    bWantsBeginPlay         = false;
    bWantsPostBeginPlay     = false;
    bWantsTick              = false;
    bWantsPostStartMatch    = false;
    bWantsPlayerCameraReady = false;
    bWantsEndPlay           = false;
    bWantsOverlap           = false;
    bWantsEndOverlap        = false;
    bWantsHit               = false;
    bWantsEndHit            = false;
    bHasCalledLuaEndPlay = false;
    bPendingLuaEndPlay   = false;
    bPendingLuaCleanup   = false;
    bInputBindingPending = false;
    bInputBindingPendingLogEmitted = false;
}

namespace
{
    template<typename T>
    T LuaBlueprintTableGetOr(const sol::table& Table, const char* Key, const T& DefaultValue)
    {
        if (!Key)
        {
            return DefaultValue;
        }

        sol::object Value = Table[Key];
        if (!Value.valid() || Value.get_type() == sol::type::nil)
        {
            return DefaultValue;
        }

        return Value.is<T>() ? Value.as<T>() : DefaultValue;
    }

    EInputAxisSourceType LuaBlueprintAxisSourceFromString(const FString& Source)
    {
        if (Source == "MouseX") return EInputAxisSourceType::MouseX;
        if (Source == "MouseY") return EInputAxisSourceType::MouseY;
        if (Source == "MouseWheel") return EInputAxisSourceType::MouseWheel;
        return EInputAxisSourceType::Key;
    }

    int32 LuaBlueprintResolveBindingKey(const sol::table& Binding)
    {
        const FString KeyName = LuaBlueprintTableGetOr<FString>(Binding, "KeyName", "");
        const int32 Resolved = ResolveInputKeyCode(KeyName);
        if (Resolved != 0 || !KeyName.empty())
        {
            return Resolved;
        }

        // Backward compatibility for older generated blueprints that emitted numeric Key.
        return LuaBlueprintTableGetOr<int>(Binding, "Key", 0);
    }
}

bool ULuaBlueprintComponent::BindInputEvents()
{
    ClearInputBindings();

    if (!Env.valid())
    {
        bInputBindingPending = false;
        return true;
    }

    sol::object BindingsObject = Env["__input_bindings"];
    if (!BindingsObject.valid() || BindingsObject.get_type() != sol::type::table)
    {
        bInputBindingPending = false;
        return true;
    }

    sol::table Bindings = BindingsObject.as<sol::table>();
    bool bHasBindings = false;
    for (auto&& Entry : Bindings)
    {
        sol::object Value = Entry.second;
        if (Value.valid() && Value.get_type() == sol::type::table)
        {
            bHasBindings = true;
            break;
        }
    }

    if (!bHasBindings)
    {
        bInputBindingPending = false;
        return true;
    }

    AActor* OwnerActor = GetOwner();
    APawn* OwnerPawn = Cast<APawn>(OwnerActor);
    UInputComponent* InputComponent = nullptr;

    // Prefer the owner pawn's input component even if it is not possessed yet.
    // Possession only controls when ProcessPlayerInput starts running; it should not
    // prevent bindings from being registered ahead of time.
    if (OwnerPawn)
    {
        InputComponent = OwnerPawn->GetInputComponent();
    }

    // Actor-owned LuaBlueprints can still route input through the currently possessed pawn.
    // This is a fallback, not an Actor->Pawn type conversion inside the graph.
    if (!InputComponent && GEngine && GEngine->GetWorld())
    {
        if (APlayerController* PlayerController = GEngine->GetWorld()->GetFirstPlayerController())
        {
            if (APawn* PossessedPawn = PlayerController->GetPossessedPawn())
            {
                InputComponent = PossessedPawn->GetInputComponent();
            }
        }
    }

    if (!InputComponent)
    {
        bInputBindingPending = true;
        if (!bInputBindingPendingLogEmitted)
        {
            if (OwnerPawn)
            {
                UE_LOG(
                    "LuaBlueprint input bindings pending in %s: InputComponent is not available yet. OwnerPawn=%s Possessed=%d",
                    GetRuntimeName().c_str(),
                    OwnerPawn->GetName().c_str(),
                    OwnerPawn->IsPossessed() ? 1 : 0
                );
            }
            else
            {
                UE_LOG(
                    "LuaBlueprint input bindings pending in %s: owner is not APawn and no possessed pawn InputComponent is available. Owner=%s",
                    GetRuntimeName().c_str(),
                    OwnerActor ? OwnerActor->GetName().c_str() : "(null)"
                );
            }
            bInputBindingPendingLogEmitted = true;
        }
        return false;
    }

    const void* OwnerKey = GetInputBindingOwnerKey();
    const uint32 Generation = LuaRuntimeGeneration;
    TWeakObjectPtr<ULuaBlueprintComponent> WeakThis(this);
    int32 BoundCount = 0;

    for (auto&& Entry : Bindings)
    {
        sol::object Value = Entry.second;
        if (!Value.valid() || Value.get_type() != sol::type::table)
        {
            continue;
        }

        sol::table Binding = Value.as<sol::table>();
        const FString Kind = LuaBlueprintTableGetOr<FString>(Binding, "Kind", "");
        const FString Name = LuaBlueprintTableGetOr<FString>(Binding, "Name", "");
        const FString FunctionName = LuaBlueprintTableGetOr<FString>(Binding, "Function", "");
        if (Name.empty() || FunctionName.empty())
        {
            continue;
        }

        sol::object FunctionObject = Env[FunctionName.c_str()];
        if (!FunctionObject.valid() || FunctionObject.get_type() != sol::type::function)
        {
            UE_LOG(
                "LuaBlueprint input binding skipped in %s: generated function '%s' is missing",
                GetRuntimeName().c_str(),
                FunctionName.c_str()
            );
            continue;
        }

        sol::protected_function Callback = FunctionObject;
        if (Kind == "Action")
        {
            const int Key = LuaBlueprintResolveBindingKey(Binding);
            if (Key != 0)
            {
                InputComponent->AddActionMappingForOwner(OwnerKey, Name, Key);
            }

            const FString EventText = LuaBlueprintTableGetOr<FString>(Binding, "Event", "Pressed");
            const EInputEvent Event = (EventText == "Released") ? EInputEvent::Released : EInputEvent::Pressed;
            InputComponent->BindActionForOwner(OwnerKey, Name, Event, [WeakThis, Generation, Callback]() mutable
            {
                ULuaBlueprintComponent* Owner = WeakThis.Get();
                if (!Owner || !Owner->IsLuaRuntimeGenerationValid(Generation))
                {
                    return;
                }

                FLuaCallScope Scope(Owner);
                sol::protected_function_result Result = Callback();
                if (!Result.valid())
                {
                    sol::error Err = Result;
                    UE_LOG("LuaBlueprint input action error in %s: %s", Owner->GetRuntimeName().c_str(), Err.what());
                }
            });
            ++BoundCount;
        }
        else if (Kind == "Axis")
        {
            const FString Source = LuaBlueprintTableGetOr<FString>(Binding, "Source", "Key");
            const int Key = LuaBlueprintResolveBindingKey(Binding);
            const float Scale = LuaBlueprintTableGetOr<float>(Binding, "Scale", 1.0f);
            const EInputAxisSourceType AxisSource = LuaBlueprintAxisSourceFromString(Source);
            if (AxisSource == EInputAxisSourceType::Key)
            {
                if (Key != 0)
                {
                    InputComponent->AddAxisMappingForOwner(OwnerKey, Name, Key, Scale);
                }
            }
            else
            {
                InputComponent->AddMouseAxisMappingForOwner(OwnerKey, Name, AxisSource, Scale);
            }

            InputComponent->BindAxisForOwner(OwnerKey, Name, [WeakThis, Generation, Callback](float AxisValue) mutable
            {
                // LuaBlueprint axis event nodes are authored like event callbacks, not low-level polling hooks.
                // UInputComponent evaluates axis bindings every frame, including value=0. Calling the generated
                // Lua function at zero makes graphs that ignore the Value pin run forever while no key is held.
                // Skipping near-zero values keeps key/mouse axis events event-like; continuous zero-state logic
                // should use Tick or explicit Input.IsDown polling instead.
                if (std::fabs(AxisValue) <= 0.000001f)
                {
                    return;
                }

                ULuaBlueprintComponent* Owner = WeakThis.Get();
                if (!Owner || !Owner->IsLuaRuntimeGenerationValid(Generation))
                {
                    return;
                }

                FLuaCallScope Scope(Owner);
                sol::protected_function_result Result = Callback(AxisValue);
                if (!Result.valid())
                {
                    sol::error Err = Result;
                    UE_LOG("LuaBlueprint input axis error in %s: %s", Owner->GetRuntimeName().c_str(), Err.what());
                }
            });
            ++BoundCount;
        }
    }

    BoundInputComponent = InputComponent;
    bInputBindingPending = false;
    bInputBindingPendingLogEmitted = false;
    UE_LOG(
        "LuaBlueprint input bindings ready in %s: %d binding(s) on %s",
        GetRuntimeName().c_str(),
        BoundCount,
        InputComponent->GetName().c_str()
    );
    return true;
}

void ULuaBlueprintComponent::ClearInputBindings()
{
    if (UInputComponent* InputComponent = BoundInputComponent.Get())
    {
        InputComponent->RemoveBindingsForOwner(GetInputBindingOwnerKey());
    }
    BoundInputComponent.Reset();
}

void ULuaBlueprintComponent::BindOwnerCollisionEvents()
{
    ClearCollisionBindings();

    if ((!bWantsOverlap || !LuaOnOverlap) && (!bWantsEndOverlap || !LuaOnEndOverlap) && (!bWantsHit || !LuaOnHit) && (!
        bWantsEndHit || !LuaOnEndHit))
    {
        return;
    }

    AActor* OwnerActor = GetOwner();
    if (!OwnerActor)
    {
        return;
    }

    for (UPrimitiveComponent* PrimitiveComponent : OwnerActor->GetPrimitiveComponents())
    {
        if (!PrimitiveComponent)
        {
            continue;
        }

        if (((bWantsOverlap && LuaOnOverlap) || (bWantsEndOverlap && LuaOnEndOverlap)) && PrimitiveComponent->
            GetGenerateOverlapEvents())
        {
            BoundOverlapComponents.push_back(PrimitiveComponent);
            BeginOverlapHandles.push_back(
                LuaOnOverlap
                ? PrimitiveComponent->OnComponentBeginOverlap.AddWeakUObject(this, &ULuaBlueprintComponent::HandleBeginOverlap)
                : FDelegateHandle()
            );
            EndOverlapHandles.push_back(
                LuaOnEndOverlap
                ? PrimitiveComponent->OnComponentEndOverlap.AddWeakUObject(this, &ULuaBlueprintComponent::HandleEndOverlap)
                : FDelegateHandle()
            );
        }

        if ((bWantsHit && LuaOnHit) || (bWantsEndHit && LuaOnEndHit))
        {
            BoundHitComponents.push_back(PrimitiveComponent);
            HitHandles.push_back(
                LuaOnHit ? PrimitiveComponent->OnComponentHit.AddWeakUObject(this, &ULuaBlueprintComponent::HandleHit)
                : FDelegateHandle()
            );
            EndHitHandles.push_back(
                LuaOnEndHit ? PrimitiveComponent->OnComponentEndHit.AddWeakUObject(this, &ULuaBlueprintComponent::HandleEndHit)
                : FDelegateHandle()
            );
        }
    }
}

void ULuaBlueprintComponent::ClearCollisionBindings()
{
    for (int32 Index = 0; Index < static_cast<int32>(BoundOverlapComponents.size()); ++Index)
    {
        UPrimitiveComponent* PrimitiveComponent = BoundOverlapComponents[Index];
        if (!PrimitiveComponent)
        {
            continue;
        }

        if (Index < static_cast<int32>(BeginOverlapHandles.size()) && BeginOverlapHandles[Index].IsValid())
        {
            PrimitiveComponent->OnComponentBeginOverlap.Remove(BeginOverlapHandles[Index]);
        }
        if (Index < static_cast<int32>(EndOverlapHandles.size()) && EndOverlapHandles[Index].IsValid())
        {
            PrimitiveComponent->OnComponentEndOverlap.Remove(EndOverlapHandles[Index]);
        }
    }
    BoundOverlapComponents.clear();
    BeginOverlapHandles.clear();
    EndOverlapHandles.clear();

    for (int32 Index = 0; Index < static_cast<int32>(BoundHitComponents.size()); ++Index)
    {
        UPrimitiveComponent* PrimitiveComponent = BoundHitComponents[Index];
        if (!PrimitiveComponent)
        {
            continue;
        }

        if (Index < static_cast<int32>(HitHandles.size()) && HitHandles[Index].IsValid())
        {
            PrimitiveComponent->OnComponentHit.Remove(HitHandles[Index]);
        }
        if (Index < static_cast<int32>(EndHitHandles.size()) && EndHitHandles[Index].IsValid())
        {
            PrimitiveComponent->OnComponentEndHit.Remove(EndHitHandles[Index]);
        }
    }
    BoundHitComponents.clear();
    HitHandles.clear();
    EndHitHandles.clear();
}

FString ULuaBlueprintComponent::GetRuntimeName() const
{
    if (!BlueprintPath.empty())
    {
        return BlueprintPath;
    }
    ULuaBlueprintAsset* ValidAsset = GetValidBlueprintAsset();
    if (ValidAsset && !ValidAsset->GetSourcePath().empty())
    {
        return ValidAsset->GetSourcePath();
    }
    return "TransientLuaBlueprint";
}

FString ULuaBlueprintComponent::GetDebugBlueprintPath() const
{
    const ULuaBlueprintAsset* Asset = GetValidBlueprintAsset();
    if (Asset && !Asset->GetSourcePath().empty())
    {
        return Asset->GetSourcePath();
    }
    return BlueprintPath;
}

void ULuaBlueprintComponent::InitializeRuntimeObjectVariables()
{
    RuntimeObjectVariables.clear();
    ULuaBlueprintAsset* ValidAsset = GetValidBlueprintAsset();
    if (!ValidAsset)
    {
        return;
    }

    for (const FLuaBlueprintVariable& Variable : ValidAsset->GetVariables())
    {
        if (Variable.Type == ELuaBlueprintPinType::Object)
        {
            InitRuntimeObjectVariable(Variable.Name.ToString(), Variable.bStrongObject);
        }
    }
}

void ULuaBlueprintComponent::InitRuntimeObjectVariable(const FString& Name, bool bStrong)
{
    if (Name.empty())
    {
        return;
    }

    for (FLuaBlueprintRuntimeObjectVariable& Variable : RuntimeObjectVariables)
    {
        if (Variable.Name == Name)
        {
            Variable.bStrong = bStrong;
            return;
        }
    }

    FLuaBlueprintRuntimeObjectVariable Variable;
    Variable.Name    = Name;
    Variable.bStrong = bStrong;
    RuntimeObjectVariables.push_back(std::move(Variable));
}

void ULuaBlueprintComponent::SetRuntimeObjectVariable(const FString& Name, sol::object Value)
{
    if (Name.empty())
    {
        return;
    }

    UObject* ObjectValue = nullptr;
    if (Value.valid() && Value.get_type() != sol::type::nil)
    {
        if (Value.is<UObject*>())
        {
            ObjectValue = Value.as<UObject*>();
        }
        else if (Value.is<AActor*>())
        {
            ObjectValue = Value.as<AActor*>();
        }
        else if (Value.is<UPrimitiveComponent*>())
        {
            ObjectValue = Value.as<UPrimitiveComponent*>();
        }
    }

    bool bFoundVariable = false;
    for (const FLuaBlueprintRuntimeObjectVariable& Existing : RuntimeObjectVariables)
    {
        if (Existing.Name == Name)
        {
            bFoundVariable = true;
            break;
        }
    }
    if (!bFoundVariable)
    {
        InitRuntimeObjectVariable(Name, false);
    }

    for (FLuaBlueprintRuntimeObjectVariable& Variable : RuntimeObjectVariables)
    {
        if (Variable.Name == Name)
        {
            if (Variable.bStrong)
            {
                Variable.StrongValue = ObjectValue;
                Variable.WeakValue.Reset();
            }
            else
            {
                Variable.WeakValue   = ObjectValue;
                Variable.StrongValue = nullptr;
            }
            return;
        }
    }
}

UObject* ULuaBlueprintComponent::GetRuntimeObjectVariable(const FString& Name) const
{
    for (const FLuaBlueprintRuntimeObjectVariable& Variable : RuntimeObjectVariables)
    {
        if (Variable.Name == Name)
        {
            return Variable.bStrong ? Variable.StrongValue : Variable.WeakValue.Get();
        }
    }
    return nullptr;
}

TArray<std::pair<FString, UObject*>> ULuaBlueprintComponent::GetRuntimeObjectVariableSnapshot() const
{
    TArray<std::pair<FString, UObject*>> Result;
    for (const FLuaBlueprintRuntimeObjectVariable& Variable : RuntimeObjectVariables)
    {
        UObject* Value = Variable.bStrong ? Variable.StrongValue : Variable.WeakValue.Get();
        Result.emplace_back(Variable.Name, Value);
    }
    return Result;
}

bool ULuaBlueprintComponent::IsLuaRuntimeGenerationValid(uint32 Generation) const
{
    return Env.valid() && LuaRuntimeGeneration == Generation && !bPendingLuaCleanup;
}

void ULuaBlueprintComponent::ScheduleLuaDelay(float Seconds, sol::protected_function Callback, uint32 Generation)
{
    if (!Callback.valid() || !IsLuaRuntimeGenerationValid(Generation))
    {
        return;
    }

    FLuaBlueprintDelayedCallback Entry;
    Entry.RemainingSeconds = std::max(0.0f, Seconds);
    Entry.Generation       = Generation;
    Entry.Callback         = Callback;
    PendingLuaDelays.push_back(std::move(Entry));
}

void ULuaBlueprintComponent::TickLuaDelays(float DeltaTime)
{
    if (PendingLuaDelays.empty())
    {
        return;
    }

    const float SafeDeltaTime = std::max(0.0f, DeltaTime);
    TArray<FLuaBlueprintDelayedCallback> ReadyCallbacks;

    for (int32 Index = 0; Index < static_cast<int32>(PendingLuaDelays.size());)
    {
        FLuaBlueprintDelayedCallback& Entry = PendingLuaDelays[Index];
        if (!IsLuaRuntimeGenerationValid(Entry.Generation) || !Entry.Callback.valid())
        {
            PendingLuaDelays.erase(PendingLuaDelays.begin() + Index);
            continue;
        }

        Entry.RemainingSeconds -= SafeDeltaTime;
        if (Entry.RemainingSeconds <= 0.0f)
        {
            ReadyCallbacks.push_back(std::move(Entry));
            PendingLuaDelays.erase(PendingLuaDelays.begin() + Index);
            continue;
        }

        ++Index;
    }

    for (FLuaBlueprintDelayedCallback& Entry : ReadyCallbacks)
    {
        if (!IsLuaRuntimeGenerationValid(Entry.Generation) || !Entry.Callback.valid())
        {
            continue;
        }

        FLuaCallScope                  Scope(this);
        sol::protected_function_result Result = Entry.Callback();
        if (!Result.valid())
        {
            sol::error Err = Result;
            UE_LOG("LuaBlueprint Delay callback error in %s: %s", GetRuntimeName().c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        }

        if (!IsLuaRuntimeGenerationValid(Entry.Generation))
        {
            break;
        }
    }
}

bool ULuaBlueprintComponent::InvokeLuaNoArgEvent(const char* EventName, sol::protected_function& Function)
{
    if (!EventName || !Env.valid() || bPendingLuaCleanup || !Function)
    {
        return false;
    }

    FLuaCallScope                  Scope(this);
    sol::protected_function_result Result = Function();
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("LuaBlueprint %s error in %s: %s", EventName, GetRuntimeName().c_str(), Err.what());
        FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        return false;
    }
    return true;
}

bool ULuaBlueprintComponent::ReadEventFlag(const char* EventName) const
{
    if (!EventName || !Env.valid())
    {
        return false;
    }

    sol::object EventsObject = Env["__events"];
    if (!EventsObject.valid() || EventsObject.get_type() != sol::type::table)
    {
        sol::object FunctionObject = Env[EventName];
        return FunctionObject.valid() && FunctionObject.get_type() == sol::type::function;
    }

    sol::table  Events = EventsObject.as<sol::table>();
    sol::object Flag   = Events[EventName];
    return Flag.valid() && Flag.get_type() == sol::type::boolean && Flag.as<bool>();
}

void ULuaBlueprintComponent::InvokeLuaEndPlay()
{
    if (bHasCalledLuaEndPlay || !bWantsEndPlay || !LuaEndPlay)
    {
        return;
    }

    bHasCalledLuaEndPlay = true;
    FLuaCallScope                  Scope(this);
    sol::protected_function_result Result = LuaEndPlay();
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("LuaBlueprint EndPlay error in %s: %s", GetRuntimeName().c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
    }
}

void ULuaBlueprintComponent::HandleDeferredLuaCleanup()
{
    if (LuaCallDepth != 0 || !bPendingLuaCleanup)
    {
        return;
    }

    const bool bShouldRunEndPlay = bPendingLuaEndPlay;
    bPendingLuaCleanup           = false;
    bPendingLuaEndPlay           = false;
    if (bShouldRunEndPlay)
    {
        InvokeLuaEndPlay();
    }
    ClearLuaRuntime();
}

void ULuaBlueprintComponent::HandleBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 /*OtherBodyIndex*/,
    bool /*bFromSweep*/,
    const FHitResult& /*SweepResult*/
    )
{
    if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnOverlap)
    {
        return;
    }

    AActor* SafeOtherActor = IsValid(OtherActor) ? OtherActor : nullptr;
    UPrimitiveComponent* SafeOverlappedComponent = IsValid(OverlappedComponent) ? OverlappedComponent : nullptr;
    UPrimitiveComponent* SafeOtherComp = IsValid(OtherComp) ? OtherComp : nullptr;
    {
        FLuaCallScope                  Scope(this);
        sol::protected_function_result Result = LuaOnOverlap(SafeOtherActor, SafeOverlappedComponent, SafeOtherComp);
        if (!Result.valid())
        {
            sol::error Err = Result;
            UE_LOG("LuaBlueprint OnOverlap error in %s: %s", GetRuntimeName().c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        }
    }
}

void ULuaBlueprintComponent::HandleEndOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 /*OtherBodyIndex*/
    )
{
    if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnEndOverlap)
    {
        return;
    }

    AActor* SafeOtherActor = IsValid(OtherActor) ? OtherActor : nullptr;
    UPrimitiveComponent* SafeOverlappedComponent = IsValid(OverlappedComponent) ? OverlappedComponent : nullptr;
    UPrimitiveComponent* SafeOtherComp = IsValid(OtherComp) ? OtherComp : nullptr;
    {
        FLuaCallScope                  Scope(this);
        sol::protected_function_result Result = LuaOnEndOverlap(SafeOtherActor, SafeOverlappedComponent, SafeOtherComp);
        if (!Result.valid())
        {
            sol::error Err = Result;
            UE_LOG("LuaBlueprint OnEndOverlap error in %s: %s", GetRuntimeName().c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        }
    }
}

void ULuaBlueprintComponent::HandleHit(
    UPrimitiveComponent* HitComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    FVector              NormalImpulse,
    const FHitResult&    HitResult
    )
{
    if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnHit)
    {
        return;
    }

    AActor* SafeOtherActor = IsValid(OtherActor) ? OtherActor : nullptr;
    UPrimitiveComponent* SafeHitComponent = IsValid(HitComponent) ? HitComponent : nullptr;
    UPrimitiveComponent* SafeOtherComp = IsValid(OtherComp) ? OtherComp : nullptr;
    {
        FLuaCallScope                  Scope(this);
        sol::protected_function_result Result = LuaOnHit(SafeOtherActor, SafeHitComponent, SafeOtherComp, NormalImpulse, HitResult);
        if (!Result.valid())
        {
            sol::error Err = Result;
            UE_LOG("LuaBlueprint OnHit error in %s: %s", GetRuntimeName().c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        }
    }
}

void ULuaBlueprintComponent::HandleEndHit(
    UPrimitiveComponent* HitComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp
    )
{
    if (!IsValid(this) || bPendingLuaCleanup || !Env.valid() || !LuaOnEndHit)
    {
        return;
    }

    AActor* SafeOtherActor = IsValid(OtherActor) ? OtherActor : nullptr;
    UPrimitiveComponent* SafeHitComponent = IsValid(HitComponent) ? HitComponent : nullptr;
    UPrimitiveComponent* SafeOtherComp = IsValid(OtherComp) ? OtherComp : nullptr;
    {
        FLuaCallScope                  Scope(this);
        sol::protected_function_result Result = LuaOnEndHit(SafeOtherActor, SafeHitComponent, SafeOtherComp);
        if (!Result.valid())
        {
            sol::error Err = Result;
            UE_LOG("LuaBlueprint OnEndHit error in %s: %s", GetRuntimeName().c_str(), Err.what());
            FLuaDebugManager::OnLuaError(GetRuntimeName(), Err.what(), true);
        }
    }
}
