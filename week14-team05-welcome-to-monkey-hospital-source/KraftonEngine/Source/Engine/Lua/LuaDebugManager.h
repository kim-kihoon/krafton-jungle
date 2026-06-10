#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include <sol/sol.hpp>
#include <mutex>

struct lua_Debug;
struct lua_State;
class ULuaBlueprintComponent;
class UWorld;

struct FLuaDebugLocation
{
    FString ChunkName;
    FString SourceName;
    FString FunctionName;
    FString BlueprintPath;
    FString RuntimeName;
    FString NodeName;
    uint32  NodeId = 0;
    int32   Line = 0;
    int32   Depth = 0;
    bool    bLuaBlueprint = false;
};

struct FLuaDebugTraceEntry
{
    FLuaDebugLocation Location;
    double            TimestampSeconds = 0.0;
    FString           Event;
};

struct FLuaDebugValueEntry
{
    FString Name;
    FString Value;
    FString TypeName;
    FString Scope;
    bool    bError = false;
};

struct FLuaDebugValueSnapshot
{
    FLuaDebugLocation Location;
    TArray<FLuaDebugValueEntry> NodeValues;
    TArray<FLuaDebugValueEntry> Locals;
    TArray<FLuaDebugValueEntry> Upvalues;
    TArray<FLuaDebugValueEntry> BlueprintVariables;
    TArray<FLuaDebugValueEntry> Watches;
};

enum class ELuaDebugStepMode : uint8
{
    None,
    Into,
    Over,
    Out
};

enum class ELuaDebugPauseReason : uint8
{
    None,
    Manual,
    Breakpoint,
    Step,
    Exception
};

struct FLuaDebugEditorFocusRequest
{
    FLuaDebugLocation Location;
    ELuaDebugPauseReason Reason = ELuaDebugPauseReason::None;
    uint64 Serial = 0;
};

class FLuaDebugManager
{
public:
    static void Initialize(lua_State* InLuaState);
    static void Shutdown();
    static void RegisterLuaBindings(sol::state& Lua);

    static void SetEnabled(bool bInEnabled);
    static bool IsEnabled();

    static void InstallLineHook(lua_State* InLuaState);
    static void RemoveLineHook(lua_State* InLuaState);
    static void LuaLineHook(lua_State* L, lua_Debug* Ar);

    static bool ToggleNodeBreakpoint(const FString& BlueprintPath, uint32 NodeId);
    static void SetNodeBreakpoint(const FString& BlueprintPath, uint32 NodeId, bool bEnabled);
    static bool HasNodeBreakpoint(const FString& BlueprintPath, uint32 NodeId);
    static void ClearNodeBreakpoints(const FString& BlueprintPath = FString());

    static void SetSourceBreakpoint(const FString& ChunkName, int32 Line, bool bEnabled);
    static bool HasSourceBreakpoint(const FString& ChunkName, int32 Line);
    static void ClearSourceBreakpoints(const FString& ChunkName = FString());

    static bool OnLuaBlueprintNode(
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
    );

    static void OnLuaError(const FString& RuntimeName, const FString& ErrorText, bool bLuaBlueprint);

    static bool IsPaused();
    static const FLuaDebugLocation& GetPausedLocation();
    static ELuaDebugPauseReason GetPauseReason();
    static TArray<FLuaDebugTraceEntry> GetRecentTrace();

    static FLuaDebugValueSnapshot GetPausedSnapshot();
    static bool PeekEditorFocusRequest(FLuaDebugEditorFocusRequest& OutRequest);
    static void ClearEditorFocusRequest(uint64 Serial);
    static void AddWatchExpression(const FString& Expression);
    static void RemoveWatchExpression(int32 Index);
    static void ClearWatchExpressions();
    static TArray<FString> GetWatchExpressions();

    static void PauseNextLine();
    static void Continue();
    static void StepInto();
    static void StepOver();
    static void StepOut();
    static void AbortPause();
    static void AbortPauseForPlaySessionEnd();

