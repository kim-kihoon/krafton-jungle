#include "Scripting/LuaScriptSystem.h"

#include "Component/LuaScriptComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/CameraComponent.h"
#include "Component/DecalComponent.h"
#include "Core/Paths.h"
#include "Scripting/LuaBindings.h"
#include "Core/Logger.h"
#include "GameFramework/PrimitiveActors.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#if WITH_LUA
namespace
{
	bool LoadLuaSourceFromFile(const FString& ScriptPath, FString& OutSource, FString& OutError)
	{
		const std::filesystem::path WidePath(FPaths::ToAbsolute(FPaths::ToWide(ScriptPath)));
		std::ifstream File(WidePath, std::ios::binary);
		if (!File.is_open())
		{
			OutError = "failed to open script file.";
			return false;
		}

		OutSource.assign(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>());
		if (File.bad())
		{
			OutError = "failed to read script file.";
			return false;
		}

		return true;
	}
}
#endif

FLuaScriptSystem::FLuaScriptSystem()
{
	bLuaEnabled = WITH_LUA != 0;
}

bool FLuaScriptSystem::LoadScript(ULuaScriptComponent* Component, const FString& ScriptPath)
{
	if (!Component)
	{
		SetLastError("LuaScriptSystem: invalid component.");
		return false;
	}

#if WITH_LUA
	FScriptState State;
	State.Lua = std::make_unique<sol::state>();
	State.ScriptPath = ScriptPath;

	State.Lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string, sol::lib::coroutine);
	RegisterLuaBindings(*State.Lua);
	BindCoroutineAPI(Component, State);

	FString ScriptSource;
	FString ReadError;
	if (!LoadLuaSourceFromFile(ScriptPath, ScriptSource, ReadError))
	{
		SetLastError(ReadError);
		UE_LOG("LuaScriptSystem: failed to load '%s': %s", ScriptPath.c_str(), LastError.c_str());
		return false;
	}

	sol::protected_function_result Result = State.Lua->safe_script(ScriptSource, sol::script_pass_on_error);
	if (!Result.valid())
	{
		sol::error Error = Result;
		SetLastError(Error.what());
		UE_LOG("LuaScriptSystem: failed to load '%s': %s", ScriptPath.c_str(), LastError.c_str());
		return false;
	}

	Scripts[Component] = std::move(State);
	SetLastError("");
	return true;
#else
	(void)ScriptPath;
	SetLastError("Lua runtime is disabled. Add Lua/sol2 and build with WITH_LUA=1.");
	return false;
#endif
}

bool FLuaScriptSystem::ReloadScript(ULuaScriptComponent* Component, const FString& ScriptPath)
{
	return LoadScript(Component, ScriptPath);
}

void FLuaScriptSystem::UnloadScript(ULuaScriptComponent* Component)
{
#if WITH_LUA
	Scripts.erase(Component);
#else
	(void)Component;
#endif
}

void FLuaScriptSystem::CallBeginPlay(ULuaScriptComponent* Component, AActor* Owner)
{
#if WITH_LUA
	CallFunction(Component, "BeginPlay", Owner);
#else
	(void)Component;
	(void)Owner;
#endif
}

void FLuaScriptSystem::CallTick(ULuaScriptComponent* Component, AActor* Owner, float DeltaTime)
{
#if WITH_LUA
	CallFunction(Component, "Tick", Owner, DeltaTime);
	if (FScriptState* State = FindScript(Component))
	{
		State->CoroutineScheduler.Tick(DeltaTime);
	}
#else
	(void)Component;
	(void)Owner;
	(void)DeltaTime;
#endif
}

void FLuaScriptSystem::CallEndPlay(ULuaScriptComponent* Component, AActor* Owner)
{
#if WITH_LUA
	CallFunction(Component, "EndPlay", Owner);
#else
	(void)Component;
	(void)Owner;
#endif
}

void FLuaScriptSystem::CallOverlap(ULuaScriptComponent* Component, AActor* Owner, const FOverlapResult& Overlap)
{
#if WITH_LUA
	CallFunction(Component, "OnOverlap", Owner, Overlap.OtherActor);
#else
	(void)Component;
	(void)Owner;
	(void)Overlap;
#endif
}

void FLuaScriptSystem::CallEndOverlap(ULuaScriptComponent* Component, AActor* Owner, const FOverlapResult& Overlap)
{
#if WITH_LUA
	CallFunction(Component, "OnEndOverlap", Owner, Overlap.OtherActor);
#else
	(void)Component;
	(void)Owner;
	(void)Overlap;
#endif
}

