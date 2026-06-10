#include "Lua/LuaDebugManager.h"

#include "Component/Script/LuaBlueprintComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Component/ActorComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Math/Vector.h"
#include "Object/Object.h"
#include "Platform/Paths.h"
#include "Runtime/Engine.h"

#include <algorithm>
#include <ctime>
#include <cstring>
#include <cmath>
#include <sstream>
#include <cstdio>
#include <utility>

namespace
{
    FString NormalizeLuaDebugPath(const FString& InPath)
    {
        FString Path = InPath;
        if (!Path.empty() && Path.front() == '@')
        {
            Path.erase(Path.begin());
        }
        Path = FPaths::MakeProjectRelative(Path);
        std::replace(Path.begin(), Path.end(), '\\', '/');
        return Path;
    }

    int LuaDebugAbsIndex(lua_State* L, int Index)
    {
        return (Index > 0 || Index <= LUA_REGISTRYINDEX) ? Index : lua_gettop(L) + Index + 1;
    }

    bool IsInternalLuaDebugName(const char* Name)
    {
        if (!Name || !Name[0])
        {
            return true;
        }
        if (Name[0] == '(')
        {
            return true;
        }
        // Generated LuaBlueprint helpers are implementation details; showing them as upvalues
        // makes the debugger look like it is exposing meaningless function pointers.
        return std::strncmp(Name, "__bp_", 5) == 0;
    }

    FString FormatLuaDebugFloat(float Value)
    {
        char Buffer[64];
        std::snprintf(Buffer, sizeof(Buffer), "%.3f", static_cast<double>(Value));
        FString Text = Buffer;
        while (Text.size() > 1 && Text.back() == '0')
        {
            Text.pop_back();
        }
        if (!Text.empty() && Text.back() == '.')
        {
            Text.pop_back();
        }
        return Text;
    }

    FString DescribeVector(const FVector& Value)
    {
        return FString("(") + FormatLuaDebugFloat(Value.X) + ", " + FormatLuaDebugFloat(Value.Y) + ", " + FormatLuaDebugFloat(Value.Z) + ")";
    }

    FString DescribeVector4(const FVector4& Value)
    {
        return FString("(") + FormatLuaDebugFloat(Value.X) + ", " + FormatLuaDebugFloat(Value.Y) + ", " + FormatLuaDebugFloat(Value.Z) + ", " + FormatLuaDebugFloat(Value.W) + ")";
    }

    bool LooksLikeDefaultLuaPointerString(const FString& Text)
    {
        return Text.rfind("userdata: ", 0) == 0 ||
               Text.rfind("function: ", 0) == 0 ||
               Text.rfind("thread: ", 0) == 0 ||
               Text.rfind("<userdata ", 0) == 0 ||
               Text.rfind("<function ", 0) == 0 ||
               Text.rfind("<thread ", 0) == 0;
    }

    FString DescribeUObjectForLuaDebug(UObject* Object)
    {
        if (!Object)
        {
            return "nil";
        }
        if (!IsValid(Object))
        {
            return "Invalid Object";
        }

        const FString ClassName = Object->GetClass() ? FString(Object->GetClass()->GetName()) : FString("Object");
        FString Out = ClassName + " '" + Object->GetName() + "'";
        if (const AActor* Actor = Cast<AActor>(Object))
        {
            Out += " Loc=" + DescribeVector(Actor->GetActorLocation());
        }
        else if (const UActorComponent* Component = Cast<UActorComponent>(Object))
        {
            if (AActor* Owner = Component->GetOwner())
            {
                Out += " Owner=";
                Out += Owner->GetName();
            }
        }
        return Out;
    }

    bool TryDescribeSolObjectForLuaDebug(lua_State* L, int Index, FString& OutValue, FString& OutTypeName)
    {
        if (!L)
        {
            return false;
        }

        Index = LuaDebugAbsIndex(L, Index);
        try
        {
            sol::object Object = sol::stack::get<sol::object>(L, Index);
            if (!Object.valid() || Object == sol::nil)
            {
                return false;
            }
            if (Object.is<std::string>())
            {
                OutValue = Object.as<std::string>();
                OutTypeName = "string";
                return true;
            }
            if (Object.is<UObject*>())
            {
                UObject* Value = Object.as<UObject*>();
                OutValue = DescribeUObjectForLuaDebug(Value);
                OutTypeName = Value && Value->GetClass() ? FString(Value->GetClass()->GetName()) : FString("Object");
                return true;
            }
            if (Object.is<AActor*>())
            {
                AActor* Value = Object.as<AActor*>();
                OutValue = DescribeUObjectForLuaDebug(Value);
                OutTypeName = Value && Value->GetClass() ? FString(Value->GetClass()->GetName()) : FString("Actor");
                return true;
            }
            if (Object.is<UActorComponent*>())
            {
                UActorComponent* Value = Object.as<UActorComponent*>();
                OutValue = DescribeUObjectForLuaDebug(Value);
                OutTypeName = Value && Value->GetClass() ? FString(Value->GetClass()->GetName()) : FString("ActorComponent");
                return true;
            }
            if (Object.is<FVector>())
            {
                OutValue = DescribeVector(Object.as<FVector>());
                OutTypeName = "Vector";
                return true;
            }
            if (Object.is<FVector4>())
            {
                OutValue = DescribeVector4(Object.as<FVector4>());
                OutTypeName = "Vector4";
                return true;
            }
        }
        catch (...)
        {
            return false;
        }
        return false;
    }

    void CopyLuaTableFields(lua_State* L, int SourceIndex, int TargetIndex, int32 MaxFields = 256)
    {
        SourceIndex = LuaDebugAbsIndex(L, SourceIndex);
        TargetIndex = LuaDebugAbsIndex(L, TargetIndex);
        if (!lua_istable(L, SourceIndex) || !lua_istable(L, TargetIndex))
        {
            return;
        }

        int32 Count = 0;
        lua_pushnil(L);
        while (lua_next(L, SourceIndex) != 0)
        {
            if (++Count > MaxFields)
            {
                lua_pop(L, 1);
                break;
            }
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_rawset(L, TargetIndex);
            lua_pop(L, 1);
        }
    }

