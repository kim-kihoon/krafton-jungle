#pragma once

#include <Windows.h>

// SEH 필터 함수: __except() 안에서 GetExceptionInformation()과 함께 사용
// + SetUnhandledExceptionFilter에서 사용할 수 있는 크래시 덤프 생성 함수.
// 크래시 발생 시 실행 파일 옆에 .dmp 파일을 생성합니다.
LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* ExceptionInfo);