void FLuaScriptSystem::CallHit(ULuaScriptComponent* Component, AActor* Owner, const FHitResult& Hit)
{
#if WITH_LUA
	CallHitFunction(Component, "OnHit", Owner, Hit);
#else
	(void)Component;
	(void)Owner;
	(void)Hit;
#endif
}

void FLuaScriptSystem::CallInteract(ULuaScriptComponent* Component, AActor* Owner, AActor* Interactor)
{
#if WITH_LUA
	CallFunction(Component, "OnInteract", Owner, Interactor);
#else
	(void)Component;
	(void)Owner;
	(void)Interactor;
#endif
}

void FLuaScriptSystem::CallPickedUp(ULuaScriptComponent* Component, AActor* Owner, AActor* Picker)
{
#if WITH_LUA
	CallFunction(Component, "OnPickedUp", Owner, Picker);
#else
	(void)Component;
	(void)Owner;
	(void)Picker;
#endif
}

#if WITH_LUA
void FLuaScriptSystem::BindCoroutineAPI(ULuaScriptComponent* Component, FScriptState& State)
{
	if (!State.Lua)
	{
		return;
	}

	State.Lua->set_function("print", [](sol::variadic_args Args)
	{
		std::string Message;
		for (sol::object Arg : Args)
		{
			if (!Message.empty())
			{
				Message += "\t";
			}

			switch (Arg.get_type())
			{
			case sol::type::nil:
				Message += "nil";
				break;
			case sol::type::boolean:
				Message += Arg.as<bool>() ? "true" : "false";
				break;
			case sol::type::number:
				Message += std::to_string(Arg.as<double>());
				break;
			case sol::type::string:
				Message += Arg.as<std::string>();
				break;
			default:
				Message += "<";
				Message += sol::type_name(Arg.lua_state(), Arg.get_type());
				Message += ">";
				break;
			}
		}

		UE_LOG("[Lua] %s", Message.c_str());
	});

	State.Lua->set_function("wait", sol::yielding([](float Seconds)
	{
		return std::max(0.0f, Seconds);
	}));

	State.Lua->set_function("yield", sol::yielding([](sol::optional<float> Seconds)
	{
		return std::max(0.0f, Seconds.value_or(0.0f));
	}));

	State.Lua->set_function("StartCoroutine", [this, Component](sol::function Function)
	{
		return StartCoroutine(Component, Function).Id;
	});

	State.Lua->set_function("CreateCoroutine", [this, Component](sol::function Function)
	{
		return CreateCoroutine(Component, Function, true).Id;
	});

	State.Lua->set_function("ResumeCoroutine", [this, Component](int32 CoroutineId)
	{
		return ResumeCoroutine(Component, FLuaCoroutineHandle{ CoroutineId });
	});

	State.Lua->set_function("CancelCoroutine", [this, Component](int32 CoroutineId)
	{
		FScriptState* ScriptState = FindScript(Component);
		if (ScriptState == nullptr)
		{
			return false;
		}

		return ScriptState->CoroutineScheduler.Cancel(FLuaCoroutineHandle{ CoroutineId });
	});

	State.Lua->set_function("FindActorByName", [this, Component](const FString& ActorName) -> AActor*
	{
		return FindActorByName(Component, ActorName);
	});

	State.Lua->set_function("GetPlayerActor", [Component]() -> AActor*
	{
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		UWorld* World = Owner ? Owner->GetFocusedWorld() : nullptr;
		return World ? World->FindPawn() : nullptr;
	});

	State.Lua->set_function("SetGameState", [this](const FString& Key, sol::object Value)
	{
		return SetGameStateValue(Key, Value);
	});

	State.Lua->set_function("GetGameState", [this](sol::this_state LuaState, const FString& Key)
	{
		return GetGameStateValue(Key, LuaState);
	});

	State.Lua->set_function("RaycastCenter", [Component](float MaxDistance) -> FHitResult
	{
		FHitResult Hit;
		const AActor* Owner = Component->GetOwner();
		UWorld* World = Owner ? Owner->GetFocusedWorld() : nullptr;
		if (!World) return Hit;
		UCameraComponent* Cam = World->GetActiveCameraComponent();
		if (!Cam) return Hit;

		float W = Cam->GetWidth();
		float H = Cam->GetHeight();
		FRay Ray = Cam->DeprojectScreenToWorld(W * 0.5f, H * 0.5f, W, H);

		TArray<FHitResult> Hits;
		if (!World->LineTraceMulti(Ray, MaxDistance, Hits, Owner))
			return Hit;

		// 거리 순으로 순회하며, 히트 지점의 픽셀이 아직 남아있는 가장 가까운 데칼을 반환합니다.
		for (const FHitResult& H : Hits)
		{
			UDecalComponent* Decal = Cast<UDecalComponent>(H.HitComponent);
			if (!Decal) continue;
			FVector2 HitUV;
			if (!Decal->WorldPosToDecalUV(H.Location, HitUV)) continue;
			if (!Decal->IsPixelCleanAt(HitUV))
				return H;
		}

		// 데칼이 없으면 가장 가까운 히트를 반환합니다.
		return Hits[0];
	});
}

