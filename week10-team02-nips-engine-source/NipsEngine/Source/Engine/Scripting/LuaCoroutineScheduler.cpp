#include "Scripting/LuaCoroutineScheduler.h"

#include <algorithm>
#include <utility>

FLuaCoroutineYield FLuaCoroutineScheduler::Wait(float WaitSeconds)
{
	return FLuaCoroutineYield{ false, std::max(0.0f, WaitSeconds) };
}

FLuaCoroutineYield FLuaCoroutineScheduler::Finish()
{
	return FLuaCoroutineYield{ true, 0.0f };
}

FLuaCoroutineHandle FLuaCoroutineScheduler::Create(FResumeCallback ResumeCallback, float InitialDelay)
{
	return Start(std::move(ResumeCallback), InitialDelay);
}

FLuaCoroutineHandle FLuaCoroutineScheduler::CreatePaused(FResumeCallback ResumeCallback)
{
	FLuaCoroutineHandle Handle = Start(std::move(ResumeCallback), 0.0f);
	if (FTask* Task = FindTask(Handle))
	{
		Task->bPaused = true;
	}
	return Handle;
}

FLuaCoroutineHandle FLuaCoroutineScheduler::StartCoroutine(FResumeCallback ResumeCallback, float InitialDelay)
{
	return Start(std::move(ResumeCallback), InitialDelay);
}

FLuaCoroutineHandle FLuaCoroutineScheduler::Start(FResumeCallback ResumeCallback, float InitialDelay)
{
	if (!ResumeCallback)
	{
		return {};
	}

	FLuaCoroutineHandle Handle;
	Handle.Id = NextId++;
	if (NextId <= 0)
	{
		NextId = 1;
	}

	FTask Task;
	Task.Handle = Handle;
	Task.WaitTime = std::max(0.0f, InitialDelay);
	Task.Resume = std::move(ResumeCallback);
	Tasks.push_back(std::move(Task));
	return Handle;
}

bool FLuaCoroutineScheduler::Cancel(FLuaCoroutineHandle Handle)
{
	const auto OldSize = Tasks.size();
	Tasks.erase(
		std::remove_if(Tasks.begin(), Tasks.end(), [Handle](const FTask& Task)
		{
			return Task.Handle.Id == Handle.Id;
		}),
		Tasks.end());
	return Tasks.size() != OldSize;
}

bool FLuaCoroutineScheduler::IsRunning(FLuaCoroutineHandle Handle) const
{
	return FindTask(Handle) != nullptr;
}

bool FLuaCoroutineScheduler::SetWaitTime(FLuaCoroutineHandle Handle, float WaitSeconds)
{
	FTask* Task = FindTask(Handle);
	if (Task == nullptr)
	{
		return false;
	}

	Task->WaitTime = std::max(0.0f, WaitSeconds);
	return true;
}

bool FLuaCoroutineScheduler::YieldFor(FLuaCoroutineHandle Handle, float WaitSeconds)
{
	return SetWaitTime(Handle, WaitSeconds);
}

bool FLuaCoroutineScheduler::Resume(FLuaCoroutineHandle Handle)
{
	FTask* Task = FindTask(Handle);
	if (Task == nullptr)
	{
		return false;
	}

	Task->bPaused = false;
	const FLuaCoroutineYield Yield = Task->Resume ? Task->Resume() : FLuaCoroutineYield{ true, 0.0f };
	return ApplyResumeResult(Handle, Yield);
}

bool FLuaCoroutineScheduler::ResumeNow(FLuaCoroutineHandle Handle)
{
	FTask* Task = FindTask(Handle);
	if (Task == nullptr)
	{
		return false;
	}

	Task->bPaused = false;
	Task->WaitTime = 0.0f;
	return true;
}

void FLuaCoroutineScheduler::Tick(float DeltaTime)
{
	const float ClampedDeltaTime = std::max(0.0f, DeltaTime);

	for (int32 Index = 0; Index < static_cast<int32>(Tasks.size());)
	{
		FTask& Task = Tasks[Index];
		if (Task.bPaused)
		{
			++Index;
			continue;
		}

		Task.WaitTime -= ClampedDeltaTime;

		if (Task.WaitTime > 0.0f)
		{
			++Index;
			continue;
		}

		const FLuaCoroutineYield Yield = Task.Resume ? Task.Resume() : FLuaCoroutineYield{ true, 0.0f };
		if (Yield.bFinished)
		{
			Tasks.erase(Tasks.begin() + Index);
			continue;
		}

		Task.WaitTime = std::max(0.0f, Yield.WaitSeconds);
		++Index;
	}
}

void FLuaCoroutineScheduler::Clear()
{
	Tasks.clear();
}

int32 FLuaCoroutineScheduler::Num() const
{
	return static_cast<int32>(Tasks.size());
}

FLuaCoroutineScheduler::FTask* FLuaCoroutineScheduler::FindTask(FLuaCoroutineHandle Handle)
{
	auto It = std::find_if(Tasks.begin(), Tasks.end(), [Handle](const FTask& Task)
	{
		return Task.Handle.Id == Handle.Id;
	});
	return It == Tasks.end() ? nullptr : &(*It);
}

bool FLuaCoroutineScheduler::ApplyResumeResult(FLuaCoroutineHandle Handle, const FLuaCoroutineYield& Yield)
{
	if (Yield.bFinished)
	{
		return Cancel(Handle);
	}

	return SetWaitTime(Handle, Yield.WaitSeconds);
}

const FLuaCoroutineScheduler::FTask* FLuaCoroutineScheduler::FindTask(FLuaCoroutineHandle Handle) const
{
	auto It = std::find_if(Tasks.begin(), Tasks.end(), [Handle](const FTask& Task)
	{
		return Task.Handle.Id == Handle.Id;
	});
	return It == Tasks.end() ? nullptr : &(*It);
}
