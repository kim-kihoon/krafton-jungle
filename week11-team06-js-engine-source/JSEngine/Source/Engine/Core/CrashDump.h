#pragma once

#include <Windows.h>
#include <DbgHelp.h>

#pragma comment(lib, "DbgHelp.lib")

enum class ECrashDumpProfile
{
    FullMemory,
    DataSegsOnly
};

struct FCrashHandler
{
    static void Initialize();

    static LONG WINAPI HandleException(EXCEPTION_POINTERS* ExceptionInfo);

    static void WriteCrashDump(
        EXCEPTION_POINTERS* ExceptionInfo,
        ECrashDumpProfile Profile = ECrashDumpProfile::FullMemory);

    static void WriteCrashLog(EXCEPTION_POINTERS* ExceptionInfo);

    static void SetNextDumpProfileDataSegsOnly();
};