#pragma once
#include "Animation/AnimationStateMachine.h"
#include <sol/sol.hpp>

class UAnimSequence;

class ULuaAnimStateMachine : public UAnimationStateMachine
{
public:
	DECLARE_CLASS(ULuaAnimStateMachine, UAnimationStateMachine)

	void Initialize(USkeletalMeshComponent* InOwner, UAnimInstance* InAnimInstance) override;

	// Lua 스크립트 파일을 로드하고 onInit() 호출
	void LoadScript(const FString& ScriptPath);

	template<typename T>
	void BindProperty(const std::string& Name, T* Ptr)
	{
		ScriptEnv[Name] = [Ptr]() -> T { return *Ptr; };
	}

	// 매 프레임 호출 — Lua의 onUpdate(dt) 로 위임
	void ProcessState(float DeltaSeconds) override;

	// Lua에서 호출하는 전환 API
	void TransitionTo(const FString& StateName);
	void SetBlendDuration(float Duration) { BlendDuration = Duration; }
	float GetBlendDuration() const { return BlendDuration; }
	FString GetCurrentStateName() const { return CurrentStateName; }

	// Lua에서 AnimSequence를 이름으로 설정
	void SetSequenceByName(const FString& SequenceName);

private:
	sol::environment ScriptEnv;
	sol::protected_function LuaOnUpdate;
	sol::protected_function LuaOnTransition;

	FString CurrentStateName;
	FString ScriptFilePath;

	bool bScriptLoaded = false;
};