    int BuildLuaDebugEvaluationEnvironment(lua_State* L, int32 StackLevel, int EnvTableRef)
    {
        if (!L)
        {
            return LUA_NOREF;
        }

        lua_newtable(L);
        const int EvalEnvIndex = LuaDebugAbsIndex(L, -1);

        if (EnvTableRef != LUA_NOREF)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, EnvTableRef);
            if (lua_istable(L, -1))
            {
                CopyLuaTableFields(L, -1, EvalEnvIndex);
                if (lua_getmetatable(L, -1))
                {
                    lua_setmetatable(L, EvalEnvIndex);
                }
                lua_getfield(L, -1, "__vars");
                if (lua_istable(L, -1))
                {
                    CopyLuaTableFields(L, -1, EvalEnvIndex);
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }

        lua_Debug DebugInfo;
        if (lua_getstack(L, StackLevel, &DebugInfo) != 0)
        {
            for (int LocalIndex = 1;; ++LocalIndex)
            {
                const char* LocalName = lua_getlocal(L, &DebugInfo, LocalIndex);
                if (!LocalName)
                {
                    break;
                }
                if (!IsInternalLuaDebugName(LocalName))
                {
                    lua_pushvalue(L, -1);
                    lua_setfield(L, EvalEnvIndex, LocalName);
                }
                lua_pop(L, 1);
            }

            if (lua_getinfo(L, "f", &DebugInfo) != 0)
            {
                const int FunctionIndex = LuaDebugAbsIndex(L, -1);
                for (int UpvalueIndex = 1;; ++UpvalueIndex)
                {
                    const char* UpvalueName = lua_getupvalue(L, FunctionIndex, UpvalueIndex);
                    if (!UpvalueName)
                    {
                        break;
                    }
                    if (std::strcmp(UpvalueName, "_ENV") != 0 && !IsInternalLuaDebugName(UpvalueName))
                    {
                        lua_pushvalue(L, -1);
                        lua_setfield(L, EvalEnvIndex, UpvalueName);
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
        }

        lua_pushvalue(L, EvalEnvIndex);
        lua_setfield(L, EvalEnvIndex, "_G");
        return luaL_ref(L, LUA_REGISTRYINDEX);
    }
}

std::mutex FLuaDebugManager::Mutex;
lua_State* FLuaDebugManager::LuaState = nullptr;
bool       FLuaDebugManager::bEnabled = true;
bool       FLuaDebugManager::bHookInstalled = false;
bool       FLuaDebugManager::bPaused = false;
ELuaDebugPauseReason FLuaDebugManager::PauseReason = ELuaDebugPauseReason::None;
FLuaDebugLocation    FLuaDebugManager::PausedLocation;
TWeakObjectPtr<ULuaBlueprintComponent> FLuaDebugManager::PausedBlueprintComponent;
ELuaDebugStepMode FLuaDebugManager::StepMode = ELuaDebugStepMode::None;
int32             FLuaDebugManager::StepStartDepth = 0;
bool              FLuaDebugManager::bPauseNextLine = false;
TMap<FString, bool> FLuaDebugManager::NodeBreakpoints;
TMap<FString, bool> FLuaDebugManager::SourceBreakpoints;
TArray<FLuaDebugTraceEntry> FLuaDebugManager::RecentTrace;
FLuaDebugValueSnapshot FLuaDebugManager::PausedSnapshot;
TArray<FString> FLuaDebugManager::WatchExpressions;
FLuaDebugEditorFocusRequest FLuaDebugManager::EditorFocusRequest;
uint64 FLuaDebugManager::EditorFocusSerial = 0;
bool FLuaDebugManager::bWorldPauseCaptured = false;
bool FLuaDebugManager::bWorldWasPaused = false;

void FLuaDebugManager::Initialize(lua_State* InLuaState)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    LuaState = InLuaState;
    bPaused = false;
    PauseReason = ELuaDebugPauseReason::None;
    StepMode = ELuaDebugStepMode::None;
    StepStartDepth = 0;
    bPauseNextLine = false;
    PausedBlueprintComponent = nullptr;
    PausedLocation = FLuaDebugLocation();
    RecentTrace.clear();
    PausedSnapshot = FLuaDebugValueSnapshot();
    EditorFocusRequest = FLuaDebugEditorFocusRequest();
    EditorFocusSerial = 0;
    bWorldPauseCaptured = false;
    bWorldWasPaused = false;
    if (LuaState && bEnabled)
    {
        lua_sethook(LuaState, &FLuaDebugManager::LuaLineHook, LUA_MASKLINE, 0);
        bHookInstalled = true;
    }
}

void FLuaDebugManager::Shutdown()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (LuaState && bHookInstalled)
    {
        lua_sethook(LuaState, nullptr, 0, 0);
    }
    LuaState = nullptr;
    bHookInstalled = false;
    bPaused = false;
    PauseReason = ELuaDebugPauseReason::None;
    StepMode = ELuaDebugStepMode::None;
    PausedBlueprintComponent = nullptr;
    NodeBreakpoints.clear();
    SourceBreakpoints.clear();
    RecentTrace.clear();
    PausedSnapshot = FLuaDebugValueSnapshot();
    WatchExpressions.clear();
    EditorFocusRequest = FLuaDebugEditorFocusRequest();
    EditorFocusSerial = 0;
    bWorldPauseCaptured = false;
    bWorldWasPaused = false;
}

void FLuaDebugManager::RegisterLuaBindings(sol::state& Lua)
{
    sol::table Debug = Lua.create_named_table("LuaDebug");
    Debug.set_function("SetEnabled", &FLuaDebugManager::SetEnabled);
    Debug.set_function("IsEnabled", &FLuaDebugManager::IsEnabled);
    Debug.set_function("Continue", &FLuaDebugManager::Continue);
    Debug.set_function("StepInto", &FLuaDebugManager::StepInto);
    Debug.set_function("StepOver", &FLuaDebugManager::StepOver);
    Debug.set_function("StepOut", &FLuaDebugManager::StepOut);
    Debug.set_function("PauseNextLine", &FLuaDebugManager::PauseNextLine);
    Debug.set_function("PauseNextNode", &FLuaDebugManager::PauseNextLine);
    Debug.set_function("AbortPause", &FLuaDebugManager::AbortPause);
    Debug.set_function("IsPaused", &FLuaDebugManager::IsPaused);
    Debug.set_function("SetBreakpoint", [](const FString& ChunkName, int32 Line)
    {
        FLuaDebugManager::SetSourceBreakpoint(ChunkName, Line, true);
    });
    Debug.set_function("SetBreakpointEnabled", [](const FString& ChunkName, int32 Line, bool bInEnabled)
    {
        FLuaDebugManager::SetSourceBreakpoint(ChunkName, Line, bInEnabled);
    });
    Debug.set_function("SetNodeBreakpoint", [](const FString& BlueprintPath, uint32 NodeId)
    {
        FLuaDebugManager::SetNodeBreakpoint(BlueprintPath, NodeId, true);
    });
    Debug.set_function("SetNodeBreakpointEnabled", [](const FString& BlueprintPath, uint32 NodeId, bool bInEnabled)
    {
        FLuaDebugManager::SetNodeBreakpoint(BlueprintPath, NodeId, bInEnabled);
    });
    Debug.set_function("ClearNodeBreakpoint", [](const FString& BlueprintPath, uint32 NodeId)
    {
        FLuaDebugManager::SetNodeBreakpoint(BlueprintPath, NodeId, false);
    });
    Debug.set_function("ClearBreakpoint", [](const FString& ChunkName, int32 Line)
    {
        FLuaDebugManager::SetSourceBreakpoint(ChunkName, Line, false);
    });
    Debug.set_function("ClearBreakpoints", []()
    {
        FLuaDebugManager::ClearSourceBreakpoints();
        FLuaDebugManager::ClearNodeBreakpoints();
    });
    Debug.set_function("GetPausedLocation", [](sol::this_state State) -> sol::table
    {
        sol::state_view L(State);
        const FLuaDebugLocation& Loc = FLuaDebugManager::GetPausedLocation();
        sol::table T = L.create_table();
        T["ChunkName"] = Loc.ChunkName;
        T["SourceName"] = Loc.SourceName;
        T["FunctionName"] = Loc.FunctionName;
        T["BlueprintPath"] = Loc.BlueprintPath;
        T["RuntimeName"] = Loc.RuntimeName;
        T["NodeName"] = Loc.NodeName;
        T["NodeId"] = Loc.NodeId;
        T["Line"] = Loc.Line;
        T["Depth"] = Loc.Depth;
        T["LuaBlueprint"] = Loc.bLuaBlueprint;
        return T;
    });
    Debug.set_function("AddWatch", &FLuaDebugManager::AddWatchExpression);
    Debug.set_function("RemoveWatch", &FLuaDebugManager::RemoveWatchExpression);
    Debug.set_function("ClearWatches", &FLuaDebugManager::ClearWatchExpressions);
}

void FLuaDebugManager::SetEnabled(bool bInEnabled)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    bEnabled = bInEnabled;
    if (!LuaState)
    {
        return;
    }
    if (bEnabled && !bHookInstalled)
    {
        lua_sethook(LuaState, &FLuaDebugManager::LuaLineHook, LUA_MASKLINE, 0);
        bHookInstalled = true;
    }
    else if (!bEnabled && bHookInstalled)
    {
        lua_sethook(LuaState, nullptr, 0, 0);
        bHookInstalled = false;
    }
}