AActor* FLuaScriptSystem::FindActorByName(ULuaScriptComponent* Component, const FString& ActorName) const
{
	if (Component == nullptr || ActorName.empty())
	{
		return nullptr;
	}

	const AActor* Owner = Component->GetOwner();
	UWorld* World = Owner ? Owner->GetFocusedWorld() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}

	const TArray<AActor*> Actors = World->GetActors();
	for (AActor* Actor : Actors)
	{
		if (Actor == nullptr)
		{
			continue;
		}

		if (Actor->GetFName().ToString() == ActorName)
		{
			return Actor;
		}
	}

	return nullptr;
}

bool FLuaScriptSystem::SetGameStateValue(const FString& Key, sol::object Value)
{
	if (Key.empty())
	{
		return false;
	}

	FGameStateValue StoredValue;
	switch (Value.get_type())
	{
	case sol::type::nil:
		GameState.erase(Key);
		return true;

	case sol::type::boolean:
		StoredValue.Type = FGameStateValue::EType::Boolean;
		StoredValue.BoolValue = Value.as<bool>();
		break;

	case sol::type::number:
		StoredValue.Type = FGameStateValue::EType::Number;
		StoredValue.NumberValue = Value.as<double>();
		break;

	case sol::type::string:
		StoredValue.Type = FGameStateValue::EType::String;
		StoredValue.StringValue = Value.as<FString>();
		break;

	default:
		UE_LOG("LuaScriptSystem: SetGameState only supports nil, bool, number, and string values.");
		return false;
	}

	GameState[Key] = StoredValue;
	return true;
}

sol::object FLuaScriptSystem::GetGameStateValue(const FString& Key, sol::this_state LuaState) const
{
	lua_State* L = LuaState;
	auto It = GameState.find(Key);
	if (It == GameState.end())
	{
		return sol::make_object(L, sol::nil);
	}

	const FGameStateValue& Value = It->second;
	switch (Value.Type)
	{
	case FGameStateValue::EType::Boolean:
		return sol::make_object(L, Value.BoolValue);

	case FGameStateValue::EType::Number:
		return sol::make_object(L, Value.NumberValue);

	case FGameStateValue::EType::String:
		return sol::make_object(L, Value.StringValue);

	case FGameStateValue::EType::Nil:
	default:
		return sol::make_object(L, sol::nil);
	}
}

FLuaCoroutineHandle FLuaScriptSystem::CreateCoroutine(ULuaScriptComponent* Component, sol::function Function, bool bStartPaused)
{
	FScriptState* State = FindScript(Component);
	if (State == nullptr || !State->Lua || !Function.valid())
	{
		return {};
	}

	sol::table CoroutineTable = (*State->Lua)["coroutine"];
	sol::protected_function NativeCreate = CoroutineTable["create"];
	sol::protected_function NativeResume = CoroutineTable["resume"];
	if (!NativeCreate.valid() || !NativeResume.valid())
	{
		UE_LOG("Lua coroutine error: native coroutine API is not available.");
		return {};
	}

	sol::protected_function_result CreateResult = NativeCreate(Function);
	if (!CreateResult.valid())
	{
		sol::error Error = CreateResult;
		UE_LOG("Lua coroutine create error: %s", Error.what());
		return {};
	}

	sol::object Coroutine = CreateResult.get<sol::object>();

	auto ResumeCallback =
		[NativeResume = std::move(NativeResume), Coroutine = std::move(Coroutine)]() mutable
		{
			sol::protected_function_result Result = NativeResume(Coroutine);
			if (!Result.valid())
			{
				sol::error Error = Result;
				UE_LOG("Lua coroutine error: %s", Error.what());
				return FLuaCoroutineScheduler::Finish();
			}

			const bool bResumeSucceeded = Result.return_count() > 0 && Result.get<bool>(0);
			if (!bResumeSucceeded)
			{
				FString ErrorMessage = "unknown coroutine error";
				if (Result.return_count() > 1)
				{
					ErrorMessage = Result.get<FString>(1);
				}

				UE_LOG("Lua coroutine error: %s", ErrorMessage.c_str());
				return FLuaCoroutineScheduler::Finish();
			}

			if (Result.return_count() > 1)
			{
				const float WaitSeconds = Result.get<float>(1);
				return FLuaCoroutineScheduler::Wait(WaitSeconds);
			}

			return FLuaCoroutineScheduler::Finish();
		};

	return bStartPaused
		? State->CoroutineScheduler.CreatePaused(std::move(ResumeCallback))
		: State->CoroutineScheduler.StartCoroutine(std::move(ResumeCallback));
}

