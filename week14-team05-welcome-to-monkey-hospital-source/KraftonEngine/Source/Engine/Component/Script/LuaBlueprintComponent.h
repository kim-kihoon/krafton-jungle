#pragma once

#include <sol/sol.hpp>
#include <utility>
#include "Component/ActorComponent.h"
#include "Core/Delegate.h"
#include "Math/Vector.h"
#include "Object/GarbageCollection.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Source/Engine/Component/Script/LuaBlueprintComponent.generated.h"

class ULuaBlueprintAsset;
class UObject;
class AActor;
class APlayerController;
class APlayerCameraManager;
class UPrimitiveComponent;
class UCameraComponent;
class UInputComponent;
struct FHitResult;

UCLASS()
class ULuaBlueprintComponent : public UActorComponent
{
public:
    GENERATED_BODY()
    ULuaBlueprintComponent();
    ~ULuaBlueprintComponent() override;

    UFUNCTION(Callable, Exec, Category="Lua Blueprint") bool ReloadBlueprint();

    UFUNCTION(Callable, Exec, Category="Lua Blueprint") bool CallFunction(const FString& FunctionName);
    UFUNCTION(Callable, Exec, Category="Lua Blueprint") bool CallLuaBlueprintFileFunction(const FString& BlueprintPath, const FString& FunctionName);
    UFUNCTION(Callable, Exec, Category="Lua Blueprint") bool CallLuaScriptFileFunction(const FString& ScriptFile, const FString& FunctionName);

    // LuaBlueprint 디버거가 중단점/스텝 정지 후 같은 coroutine 지점에서 재개할 때 호출한다.
    bool ResumeLuaDebugExecution();

    UFUNCTION(Callable, Exec, Category="Lua Blueprint")
    void SetBlueprintPath(const FString& InPath);
    UFUNCTION(Pure, Category="Lua Blueprint")
    const FString& GetBlueprintPath() const
    {
        return BlueprintPath;
    }
    UFUNCTION(Pure, Category="Lua Blueprint")
    FString GetBlueprintPathValue() const { return BlueprintPath; }

    UFUNCTION(Pure, Category="Lua Blueprint")
    ULuaBlueprintAsset* GetBlueprintAsset() const
    {
        return GetValidBlueprintAsset();
    }

    TArray<std::pair<FString, UObject*>> GetRuntimeObjectVariableSnapshot() const;

    void BeginPlay() override;
    void EndPlay() override;
    void RouteComponentDestroyed() override;
    void BeginDestroy() override;
    void AddReferencedObjects(FReferenceCollector& Collector) override;

    // Routed by UWorld after the global gameplay phase is actually ready.
    void RoutePostBeginPlay();
    void RoutePostStartMatch();
    void RoutePlayerCameraReady(
        APlayerController*    PlayerController,
        APlayerCameraManager* CameraManager,
        UCameraComponent*     ActiveCamera
    );

    // FLuaScriptManager::Shutdown 이 lua_State 종료(Lua.reset()) 직전에 호출한다.
    // sol 핸들을 살아있는 lua_State 에서 미리 해제해, 이후 최종 GC sweep 의 ClearLuaRuntime 이
    // 닫힌 lua_State 에 luaL_unref 를 호출하며 lua51.dll 내부에서 크래시나는 것을 막는다.
    void ReleaseLuaRuntimeForShutdown();
    void PreGetEditableProperties() override;
    void PostEditProperty(const char* PropertyName) override;

protected:
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
    bool                LoadBlueprintAsset();
    ULuaBlueprintAsset* GetValidBlueprintAsset() const;
    void                ClearInvalidBlueprintAsset();
    bool                InitializeLua();
    void                ClearLuaRuntime();
    void                ClearExternalLuaRuntimes();
    sol::environment    CreateExternalLuaEnvironment(const FString& DebugName, uint32 Generation);
    bool                LoadExternalLuaBlueprintRuntime(const FString& InBlueprintPath, sol::environment& OutEnv, FString& OutDebugName);
    bool                LoadExternalLuaScriptRuntime(const FString& InScriptFile, sol::environment& OutEnv, FString& OutDebugName);
    void                BindOwnerCollisionEvents();
    void                ClearCollisionBindings();
    bool                BindInputEvents();
    void                ClearInputBindings();

    const void* GetInputBindingOwnerKey() const
    {
        return this;
    }

    FString  GetRuntimeName() const;
    FString  GetDebugBlueprintPath() const;
    void     InitializeRuntimeObjectVariables();
    void     InitRuntimeObjectVariable(const FString& Name, bool bStrong);
    void     SetRuntimeObjectVariable(const FString& Name, sol::object Value);
    UObject* GetRuntimeObjectVariable(const FString& Name) const;
    bool     ReadEventFlag(const char* EventName) const;
    void     ScheduleLuaDelay(float Seconds, sol::protected_function Callback, uint32 Generation);
    void     TickLuaDelays(float DeltaTime);
    bool     IsLuaRuntimeGenerationValid(uint32 Generation) const;
    bool     InvokeLuaNoArgEvent(const char* EventName, sol::protected_function& Function);
    void     InvokeLuaEndPlay();
    void     HandleDeferredLuaCleanup();

