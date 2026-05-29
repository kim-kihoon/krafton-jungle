#pragma once
#include "Core/CoreTypes.h"
#include "Object/UClass.h"

class FNotifyRegistry
{
public:
	static FNotifyRegistry& Get()
	{
		static FNotifyRegistry Instance;
		return Instance;
	}

	// 새 Notify 클래스를 등록하는 함수
	void RegisterClass(const FString& Name, UClass* Class)
	{
		NotifyClasses[Name] = Class;
	}

	// 등록된 Notify 클래스 목록 반환
	const TMap<FString, UClass*>& GetNotifyClasses() const
	{
		return NotifyClasses;
	}

private:
	TMap<FString, UClass*> NotifyClasses;
};

// 컴파일 시점 혹은 프로그램 시작 시점에 전역 변수 초기화를 이용해 자동 등록하는 트릭
struct FAutoRegisterNotify
{
	FAutoRegisterNotify(const std::string& Name, UClass* Cls)
	{
		FNotifyRegistry::Get().RegisterClass(Name, Cls);
	}
};

// 새로운 Notify를 만들 때 파일 하단에 붙여줄 매크로
#define REGISTER_NOTIFY(ClassName) \
    static FAutoRegisterNotify GAutoRegister_##ClassName(#ClassName, ClassName::StaticClass());