bool FLuaDebugManager::IsEnabled()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return bEnabled;
}

void FLuaDebugManager::InstallLineHook(lua_State* InLuaState)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    LuaState = InLuaState;
    if (LuaState && bEnabled)
    {
        lua_sethook(LuaState, &FLuaDebugManager::LuaLineHook, LUA_MASKLINE, 0);
        bHookInstalled = true;
    }
}

void FLuaDebugManager::RemoveLineHook(lua_State* InLuaState)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (InLuaState && InLuaState == LuaState && bHookInstalled)
    {
        lua_sethook(InLuaState, nullptr, 0, 0);
        bHookInstalled = false;
    }
}

FString FLuaDebugManager::MakeNodeBreakpointKey(const FString& BlueprintPath, uint32 NodeId)
{
    return NormalizeLuaDebugPath(BlueprintPath) + "#" + std::to_string(NodeId);
}

FString FLuaDebugManager::MakeSourceBreakpointKey(const FString& ChunkName, int32 Line)
{
    return NormalizeLuaDebugPath(ChunkName) + ":" + std::to_string(Line);
}

bool FLuaDebugManager::ToggleNodeBreakpoint(const FString& BlueprintPath, uint32 NodeId)
{
    const bool bNewEnabled = !HasNodeBreakpoint(BlueprintPath, NodeId);
    SetNodeBreakpoint(BlueprintPath, NodeId, bNewEnabled);
    return bNewEnabled;
}

void FLuaDebugManager::SetNodeBreakpoint(const FString& BlueprintPath, uint32 NodeId, bool bInEnabled)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    const FString Key = MakeNodeBreakpointKey(BlueprintPath, NodeId);
    if (bInEnabled)
    {
        NodeBreakpoints[Key] = true;
    }
    else
    {
        NodeBreakpoints.erase(Key);
    }
}

bool FLuaDebugManager::HasNodeBreakpoint(const FString& BlueprintPath, uint32 NodeId)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    const FString Key = MakeNodeBreakpointKey(BlueprintPath, NodeId);
    auto It = NodeBreakpoints.find(Key);
    return It != NodeBreakpoints.end() && It->second;
}

void FLuaDebugManager::ClearNodeBreakpoints(const FString& BlueprintPath)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (BlueprintPath.empty())
    {
        NodeBreakpoints.clear();
        return;
    }
    const FString Prefix = BlueprintPath + "#";
    for (auto It = NodeBreakpoints.begin(); It != NodeBreakpoints.end();)
    {
        if (It->first.rfind(Prefix, 0) == 0)
        {
            It = NodeBreakpoints.erase(It);
        }
        else
        {
            ++It;
        }
    }
}

void FLuaDebugManager::SetSourceBreakpoint(const FString& ChunkName, int32 Line, bool bInEnabled)
{
    if (ChunkName.empty() || Line <= 0)
    {
        return;
    }
    std::lock_guard<std::mutex> Lock(Mutex);
    const FString Key = MakeSourceBreakpointKey(ChunkName, Line);
    if (bInEnabled)
    {
        SourceBreakpoints[Key] = true;
    }
    else
    {
        SourceBreakpoints.erase(Key);
    }
}

bool FLuaDebugManager::HasSourceBreakpoint(const FString& ChunkName, int32 Line)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    const FString Key = MakeSourceBreakpointKey(ChunkName, Line);
    auto It = SourceBreakpoints.find(Key);
    return It != SourceBreakpoints.end() && It->second;
}

void FLuaDebugManager::ClearSourceBreakpoints(const FString& ChunkName)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (ChunkName.empty())
    {
        SourceBreakpoints.clear();
        return;
    }
    const FString Prefix = ChunkName + ":";
    for (auto It = SourceBreakpoints.begin(); It != SourceBreakpoints.end();)
    {
        if (It->first.rfind(Prefix, 0) == 0)
        {
            It = SourceBreakpoints.erase(It);
        }
        else
        {
            ++It;
        }
    }
}

void FLuaDebugManager::RecordTrace(const FLuaDebugLocation& Location, const FString& Event)
{
    FLuaDebugTraceEntry Entry;
    Entry.Location = Location;
    Entry.Event = Event;
    Entry.TimestampSeconds = static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC);
    RecentTrace.push_back(std::move(Entry));
    constexpr size_t MaxTraceEntries = 512;
    if (RecentTrace.size() > MaxTraceEntries)
    {
        RecentTrace.erase(RecentTrace.begin(), RecentTrace.begin() + (RecentTrace.size() - MaxTraceEntries));
    }
}

