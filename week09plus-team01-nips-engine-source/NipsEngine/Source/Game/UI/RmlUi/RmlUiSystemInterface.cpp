#include "Game/UI/RmlUi/RmlUiSystemInterface.h"

#include <Windows.h>

FRmlUiSystemInterface::FRmlUiSystemInterface()
{
	LARGE_INTEGER Frequency;
	LARGE_INTEGER Counter;
	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&Counter);

	SecondsPerCount = 1.0 / static_cast<double>(Frequency.QuadPart);
	StartCounter = Counter.QuadPart;
}

double FRmlUiSystemInterface::GetElapsedTime()
{
	LARGE_INTEGER Counter;
	QueryPerformanceCounter(&Counter);
	return static_cast<double>(Counter.QuadPart - StartCounter) * SecondsPerCount;
}

bool FRmlUiSystemInterface::LogMessage(Rml::Log::Type Type, const Rml::String& Message)
{
	(void)Type;
	OutputDebugStringA("[RmlUi] ");
	OutputDebugStringA(Message.c_str());
	OutputDebugStringA("\n");
	return true;
}
