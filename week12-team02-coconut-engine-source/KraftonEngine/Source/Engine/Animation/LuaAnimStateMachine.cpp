#include "LuaAnimStateMachine.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceManager.h"
#include "Object/ObjectFactory.h"
#include "Lua/LuaScriptManager.h"
#include "Core/Log.h"

IMPLEMENT_CLASS(ULuaAnimStateMachine, UAnimationStateMachine)

void ULuaAnimStateMachine::Initialize(USkeletalMeshComponent* InOwner, UAnimInstance* InAnimInstance)
{
	Super::Initialize(InOwner, InAnimInstance);
}

void ULuaAnimStateMachine::LoadScript(const FString& ScriptPath)
{
	ScriptFilePath = ScriptPath;
	bScriptLoaded = false;

	sol::state& Lua = FLuaScriptManager::GetState();

	// 스크립트마다 격리된 환경 생성 (전역 오염 방지)
	ScriptEnv = sol::environment(Lua, sol::create, Lua.globals());

	// Lua에서 'self'로 이 StateMachine 객체에 접근
	ScriptEnv["self"] = this;
	ScriptEnv["AnimInstance"] = OwnerAnimInstance;

	FString Content;
	if (!FLuaScriptManager::ReadScriptFileContent(ScriptPath, Content))
	{
		UE_LOG("[LuaAnimSM] Failed to read script: %s", ScriptPath.c_str());
		return;
	}

	sol::protected_function_result Result = Lua.safe_script(Content, ScriptEnv);
	if (!Result.valid())
	{
		sol::error Err = Result;
		UE_LOG("[LuaAnimSM] Script error in %s: %s", ScriptPath.c_str(), Err.what());
		return;
	}

	LuaOnUpdate     = ScriptEnv["onUpdate"];
	LuaOnTransition = ScriptEnv["onTransition"];

	// 초기화 함수 호출
	sol::protected_function OnInit = ScriptEnv["onInit"];
	if (OnInit.valid())
	{
		sol::protected_function_result InitResult = OnInit();
		if (!InitResult.valid())
		{
			sol::error Err = InitResult;
			UE_LOG("[LuaAnimSM] onInit error: %s", Err.what());
		}
	}

	bScriptLoaded = true;
	UE_LOG("[LuaAnimSM] Loaded: %s", ScriptPath.c_str());
}

void ULuaAnimStateMachine::ProcessState(float DeltaSeconds)
{
	if (!bScriptLoaded || !LuaOnUpdate.valid())
		return;

	sol::protected_function_result Result = LuaOnUpdate(DeltaSeconds);
	if (!Result.valid())
	{
		sol::error Err = Result;
		UE_LOG("[LuaAnimSM] onUpdate error: %s", Err.what());
	}
}

void ULuaAnimStateMachine::TransitionTo(const FString& StateName)
{
	if (StateName == CurrentStateName)
		return;

	PrevSequence   = CurrentSequence;
	BlendingPrevStateTime = StateLocalTime;
	StateLocalTime = 0.0f;
	BlendAlpha     = 0.0f;

	CurrentStateName = StateName;

	UE_LOG("[LuaAnimSM] Transition -> %s", StateName.c_str());

	if (LuaOnTransition.valid())
	{
		sol::protected_function_result Result = LuaOnTransition(StateName);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("[LuaAnimSM] onTransition error: %s", Err.what());
		}
	}
}

void ULuaAnimStateMachine::SetSequenceByName(const FString& SequenceName)
{
	UAnimSequence* Seq = FAnimSequenceManager::Get().Load(SequenceName);
	if (Seq)
	{
		CurrentSequence = Seq;
	}
	else
	{
		UE_LOG("[LuaAnimSM] SetSequenceByName: not found - %s", SequenceName.c_str());
	}
}