    void HandleBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        int32                OtherBodyIndex,
        bool                 bFromSweep,
        const FHitResult&    SweepResult
    );
    void HandleEndOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        int32                OtherBodyIndex
    );
    void HandleHit(
        UPrimitiveComponent* HitComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        FVector              NormalImpulse,
        const FHitResult&    HitResult
    );
    void HandleEndHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp);

private:
    UPROPERTY(Edit, Save, Category="Lua Blueprint", DisplayName="Blueprint", AssetType="ULuaBlueprintAsset")
    FString BlueprintPath;

    ULuaBlueprintAsset* BlueprintAsset                = nullptr;
    uint32              LoadedBlueprintVersion        = 0;
    uint32              LoadedBlueprintRuntimeVersion = 0;

    sol::environment        Env;
    sol::protected_function LuaBeginPlay;
    sol::protected_function LuaPostBeginPlay;
    sol::protected_function LuaTick;
    sol::protected_function LuaPostStartMatch;
    sol::protected_function LuaOnPlayerCameraReady;
    sol::protected_function LuaEndPlay;
    sol::protected_function LuaOnOverlap;
    sol::protected_function LuaOnEndOverlap;
    sol::protected_function LuaOnHit;
    sol::protected_function LuaOnEndHit;

    bool   bWantsBeginPlay      = false;
    bool   bWantsPostBeginPlay  = false;
    bool   bWantsTick           = false;
    bool   bWantsPostStartMatch = false;
    bool   bWantsPlayerCameraReady = false;
    bool   bWantsEndPlay        = false;
    bool   bWantsOverlap        = false;
    bool   bWantsEndOverlap     = false;
    bool   bWantsHit            = false;
    bool   bWantsEndHit         = false;
    bool   bEndPlayRouted       = false;
    bool   bHasCalledLuaEndPlay = false;
    bool   bPendingLuaEndPlay   = false;
    uint32 LuaRuntimeGeneration = 0;

    // Lua 콜백 진입 카운터. obj:Destroy() 처럼 Lua 안에서 자기 자신을 destroy 하면
    // EndPlay → ClearLuaRuntime 이 mid-execution 으로 호출되어 sol::env / function 이 nil 화 →
    // 복귀 시 lua51 SIGSEGV. 재진입 중에는 정리를 미루고 outer 진입에서 처리한다.
    int32 LuaCallDepth       = 0;
    bool  bPendingLuaCleanup = false;

    struct FLuaBlueprintDelayedCallback
    {
        float                   RemainingSeconds = 0.0f;
        uint32                  Generation       = 0;
        sol::protected_function Callback;
    };

    TArray<FLuaBlueprintDelayedCallback> PendingLuaDelays;

    struct FLuaCallScope
    {
        ULuaBlueprintComponent* Owner;

        explicit FLuaCallScope(ULuaBlueprintComponent* InOwner) : Owner(InOwner)
        {
            FGarbageCollector::Get().PushCollectionBlock();
            ++Owner->LuaCallDepth;
        }

        ~FLuaCallScope()
        {
            --Owner->LuaCallDepth;
            Owner->HandleDeferredLuaCleanup();
            FGarbageCollector::Get().PopCollectionBlock();
        }
    };

    struct FLuaBlueprintRuntimeObjectVariable
    {
        FString                 Name;
        bool                    bStrong = false;
        TWeakObjectPtr<UObject> WeakValue;
        UObject*                StrongValue = nullptr;
    };

    TArray<FLuaBlueprintRuntimeObjectVariable> RuntimeObjectVariables;

    struct FLuaBlueprintExternalRuntime
    {
        FString          Key;
        FString          DebugName;
        bool             bBlueprint = false;
        ULuaBlueprintAsset* BlueprintAsset = nullptr;
        uint32           LoadedRuntimeVersion = 0;
        sol::environment Env;
    };

    TArray<FLuaBlueprintExternalRuntime> ExternalRuntimes;

    TArray<TWeakObjectPtr<UPrimitiveComponent>> BoundOverlapComponents;
    TWeakObjectPtr<UInputComponent>             BoundInputComponent;
    bool                                        bInputBindingPending           = false;
    bool                                        bInputBindingPendingLogEmitted = false;

    TArray<TWeakObjectPtr<UPrimitiveComponent>> BoundHitComponents;
    TArray<FDelegateHandle>                     BeginOverlapHandles;
    TArray<FDelegateHandle>                     EndOverlapHandles;
    TArray<FDelegateHandle>                     HitHandles;
    TArray<FDelegateHandle>                     EndHitHandles;
};