bool FLuaDebugManager::ShouldPauseAtBlueprintNode(const FLuaDebugLocation& Location)
{
    if (bPauseNextLine)
    {
        bPauseNextLine = false;
        return true;
    }

    const FString Key = MakeNodeBreakpointKey(Location.BlueprintPath, Location.NodeId);
    auto BpIt = NodeBreakpoints.find(Key);
    if (BpIt != NodeBreakpoints.end() && BpIt->second)
    {
        return true;
    }

    switch (StepMode)
    {
    case ELuaDebugStepMode::Into:
        return true;
    case ELuaDebugStepMode::Over:
        return Location.Depth <= StepStartDepth;
    case ELuaDebugStepMode::Out:
        return Location.Depth < StepStartDepth;
    default:
        return false;
    }
}

bool FLuaDebugManager::ShouldPauseAtSourceLine(const FLuaDebugLocation& Location)
{
    const FString Key = MakeSourceBreakpointKey(Location.ChunkName.empty() ? Location.SourceName : Location.ChunkName, Location.Line);
    auto It = SourceBreakpoints.find(Key);
    return It != SourceBreakpoints.end() && It->second;
}

void FLuaDebugManager::PauseWorldIfNeeded()
{
    if (!GEngine || !GEngine->GetWorld())
    {
        return;
    }
    UWorld* World = GEngine->GetWorld();
    if (!bWorldPauseCaptured)
    {
        bWorldWasPaused = World->IsPaused();
        bWorldPauseCaptured = true;
    }
    World->SetPaused(true);
}

void FLuaDebugManager::RestoreWorldPauseIfNeeded()
{
    if (bWorldPauseCaptured && GEngine && GEngine->GetWorld())
    {
        GEngine->GetWorld()->SetPaused(bWorldWasPaused);
    }
    bWorldPauseCaptured = false;
    bWorldWasPaused = false;
}

void FLuaDebugManager::PublishEditorFocusRequest(ELuaDebugPauseReason Reason, const FLuaDebugLocation& Location)
{
    if (!Location.bLuaBlueprint || Location.BlueprintPath.empty() || Location.NodeId == 0)
    {
        return;
    }

    ++EditorFocusSerial;
    EditorFocusRequest.Location = Location;
    EditorFocusRequest.Reason = Reason;
    EditorFocusRequest.Serial = EditorFocusSerial;
}

void FLuaDebugManager::EnterPause(ELuaDebugPauseReason Reason, const FLuaDebugLocation& Location, ULuaBlueprintComponent* Component)
{
    bPaused = true;
    PauseReason = Reason;
    PausedLocation = Location;
    PausedBlueprintComponent = Component;
    StepMode = ELuaDebugStepMode::None;
    StepStartDepth = Location.Depth;
    PauseWorldIfNeeded();
    RecordTrace(Location, "Pause");
    PublishEditorFocusRequest(Reason, Location);
    UE_LOG("[LuaDebug] Paused at %s Node %u Line %d (%s)", Location.RuntimeName.c_str(), Location.NodeId, Location.Line, Location.NodeName.c_str());
}

bool FLuaDebugManager::OnLuaBlueprintNode(
    lua_State*              InLuaState,
    ULuaBlueprintComponent* Component,
    const FString&          RuntimeName,
    const FString&          BlueprintPath,
    uint32                  NodeId,
    const FString&          NodeName,
    const FString&          SourceName,
    int32                   Line,
    int32                   Depth,
    sol::object             VarsObject,
    sol::object             NodeValuesObject
)
{
    if (!Component)
    {
        return false;
    }

    FLuaDebugLocation Location;
    Location.RuntimeName = RuntimeName;
    Location.BlueprintPath = BlueprintPath;
    Location.ChunkName = BlueprintPath.empty() ? RuntimeName : BlueprintPath;
    Location.SourceName = SourceName;
    Location.NodeId = NodeId;
    Location.NodeName = NodeName;
    Location.Line = Line;
    Location.Depth = Depth;
    Location.bLuaBlueprint = true;

    bool bShouldPause = false;
    bool bBreakpoint = false;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        if (!bEnabled)
        {
            return false;
        }
        RecordTrace(Location, "Node");
        if (bPaused)
        {
            return false;
        }
        bShouldPause = ShouldPauseAtBlueprintNode(Location);
        if (bShouldPause)
        {
            const FString BreakpointKey = MakeNodeBreakpointKey(BlueprintPath, NodeId);
            auto BreakpointIt = NodeBreakpoints.find(BreakpointKey);
            bBreakpoint = BreakpointIt != NodeBreakpoints.end() && BreakpointIt->second;
        }
    }

    if (!bShouldPause)
    {
        return false;
    }

    FLuaDebugValueSnapshot Snapshot = CaptureValueSnapshot(InLuaState, Location, Component, VarsObject, NodeValuesObject);
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        if (bPaused)
        {
            return false;
        }
        PausedSnapshot = std::move(Snapshot);
        EnterPause(bBreakpoint ? ELuaDebugPauseReason::Breakpoint : ELuaDebugPauseReason::Step, Location, Component);
    }
    return true;
}

void FLuaDebugManager::OnLuaError(const FString& RuntimeName, const FString& ErrorText, bool bLuaBlueprint)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (!bEnabled)
    {
        return;
    }
    FLuaDebugLocation Location;
    Location.RuntimeName = RuntimeName;
    Location.ChunkName = RuntimeName;
    Location.NodeName = "<error>";
    Location.Line = 0;
    Location.bLuaBlueprint = bLuaBlueprint;

    if (bLuaBlueprint)
    {
        for (auto It = RecentTrace.rbegin(); It != RecentTrace.rend(); ++It)
        {
            if (It->Location.bLuaBlueprint && It->Location.RuntimeName == RuntimeName)
            {
                Location = It->Location;
                break;
            }
        }
    }

    RecordTrace(Location, FString("Error: ") + ErrorText);
    if (!bPaused)
    {
        PausedSnapshot = FLuaDebugValueSnapshot();
        PausedSnapshot.Location = Location;
        EnterPause(ELuaDebugPauseReason::Exception, Location, nullptr);
    }
}

bool FLuaDebugManager::IsPaused()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return bPaused;
}

const FLuaDebugLocation& FLuaDebugManager::GetPausedLocation()
{
    return PausedLocation;
}

ELuaDebugPauseReason FLuaDebugManager::GetPauseReason()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return PauseReason;
}

TArray<FLuaDebugTraceEntry> FLuaDebugManager::GetRecentTrace()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return RecentTrace;
}


