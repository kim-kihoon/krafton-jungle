#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CollisionTypes.h"
#include "Core/Singleton.h"
#include "Scripting/LuaCoroutineScheduler.h"

#ifndef WITH_LUA
#define WITH_LUA 0
#endif

#if WITH_LUA
#include <memory>
#include <sol/sol.hpp>
#endif

class AActor;
class ULuaScriptComponent;

class FLuaScriptSystem : public TSingleton<FLuaScriptSystem>
{
	friend class TSingleton<FLuaScriptSystem>;

public:
	bool LoadScript(ULuaScriptComponent* Component, const FString& ScriptPath);
	bool ReloadScript(ULuaScriptComponent* Component, const FString& ScriptPath);
	void UnloadScript(ULuaScriptComponent* Component);

	void CallBeginPlay(ULuaScriptComponent* Component, AActor* Owner);
	void CallTick(ULuaScriptComponent* Component, AActor* Owner, float DeltaTime);
	void CallEndPlay(ULuaScriptComponent* Component, AActor* Owner);
	void CallOverlap(ULuaScriptComponent* Component, AActor* Owner, const FOverlapResult& Overlap);
	void CallEndOverlap(ULuaScriptComponent* Component, AActor* Owner, const FOverlapResult& Overlap);
	void CallHit(ULuaScriptComponent* Component, AActor* Owner, const FHitResult& Hit);
	void CallInteract(ULuaScriptComponent* Component, AActor* Owner, AActor* Interactor);
	void CallPickedUp(ULuaScriptComponent* Component, AActor* Owner, AActor* Picker);

	bool IsLuaEnabled() const { return bLuaEnabled; }
	const FString& GetLastError() const { return LastError; }
	bool SetStringGameStateValue(const FString& Key, const FString& Value);
	FString GetStringGameStateValue(const FString& Key) const;

private:
	FLuaScriptSystem();

#if WITH_LUA
	struct FScriptState
	{
		std::unique_ptr<sol::state> Lua;
		FString ScriptPath;
		FLuaCoroutineScheduler CoroutineScheduler;
	};

	struct FGameStateValue
	{
		enum class EType
		{
			Nil,
			Boolean,
			Number,
			String
		};

		EType Type = EType::Nil;
		bool BoolValue = false;
		double NumberValue = 0.0;
		FString StringValue;
	};

	FScriptState* FindScript(ULuaScriptComponent* Component);
	void BindCoroutineAPI(ULuaScriptComponent* Component, FScriptState& State);
	AActor* FindActorByName(ULuaScriptComponent* Component, const FString& ActorName) const;
	bool SetGameStateValue(const FString& Key, sol::object Value);
	sol::object GetGameStateValue(const FString& Key, sol::this_state LuaState) const;
	FLuaCoroutineHandle CreateCoroutine(ULuaScriptComponent* Component, sol::function Function, bool bStartPaused);
	FLuaCoroutineHandle StartCoroutine(ULuaScriptComponent* Component, sol::function Function);
	bool ResumeCoroutine(ULuaScriptComponent* Component, FLuaCoroutineHandle Handle);
	bool CallFunction(ULuaScriptComponent* Component, const char* FunctionName);
	bool CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner);
	bool CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, float DeltaTime);
	bool CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, AActor* OtherActor);
	bool CallHitFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, const FHitResult& Hit);
	void ReportCallError(ULuaScriptComponent* Component, const char* FunctionName, const char* ErrorMessage);

	TMap<ULuaScriptComponent*, FScriptState> Scripts;
	TMap<FString, FGameStateValue> GameState;
#endif

	void SetLastError(const FString& Error);

	bool bLuaEnabled = false;
	FString LastError;
};
