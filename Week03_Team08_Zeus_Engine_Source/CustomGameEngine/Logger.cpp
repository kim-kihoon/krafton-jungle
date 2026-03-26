#include "Logger.h"
#include <cstdio>   // vsnprintf 사용을 위해 필수
#include <cstdarg>  // 가변 인자 처리를 위해 필수
#include <utility>  // std::move 사용을 위해 필수

void Logger::Bind(LogCallback callback)
{
	Callback = std::move(callback);
}

void Logger::Unbind()
{
	Callback = nullptr;
}

void Logger::LogFormat(const char* fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);

	// vsnprintf로 안전하게 버퍼에 포맷팅
	vsnprintf(buf, sizeof(buf), fmt, args);
	buf[sizeof(buf) - 1] = '\0'; // 널 종료 문자 보장

	va_end(args);

	if (Callback)
	{
		Callback(buf);
	}
}