FLuaDebugValueSnapshot FLuaDebugManager::GetPausedSnapshot()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return PausedSnapshot;
}

bool FLuaDebugManager::PeekEditorFocusRequest(FLuaDebugEditorFocusRequest& OutRequest)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (EditorFocusRequest.Serial == 0 || EditorFocusRequest.Location.BlueprintPath.empty() || EditorFocusRequest.Location.NodeId == 0)
    {
        return false;
    }
    OutRequest = EditorFocusRequest;
    return true;
}

void FLuaDebugManager::ClearEditorFocusRequest(uint64 Serial)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Serial == 0 || EditorFocusRequest.Serial == Serial)
    {
        EditorFocusRequest = FLuaDebugEditorFocusRequest();
    }
}

void FLuaDebugManager::AddWatchExpression(const FString& Expression)
{
    if (Expression.empty())
    {
        return;
    }
    std::lock_guard<std::mutex> Lock(Mutex);
    auto It = std::find(WatchExpressions.begin(), WatchExpressions.end(), Expression);
    if (It == WatchExpressions.end())
    {
        WatchExpressions.push_back(Expression);
    }
}

void FLuaDebugManager::RemoveWatchExpression(int32 Index)
{
    std::lock_guard<std::mutex> Lock(Mutex);
    if (Index >= 0 && static_cast<size_t>(Index) < WatchExpressions.size())
    {
        WatchExpressions.erase(WatchExpressions.begin() + Index);
    }
}

void FLuaDebugManager::ClearWatchExpressions()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    WatchExpressions.clear();
}

TArray<FString> FLuaDebugManager::GetWatchExpressions()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    return WatchExpressions;
}

void FLuaDebugManager::PauseNextLine()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    bPauseNextLine = true;
}

void FLuaDebugManager::Continue()
{
    ULuaBlueprintComponent* ComponentToResume = nullptr;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        ComponentToResume = PausedBlueprintComponent.Get();
        bPaused = false;
        PauseReason = ELuaDebugPauseReason::None;
        StepMode = ELuaDebugStepMode::None;
        PausedBlueprintComponent = nullptr;
        RecordTrace(PausedLocation, "Continue");
        PausedSnapshot = FLuaDebugValueSnapshot();
    }

    if (ComponentToResume)
    {
        ComponentToResume->ResumeLuaDebugExecution();
    }

    {
        std::lock_guard<std::mutex> Lock(Mutex);
        if (!bPaused)
        {
            RestoreWorldPauseIfNeeded();
        }
    }
}

void FLuaDebugManager::StepInto()
{
    ULuaBlueprintComponent* ComponentToResume = nullptr;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        ComponentToResume = PausedBlueprintComponent.Get();
        StepMode = ELuaDebugStepMode::Into;
        StepStartDepth = PausedLocation.Depth;
        bPaused = false;
        PauseReason = ELuaDebugPauseReason::None;
        PausedBlueprintComponent = nullptr;
        RecordTrace(PausedLocation, "StepInto");
        PausedSnapshot = FLuaDebugValueSnapshot();
    }
    if (ComponentToResume)
    {
        ComponentToResume->ResumeLuaDebugExecution();
    }
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        if (!bPaused)
        {
            RestoreWorldPauseIfNeeded();
        }
    }
}

void FLuaDebugManager::StepOver()
{
    ULuaBlueprintComponent* ComponentToResume = nullptr;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        ComponentToResume = PausedBlueprintComponent.Get();
        StepMode = ELuaDebugStepMode::Over;
        StepStartDepth = PausedLocation.Depth;
        bPaused = false;
        PauseReason = ELuaDebugPauseReason::None;
        PausedBlueprintComponent = nullptr;
        RecordTrace(PausedLocation, "StepOver");
        PausedSnapshot = FLuaDebugValueSnapshot();
    }
    if (ComponentToResume)
    {
        ComponentToResume->ResumeLuaDebugExecution();
    }
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        if (!bPaused)
        {
            RestoreWorldPauseIfNeeded();
        }
    }
}

void FLuaDebugManager::StepOut()
{
    ULuaBlueprintComponent* ComponentToResume = nullptr;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        ComponentToResume = PausedBlueprintComponent.Get();
        StepMode = ELuaDebugStepMode::Out;
        StepStartDepth = PausedLocation.Depth;
        bPaused = false;
        PauseReason = ELuaDebugPauseReason::None;
        PausedBlueprintComponent = nullptr;
        RecordTrace(PausedLocation, "StepOut");
        PausedSnapshot = FLuaDebugValueSnapshot();
    }
    if (ComponentToResume)
    {
        ComponentToResume->ResumeLuaDebugExecution();
    }
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        if (!bPaused)
        {
            RestoreWorldPauseIfNeeded();
        }
    }
}

void FLuaDebugManager::AbortPause()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    bPaused = false;
    PauseReason = ELuaDebugPauseReason::None;
    StepMode = ELuaDebugStepMode::None;
    bPauseNextLine = false;
    PausedBlueprintComponent = nullptr;
    RecordTrace(PausedLocation, "AbortPause");
    PausedLocation = FLuaDebugLocation();
    PausedSnapshot = FLuaDebugValueSnapshot();
    EditorFocusRequest = FLuaDebugEditorFocusRequest();
    RestoreWorldPauseIfNeeded();
}

void FLuaDebugManager::AbortPauseForPlaySessionEnd()
{
    std::lock_guard<std::mutex> Lock(Mutex);
    const bool bHadRuntimePause = bPaused || PauseReason != ELuaDebugPauseReason::None || PausedBlueprintComponent.IsValid();
    if (bHadRuntimePause)
    {
        RecordTrace(PausedLocation, "AbortPauseForPlaySessionEnd");
    }
    bPaused = false;
    PauseReason = ELuaDebugPauseReason::None;
    StepMode = ELuaDebugStepMode::None;
    bPauseNextLine = false;
    PausedBlueprintComponent = nullptr;
    PausedLocation = FLuaDebugLocation();
    PausedSnapshot = FLuaDebugValueSnapshot();
    EditorFocusRequest = FLuaDebugEditorFocusRequest();
    // Runtime coroutine/components are about to be destroyed. Never resume them; only
    // restore the captured world pause state and clear the transient debug view.
    RestoreWorldPauseIfNeeded();
}

int32 FLuaDebugManager::GetLuaStackDepth(lua_State* L)
{
    int32 Depth = 0;
    lua_Debug DebugInfo;
    while (lua_getstack(L, Depth, &DebugInfo) != 0)
    {
        ++Depth;
    }
    return Depth;
}

