#pragma once

#include "Core/CoreTypes.h"

#include <functional>
#include <vector>

struct FLuaCoroutineHandle
{
	int32 Id = 0;

	bool IsValid() const { return Id > 0; }
};

struct FLuaCoroutineYield
{
	bool bFinished = false;
	float WaitSeconds = 0.0f;
};

class FLuaCoroutineScheduler
{
public:
	using FResumeCallback = std::function<FLuaCoroutineYield()>;

	static FLuaCoroutineYield Wait(float WaitSeconds);
	static FLuaCoroutineYield Finish();

	FLuaCoroutineHandle Create(FResumeCallback ResumeCallback, float InitialDelay = 0.0f);
	FLuaCoroutineHandle CreatePaused(FResumeCallback ResumeCallback);
	FLuaCoroutineHandle StartCoroutine(FResumeCallback ResumeCallback, float InitialDelay = 0.0f);
	FLuaCoroutineHandle Start(FResumeCallback ResumeCallback, float InitialDelay = 0.0f);
	bool Cancel(FLuaCoroutineHandle Handle);
	bool IsRunning(FLuaCoroutineHandle Handle) const;
	bool SetWaitTime(FLuaCoroutineHandle Handle, float WaitSeconds);
	bool YieldFor(FLuaCoroutineHandle Handle, float WaitSeconds);
	bool Resume(FLuaCoroutineHandle Handle);
	bool ResumeNow(FLuaCoroutineHandle Handle);
	void Tick(float DeltaTime);
	void Clear();
	int32 Num() const;

private:
	struct FTask
	{
		FLuaCoroutineHandle Handle;
		float WaitTime = 0.0f;
		bool bPaused = false;
		FResumeCallback Resume;
	};

	FTask* FindTask(FLuaCoroutineHandle Handle);
	const FTask* FindTask(FLuaCoroutineHandle Handle) const;
	bool ApplyResumeResult(FLuaCoroutineHandle Handle, const FLuaCoroutineYield& Yield);

	std::vector<FTask> Tasks;
	int32 NextId = 1;
};
