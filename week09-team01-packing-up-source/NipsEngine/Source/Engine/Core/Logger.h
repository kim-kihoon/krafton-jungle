#pragma once

#include "Core/CoreMinimal.h"

// 에디터와 게임 런타임이 함께 사용하는 로그 라우터입니다.
class FLogger
{
public:
	using FLogRouter = void(*)(const char* Message);

	static void SetMessage(FLogRouter InRouter);
	static void ClearMessage(FLogRouter InRouter = nullptr);
	static void Log(const char* Format, ...);
};

#define UE_LOG(Format, ...) \
	FLogger::Log(Format, ##__VA_ARGS__)