FString FLuaDebugManager::GetLuaDebugString(lua_State* L, lua_Debug* Ar, const char* FieldName)
{
    if (!L || !Ar || !FieldName)
    {
        return FString();
    }
    lua_getinfo(L, FieldName, Ar);
    if (std::strcmp(FieldName, "S") == 0)
    {
        return Ar->short_src ? FString(Ar->short_src) : FString();
    }
    if (std::strcmp(FieldName, "n") == 0)
    {
        return Ar->name ? FString(Ar->name) : FString();
    }
    return FString();
}


FString FLuaDebugManager::LuaValueTypeName(lua_State* L, int Index)
{
    if (!L)
    {
        return "unknown";
    }
    FString Value;
    FString TypeName;
    if (TryDescribeSolObjectForLuaDebug(L, Index, Value, TypeName))
    {
        return TypeName;
    }
    const int Type = lua_type(L, Index);
    if (Type == LUA_TTABLE)
    {
        lua_getfield(L, Index, "__lua_debug_nil");
        const bool bDebugNil = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (bDebugNil)
        {
            return "nil";
        }
        lua_getfield(L, Index, "__lua_debug_error");
        const bool bDebugError = lua_isstring(L, -1) != 0;
        lua_pop(L, 1);
        if (bDebugError)
        {
            return "error";
        }
    }
    const char* Name = lua_typename(L, Type);
    return Name ? FString(Name) : FString("unknown");
}

FString FLuaDebugManager::LuaValueToString(lua_State* L, int Index, int32 MaxDepth)
{
    if (!L)
    {
        return "<no lua state>";
    }

    Index = LuaDebugAbsIndex(L, Index);
    FString ReadableObjectValue;
    FString ReadableObjectType;
    if (TryDescribeSolObjectForLuaDebug(L, Index, ReadableObjectValue, ReadableObjectType))
    {
        return ReadableObjectValue;
    }

    const int Type = lua_type(L, Index);
    switch (Type)
    {
    case LUA_TNIL:
        return "nil";
    case LUA_TBOOLEAN:
        return lua_toboolean(L, Index) ? "true" : "false";
    case LUA_TNUMBER:
    {
        std::ostringstream Stream;
        const double Number = static_cast<double>(lua_tonumber(L, Index));
        if (std::fabs(Number - std::round(Number)) < 0.0000001)
        {
            Stream << static_cast<long long>(std::llround(Number));
        }
        else
        {
            Stream << Number;
        }
        return Stream.str();
    }
    case LUA_TSTRING:
    {
        size_t Length = 0;
        const char* Text = lua_tolstring(L, Index, &Length);
        if (!Text)
        {
            return "\"\"";
        }
        FString Out(Text, Length);
        constexpr size_t MaxStringLength = 160;
        if (Out.size() > MaxStringLength)
        {
            Out = Out.substr(0, MaxStringLength) + "...";
        }
        return FString("\"") + Out + "\"";
    }
    case LUA_TTABLE:
    {
        lua_getfield(L, Index, "__lua_debug_nil");
        const bool bDebugNil = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (bDebugNil)
        {
            return "nil";
        }
        lua_getfield(L, Index, "__lua_debug_error");
        if (lua_isstring(L, -1))
        {
            FString Error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown debug capture error";
            lua_pop(L, 1);
            return FString("<debug value error: ") + Error + ">";
        }
        lua_pop(L, 1);
        if (MaxDepth <= 0)
        {
            return "{...}";
        }
        FString Out = "{";
        int32 Count = 0;
        lua_pushnil(L);
        while (lua_next(L, Index) != 0)
        {
            if (Count > 0)
            {
                Out += ", ";
            }
            if (Count >= 8)
            {
                Out += "...";
                lua_pop(L, 1);
                break;
            }
            Out += LuaValueToString(L, -2, 0);
            Out += "=";
            Out += LuaValueToString(L, -1, MaxDepth - 1);
            lua_pop(L, 1);
            ++Count;
        }
        Out += "}";
        return Out;
    }
    case LUA_TFUNCTION:
        return "<function>";
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA:
    case LUA_TTHREAD:
    {
        const int Top = lua_gettop(L);
        lua_pushvalue(L, Index);
        if (luaL_callmeta(L, -1, "__tostring") != 0)
        {
            FString Text = lua_tostring(L, -1) ? lua_tostring(L, -1) : lua_typename(L, Type);
            lua_settop(L, Top);
            return Text;
        }
        lua_settop(L, Top);
        char Buffer[96];
        std::snprintf(Buffer, sizeof(Buffer), "<%s %p>", lua_typename(L, Type), lua_topointer(L, Index));
        return Buffer;
    }
    default:
        return lua_typename(L, Type) ? lua_typename(L, Type) : "unknown";
    }
}

FString FLuaDebugManager::DescribeLuaValueForDisplay(sol::this_state State, sol::object Value)
{
    lua_State* L = State;
    if (!L || !Value.valid() || Value == sol::nil)
    {
        return "nil";
    }

    const int Top = lua_gettop(L);
    Value.push();
    const FString Result = LuaValueToString(L, -1);
    lua_settop(L, Top);
    return Result;
}

FString FLuaDebugManager::CoerceLuaValueToString(sol::this_state State, sol::object Value)
{
    lua_State* L = State;
    if (!L || !Value.valid() || Value == sol::nil)
    {
        return FString();
    }

    const int Top = lua_gettop(L);
    Value.push();
    const int Index = LuaDebugAbsIndex(L, -1);

    FString ReadableObjectValue;
    FString ReadableObjectType;
    if (TryDescribeSolObjectForLuaDebug(L, Index, ReadableObjectValue, ReadableObjectType))
    {
        lua_settop(L, Top);
        return ReadableObjectValue;
    }

    FString Result;
    const int Type = lua_type(L, Index);
    switch (Type)
    {
    case LUA_TNIL:
        Result = FString();
        break;
    case LUA_TBOOLEAN:
        Result = lua_toboolean(L, Index) ? "true" : "false";
        break;
    case LUA_TNUMBER:
        Result = LuaValueToString(L, Index);
        break;
    case LUA_TSTRING:
    {
        size_t Length = 0;
        const char* Text = lua_tolstring(L, Index, &Length);
        Result = Text ? FString(Text, Length) : FString();
        break;
    }
    case LUA_TTABLE:
        Result = LuaValueToString(L, Index);
        break;
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA:
    case LUA_TTHREAD:
    case LUA_TFUNCTION:
    default:
        Result = LuaValueToString(L, Index);
        if (LooksLikeDefaultLuaPointerString(Result))
        {
            const char* TypeName = lua_typename(L, Type);
            Result = FString("<") + (TypeName ? TypeName : "value") + ">";
        }
        break;
    }

    lua_settop(L, Top);
    return Result;
}