FLuaCoroutineHandle FLuaScriptSystem::StartCoroutine(ULuaScriptComponent* Component, sol::function Function)
{
	FLuaCoroutineHandle Handle = CreateCoroutine(Component, Function, true);
	ResumeCoroutine(Component, Handle);
	return Handle;
}

bool FLuaScriptSystem::ResumeCoroutine(ULuaScriptComponent* Component, FLuaCoroutineHandle Handle)
{
	FScriptState* State = FindScript(Component);
	if (State == nullptr || !Handle.IsValid())
	{
		return false;
	}

	return State->CoroutineScheduler.Resume(Handle);
}

FLuaScriptSystem::FScriptState* FLuaScriptSystem::FindScript(ULuaScriptComponent* Component)
{
	auto It = Scripts.find(Component);
	return It != Scripts.end() ? &It->second : nullptr;
}

bool FLuaScriptSystem::CallFunction(ULuaScriptComponent* Component, const char* FunctionName)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function();
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

bool FLuaScriptSystem::CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function(Owner);
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

bool FLuaScriptSystem::CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, float DeltaTime)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function(Owner, DeltaTime);
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

bool FLuaScriptSystem::CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, AActor* OtherActor)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function(Owner, OtherActor);
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

bool FLuaScriptSystem::CallHitFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, const FHitResult& Hit)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function(Owner, Hit);
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

void FLuaScriptSystem::ReportCallError(ULuaScriptComponent* Component, const char* FunctionName, const char* ErrorMessage)
{
	const FString ErrorText = ErrorMessage ? ErrorMessage : "unknown Lua error";
	SetLastError(ErrorText);

	const FString ScriptPath = Component ? Component->GetScriptPath() : "";
	if (!ScriptPath.empty())
	{
		UE_LOG("LuaScriptSystem: failed to call %s in '%s': %s", FunctionName, ScriptPath.c_str(), ErrorText.c_str());
	}
	else
	{
		UE_LOG("LuaScriptSystem: failed to call %s: %s", FunctionName, ErrorText.c_str());
	}
}
#endif

bool FLuaScriptSystem::SetStringGameStateValue(const FString& Key, const FString& Value)
{
#if WITH_LUA
	if (Key.empty())
	{
		return false;
	}

	FGameStateValue StoredValue;
	StoredValue.Type = FGameStateValue::EType::String;
	StoredValue.StringValue = Value;
	GameState[Key] = StoredValue;
	return true;
#else
	(void)Key;
	(void)Value;
	return false;
#endif
}

FString FLuaScriptSystem::GetStringGameStateValue(const FString& Key) const
{
#if WITH_LUA
	auto It = GameState.find(Key);
	if (It == GameState.end() || It->second.Type != FGameStateValue::EType::String)
	{
		return "";
	}

	return It->second.StringValue;
#else
	(void)Key;
	return "";
#endif
}

double FLuaScriptSystem::GetNumberGameStateValue(const FString& Key, double DefaultValue) const
{
#if WITH_LUA
	auto It = GameState.find(Key);
	if (It == GameState.end() || It->second.Type != FGameStateValue::EType::Number)
	{
		return DefaultValue;
	}

	return It->second.NumberValue;
#else
	(void)Key;
	return DefaultValue;
#endif
}

void FLuaScriptSystem::SetLastError(const FString& Error)
{
	LastError = Error;
}
