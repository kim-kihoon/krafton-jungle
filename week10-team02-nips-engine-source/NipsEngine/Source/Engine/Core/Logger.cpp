#include "Core/Logger.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>

namespace
{
	FLogger::FLogRouter GLogRouter = nullptr;
}

void FLogger::SetMessage(FLogRouter InRouter)
{
	GLogRouter = InRouter;
}

void FLogger::ClearMessage(FLogRouter InRouter)
{
	if (InRouter == nullptr || GLogRouter == InRouter)
	{
		GLogRouter = nullptr;
	}
}

void FLogger::Log(const char* Format, ...)
{
	char Buffer[2048];

	va_list Args;
	va_start(Args, Format);
	vsnprintf(Buffer, sizeof(Buffer), Format, Args);
	va_end(Args);

	if (GLogRouter)
	{
		GLogRouter(Buffer);
		return;
	}

	OutputDebugStringA(Buffer);
	OutputDebugStringA("\n");
}