void FLuaDebugManager::SetFieldFromStackValue(lua_State* L, int TargetTableIndex, const FString& Name, int ValueIndex)
{
    if (!L || Name.empty())
    {
        return;
    }
    TargetTableIndex = LuaDebugAbsIndex(L, TargetTableIndex);
    lua_pushvalue(L, ValueIndex);
    lua_setfield(L, TargetTableIndex, Name.c_str());
}

void FLuaDebugManager::CaptureTableObject(lua_State* L, const sol::object& Object, const FString& Scope, TArray<FLuaDebugValueEntry>& OutValues, int32 MaxFields)
{
    if (!L || !Object.valid() || Object == sol::nil || Object.get_type() != sol::type::table)
    {
        return;
    }

    const int Top = lua_gettop(L);
    Object.push();
    const int TableIndex = LuaDebugAbsIndex(L, -1);
    if (!lua_istable(L, TableIndex))
    {
        lua_settop(L, Top);
        return;
    }

    int32 Count = 0;
    lua_pushnil(L);
    while (lua_next(L, TableIndex) != 0)
    {
        if (++Count > MaxFields)
        {
            lua_pop(L, 1);
            break;
        }

        FString KeyText = LuaValueToString(L, -2, 0);
        if (KeyText.size() >= 2 && KeyText.front() == '"' && KeyText.back() == '"')
        {
            KeyText = KeyText.substr(1, KeyText.size() - 2);
        }
        if (!KeyText.empty() && KeyText.rfind("__bp_", 0) == 0)
        {
            lua_pop(L, 1);
            continue;
        }

        FLuaDebugValueEntry Entry;
        Entry.Name = KeyText;
        Entry.Value = LuaValueToString(L, -1);
        Entry.TypeName = LuaValueTypeName(L, -1);
        Entry.Scope = Scope;
        OutValues.push_back(std::move(Entry));
        lua_pop(L, 1);
    }

    lua_settop(L, Top);
}

void FLuaDebugManager::CaptureNodeValuesObject(lua_State* L, const sol::object& Object, FLuaDebugValueSnapshot& OutSnapshot)
{
    if (!L || !Object.valid() || Object == sol::nil || Object.get_type() != sol::type::table)
    {
        return;
    }

    sol::table Table = Object.as<sol::table>();
    sol::object Inputs = Table["Inputs"];
    CaptureTableObject(L, Inputs, "Input", OutSnapshot.NodeValues, 128);
    sol::object Outputs = Table["Outputs"];
    CaptureTableObject(L, Outputs, "Output", OutSnapshot.NodeValues, 128);
    sol::object Values = Table["Values"];
    CaptureTableObject(L, Values, "Node", OutSnapshot.NodeValues, 128);
}

void FLuaDebugManager::CaptureBlueprintVariablesFromObject(lua_State* L, const sol::object& Object, FLuaDebugValueSnapshot& OutSnapshot)
{
    CaptureTableObject(L, Object, "Blueprint", OutSnapshot.BlueprintVariables, 256);
}

void FLuaDebugManager::CaptureRuntimeObjectVariables(ULuaBlueprintComponent* Component, FLuaDebugValueSnapshot& OutSnapshot)
{
    if (!Component)
    {
        return;
    }

    const TArray<std::pair<FString, UObject*>> RuntimeObjects = Component->GetRuntimeObjectVariableSnapshot();
    for (const std::pair<FString, UObject*>& Pair : RuntimeObjects)
    {
        FLuaDebugValueEntry Entry;
        Entry.Name = Pair.first;
        Entry.Scope = "Blueprint";
        Entry.TypeName = Pair.second && Pair.second->GetClass() ? FString(Pair.second->GetClass()->GetName()) : FString("Object");
        Entry.Value = DescribeUObjectForLuaDebug(Pair.second);
        OutSnapshot.BlueprintVariables.push_back(std::move(Entry));
    }
}

void FLuaDebugManager::CaptureLuaLocals(lua_State* L, int32 StackLevel, FLuaDebugValueSnapshot& OutSnapshot)
{
    if (!L)
    {
        return;
    }
    lua_Debug DebugInfo;
    if (lua_getstack(L, StackLevel, &DebugInfo) == 0)
    {
        return;
    }

    for (int LocalIndex = 1;; ++LocalIndex)
    {
        const char* LocalName = lua_getlocal(L, &DebugInfo, LocalIndex);
        if (!LocalName)
        {
            break;
        }
        if (!IsInternalLuaDebugName(LocalName))
        {
            FLuaDebugValueEntry Entry;
            Entry.Name = LocalName;
            Entry.Value = LuaValueToString(L, -1);
            Entry.TypeName = LuaValueTypeName(L, -1);
            Entry.Scope = "Local";
            OutSnapshot.Locals.push_back(std::move(Entry));
        }
        lua_pop(L, 1);
    }
}