    static FString DescribeLuaValueForDisplay(sol::this_state State, sol::object Value);
    static FString CoerceLuaValueToString(sol::this_state State, sol::object Value);

private:
    static FString MakeNodeBreakpointKey(const FString& BlueprintPath, uint32 NodeId);
    static FString MakeSourceBreakpointKey(const FString& ChunkName, int32 Line);
    static void RecordTrace(const FLuaDebugLocation& Location, const FString& Event);
    static bool ShouldPauseAtBlueprintNode(const FLuaDebugLocation& Location);
    static bool ShouldPauseAtSourceLine(const FLuaDebugLocation& Location);
    static void EnterPause(ELuaDebugPauseReason Reason, const FLuaDebugLocation& Location, ULuaBlueprintComponent* Component);
    static void PublishEditorFocusRequest(ELuaDebugPauseReason Reason, const FLuaDebugLocation& Location);
    static void RestoreWorldPauseIfNeeded();
    static void PauseWorldIfNeeded();
    static int32 GetLuaStackDepth(lua_State* L);
    static FString GetLuaDebugString(lua_State* L, lua_Debug* Ar, const char* FieldName);

    static FLuaDebugValueSnapshot CaptureValueSnapshot(lua_State* L, const FLuaDebugLocation& Location, ULuaBlueprintComponent* Component, sol::object VarsObject, sol::object NodeValuesObject);
    static FString LuaValueToString(lua_State* L, int Index, int32 MaxDepth = 2);
    static FString LuaValueTypeName(lua_State* L, int Index);
    static void CaptureTableObject(lua_State* L, const sol::object& Object, const FString& Scope, TArray<FLuaDebugValueEntry>& OutValues, int32 MaxFields = 256);
    static void CaptureNodeValuesObject(lua_State* L, const sol::object& Object, FLuaDebugValueSnapshot& OutSnapshot);
    static void CaptureBlueprintVariablesFromObject(lua_State* L, const sol::object& Object, FLuaDebugValueSnapshot& OutSnapshot);
    static void CaptureRuntimeObjectVariables(ULuaBlueprintComponent* Component, FLuaDebugValueSnapshot& OutSnapshot);
    static void CaptureLuaLocals(lua_State* L, int32 StackLevel, FLuaDebugValueSnapshot& OutSnapshot);
    static void CaptureLuaUpvaluesAndEnvironment(lua_State* L, int32 StackLevel, FLuaDebugValueSnapshot& OutSnapshot, int* OutEnvTableRef = nullptr);
    static void CaptureBlueprintVariables(lua_State* L, int EnvTableRef, FLuaDebugValueSnapshot& OutSnapshot);
    static void CaptureWatchExpressions(lua_State* L, int EnvTableRef, const sol::object& VarsObject, FLuaDebugValueSnapshot& OutSnapshot);
    static FString EvaluateWatchExpression(lua_State* L, int EnvTableRef, const FString& Expression, FString* OutTypeName, bool* bOutError);
    static void SetFieldFromStackValue(lua_State* L, int TargetTableIndex, const FString& Name, int ValueIndex);

private:
    static std::mutex Mutex;
    static lua_State* LuaState;
    static bool       bEnabled;
    static bool       bHookInstalled;

    static bool                 bPaused;
    static ELuaDebugPauseReason PauseReason;
    static FLuaDebugLocation    PausedLocation;
    static TWeakObjectPtr<ULuaBlueprintComponent> PausedBlueprintComponent;

    static ELuaDebugStepMode StepMode;
    static int32             StepStartDepth;
    static bool              bPauseNextLine;

    static TMap<FString, bool> NodeBreakpoints;
    static TMap<FString, bool> SourceBreakpoints;
    static TArray<FLuaDebugTraceEntry> RecentTrace;
    static FLuaDebugValueSnapshot PausedSnapshot;
    static TArray<FString> WatchExpressions;
    static FLuaDebugEditorFocusRequest EditorFocusRequest;
    static uint64 EditorFocusSerial;

    static bool bWorldPauseCaptured;
    static bool bWorldWasPaused;
};