void FLuaDebugManager::CaptureLuaUpvaluesAndEnvironment(lua_State* L, int32 StackLevel, FLuaDebugValueSnapshot& OutSnapshot, int* OutEnvTableRef)
{
    if (OutEnvTableRef)
    {
        *OutEnvTableRef = LUA_NOREF;
    }
    if (!L)
    {
        return;
    }

    lua_Debug DebugInfo;
    if (lua_getstack(L, StackLevel, &DebugInfo) == 0 || lua_getinfo(L, "f", &DebugInfo) == 0)
    {
        return;
    }

    const int FunctionIndex = LuaDebugAbsIndex(L, -1);
    for (int UpvalueIndex = 1;; ++UpvalueIndex)
    {
        const char* UpvalueName = lua_getupvalue(L, FunctionIndex, UpvalueIndex);
        if (!UpvalueName)
        {
            break;
        }

        if (std::strcmp(UpvalueName, "_ENV") == 0)
        {
            if (OutEnvTableRef && lua_istable(L, -1))
            {
                lua_pushvalue(L, -1);
                *OutEnvTableRef = luaL_ref(L, LUA_REGISTRYINDEX);
            }
        }
        else if (!IsInternalLuaDebugName(UpvalueName))
        {
            FLuaDebugValueEntry Entry;
            Entry.Name = UpvalueName;
            Entry.Value = LuaValueToString(L, -1);
            Entry.TypeName = LuaValueTypeName(L, -1);
            Entry.Scope = "Upvalue";
            OutSnapshot.Upvalues.push_back(std::move(Entry));
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

void FLuaDebugManager::CaptureBlueprintVariables(lua_State* L, int EnvTableRef, FLuaDebugValueSnapshot& OutSnapshot)
{
    if (!L || EnvTableRef == LUA_NOREF)
    {
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, EnvTableRef);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "__vars");
    if (lua_istable(L, -1))
    {
        const int VarsIndex = LuaDebugAbsIndex(L, -1);
        int32 Count = 0;
        lua_pushnil(L);
        while (lua_next(L, VarsIndex) != 0)
        {
            if (++Count > 256)
            {
                lua_pop(L, 1);
                break;
            }
            FLuaDebugValueEntry Entry;
            Entry.Name = LuaValueToString(L, -2, 0);
            if (Entry.Name.size() >= 2 && Entry.Name.front() == '"' && Entry.Name.back() == '"')
            {
                Entry.Name = Entry.Name.substr(1, Entry.Name.size() - 2);
            }
            Entry.Value = LuaValueToString(L, -1);
            Entry.TypeName = LuaValueTypeName(L, -1);
            Entry.Scope = "Blueprint";
            OutSnapshot.BlueprintVariables.push_back(std::move(Entry));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 2);
}

FString FLuaDebugManager::EvaluateWatchExpression(lua_State* L, int EvalEnvRef, const FString& Expression, FString* OutTypeName, bool* bOutError)
{
    if (OutTypeName)
    {
        *OutTypeName = "unknown";
    }
    if (bOutError)
    {
        *bOutError = false;
    }
    if (!L || Expression.empty())
    {
        return "";
    }

    const int Top = lua_gettop(L);
    const FString Chunk = FString("return (") + Expression + ")";
    if (luaL_loadbuffer(L, Chunk.c_str(), Chunk.size(), "LuaDebugWatch") != LUA_OK)
    {
        FString Error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "failed to compile watch expression";
        lua_settop(L, Top);
        if (bOutError) *bOutError = true;
        return Error;
    }

    if (EvalEnvRef != LUA_NOREF)
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, EvalEnvRef);
        if (lua_setupvalue(L, -2, 1) == nullptr)
        {
            lua_pop(L, 1);
        }
    }

    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
    {
        FString Error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "failed to evaluate watch expression";
        lua_settop(L, Top);
        if (bOutError) *bOutError = true;
        return Error;
    }

    FString Value = LuaValueToString(L, -1);
    if (OutTypeName)
    {
        *OutTypeName = LuaValueTypeName(L, -1);
    }
    lua_settop(L, Top);
    return Value;
}

void FLuaDebugManager::CaptureWatchExpressions(lua_State* L, int EnvTableRef, const sol::object& VarsObject, FLuaDebugValueSnapshot& OutSnapshot)
{
    if (!L)
    {
        return;
    }

    TArray<FString> WatchExpressionsCopy;
    {
        std::lock_guard<std::mutex> Lock(Mutex);
        WatchExpressionsCopy = WatchExpressions;
    }
    if (WatchExpressionsCopy.empty())
    {
        return;
    }

    int RuntimeVarsEnvRef = LUA_NOREF;
    int EffectiveEnvRef = EnvTableRef;
    if (EffectiveEnvRef == LUA_NOREF && VarsObject.valid() && VarsObject != sol::nil && VarsObject.get_type() == sol::type::table)
    {
        lua_newtable(L);
        const int RuntimeEnvIndex = LuaDebugAbsIndex(L, -1);
        VarsObject.push();
        if (lua_istable(L, -1))
        {
            CopyLuaTableFields(L, -1, RuntimeEnvIndex);
            lua_pushvalue(L, -1);
            lua_setfield(L, RuntimeEnvIndex, "__vars");
        }
        lua_pop(L, 1);
        RuntimeVarsEnvRef = luaL_ref(L, LUA_REGISTRYINDEX);
        EffectiveEnvRef = RuntimeVarsEnvRef;
    }

    const int EvalEnvRef = BuildLuaDebugEvaluationEnvironment(L, 2, EffectiveEnvRef);
    for (const FString& Expression : WatchExpressionsCopy)
    {
        FLuaDebugValueEntry Entry;
        Entry.Name = Expression;
        Entry.Scope = "Watch";
        Entry.Value = EvaluateWatchExpression(L, EvalEnvRef, Expression, &Entry.TypeName, &Entry.bError);
        OutSnapshot.Watches.push_back(std::move(Entry));
    }
    if (EvalEnvRef != LUA_NOREF)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, EvalEnvRef);
    }
    if (RuntimeVarsEnvRef != LUA_NOREF)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, RuntimeVarsEnvRef);
    }
}

FLuaDebugValueSnapshot FLuaDebugManager::CaptureValueSnapshot(lua_State* L, const FLuaDebugLocation& Location, ULuaBlueprintComponent* Component, sol::object VarsObject, sol::object NodeValuesObject)
{
    FLuaDebugValueSnapshot Snapshot;
    Snapshot.Location = Location;
    if (!L)
    {
        return Snapshot;
    }

    const int Top = lua_gettop(L);
    int EnvTableRef = LUA_NOREF;

    CaptureNodeValuesObject(L, NodeValuesObject, Snapshot);
    CaptureBlueprintVariablesFromObject(L, VarsObject, Snapshot);
    CaptureRuntimeObjectVariables(Component, Snapshot);

    CaptureLuaLocals(L, 2, Snapshot);
    CaptureLuaUpvaluesAndEnvironment(L, 2, Snapshot, &EnvTableRef);
    if (Snapshot.BlueprintVariables.empty())
    {
        CaptureBlueprintVariables(L, EnvTableRef, Snapshot);
    }
    CaptureWatchExpressions(L, EnvTableRef, VarsObject, Snapshot);
    if (EnvTableRef != LUA_NOREF)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, EnvTableRef);
    }
    lua_settop(L, Top);
    return Snapshot;
}

void FLuaDebugManager::LuaLineHook(lua_State* L, lua_Debug* Ar)
{
    if (!L || !Ar)
    {
        return;
    }

    if (Ar->event != LUA_HOOKLINE)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> Lock(Mutex);
        if (!bEnabled || bPaused || SourceBreakpoints.empty())
        {
            return;
        }
    }

    lua_getinfo(L, "Sln", Ar);
    FLuaDebugLocation Location;
    Location.Line = Ar->currentline;
    Location.SourceName = Ar->short_src ? FString(Ar->short_src) : FString();
    Location.ChunkName = Ar->source ? FString(Ar->source) : Location.SourceName;
    Location.FunctionName = Ar->name ? FString(Ar->name) : FString();
    Location.Depth = GetLuaStackDepth(L);
    Location.bLuaBlueprint = false;

    std::lock_guard<std::mutex> Lock(Mutex);
    if (!bEnabled || bPaused)
    {
        return;
    }

    if (ShouldPauseAtSourceLine(Location))
    {
        RecordTrace(Location, "LineBreakpoint");
        EnterPause(ELuaDebugPauseReason::Breakpoint, Location, nullptr);
    }
}
