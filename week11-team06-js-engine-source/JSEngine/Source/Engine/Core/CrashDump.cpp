#include "Engine/Core/CrashDump.h"
#include "Engine/Core/Paths.h"

#include <DbgHelp.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <atomic>

namespace
{
std::atomic_bool GInitialized{ false };
std::atomic_bool GWritingDump{ false };
std::atomic_int GNextDumpProfile{ 0 };
} // namespace

void FCrashHandler::Initialize()
{
    bool Expected = false;
    if (!GInitialized.compare_exchange_strong(Expected, true))
    {
        return;
    }

    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    SetUnhandledExceptionFilter(FCrashHandler::HandleException);
}

LONG WINAPI FCrashHandler::HandleException(EXCEPTION_POINTERS* ExceptionInfo)
{
    bool Expected = false;
    if (!GWritingDump.compare_exchange_strong(Expected, true))
    {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    const int ProfileValue = GNextDumpProfile.exchange(0);

    const ECrashDumpProfile Profile =
        ProfileValue == 1
            ? ECrashDumpProfile::DataSegsOnly
            : ECrashDumpProfile::FullMemory;

    WriteCrashDump(ExceptionInfo, Profile);
    WriteCrashLog(ExceptionInfo);

    GWritingDump.store(false);
    return EXCEPTION_EXECUTE_HANDLER;
}

void FCrashHandler::SetNextDumpProfileDataSegsOnly()
{
    GNextDumpProfile.store(1);
}

void FCrashHandler::WriteCrashDump(EXCEPTION_POINTERS* ExceptionInfo, ECrashDumpProfile Profile)
{
    if (!ExceptionInfo)
    {
        return;
    }

    FPaths::CreateDir(FPaths::DumpDir());

    WCHAR FileName[MAX_PATH] = {};
    time_t Now = time(nullptr);
    tm LocalTime{};
    localtime_s(&LocalTime, &Now);

    swprintf_s(
        FileName,
        L"Crash_%lu_%04d%02d%02d_%02d%02d%02d.dmp",
        GetCurrentProcessId(),
        LocalTime.tm_year + 1900,
        LocalTime.tm_mon + 1,
        LocalTime.tm_mday,
        LocalTime.tm_hour,
        LocalTime.tm_min,
        LocalTime.tm_sec);

    std::wstring DumpPath = FPaths::Combine(FPaths::DumpDir(), FileName);

    HANDLE File = CreateFileW(
        DumpPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (File == INVALID_HANDLE_VALUE)
    {
        DWORD Error = GetLastError();

        WCHAR Buffer[512] = {};
        swprintf_s(
            Buffer,
            L"[CrashDump] CreateFileW failed. Path=%s, Error=%lu\n",
            DumpPath.c_str(),
            Error);

        OutputDebugStringW(Buffer);
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION DumpInfo{};
    DumpInfo.ThreadId = GetCurrentThreadId();
    DumpInfo.ExceptionPointers = ExceptionInfo;

    // 같은 프로세스에서 받은 EXCEPTION_POINTERS이므로 FALSE가 맞다.
    DumpInfo.ClientPointers = FALSE;

    MINIDUMP_TYPE DumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal |
        MiniDumpWithDataSegs);

    BOOL bSucceeded = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        File,
        DumpType,
        &DumpInfo,
        nullptr,
        nullptr);

    CloseHandle(File);

    if (bSucceeded)
    {
        WCHAR Message[1024] = {};
        swprintf_s(
            Message,
            L"[CrashDump] Dump saved: %s\n",
            DumpPath.c_str());

        MessageBoxW(nullptr, Message, L"Crash", MB_OK | MB_ICONERROR);
    }
    else
    {
        DWORD Error = GetLastError();

        WCHAR Buffer[256] = {};
        swprintf_s(
            Buffer,
            L"[CrashDump] MiniDumpWriteDump failed. Error=%lu\n",
            Error);

        MessageBoxW(nullptr, Buffer, L"Crash", MB_OK | MB_ICONERROR);
    }
}

void FCrashHandler::WriteCrashLog(EXCEPTION_POINTERS* ExceptionInfo)
{
    if (!ExceptionInfo || !ExceptionInfo->ExceptionRecord)
    {
        return;
    }

    FPaths::CreateDir(FPaths::DumpDir());

    WCHAR FileName[MAX_PATH] = {};
    time_t Now = time(nullptr);
    tm LocalTime{};
    localtime_s(&LocalTime, &Now);

    swprintf_s(
        FileName,
        L"CrashLog_%lu_%04d%02d%02d_%02d%02d%02d.txt",
        GetCurrentProcessId(),
        LocalTime.tm_year + 1900,
        LocalTime.tm_mon + 1,
        LocalTime.tm_mday,
        LocalTime.tm_hour,
        LocalTime.tm_min,
        LocalTime.tm_sec);

    std::filesystem::path LogPath(FPaths::Combine(FPaths::DumpDir(), FileName));

    std::ofstream LogFile(LogPath, std::ios::out | std::ios::app);
    if (!LogFile.is_open())
    {
        return;
    }

    const DWORD ExceptionCode = ExceptionInfo->ExceptionRecord->ExceptionCode;
    const void* ExceptionAddress = ExceptionInfo->ExceptionRecord->ExceptionAddress;

    LogFile << "========================================\n";
    LogFile << "[Engine Crash Report]\n";
    LogFile << "ProcessId: " << GetCurrentProcessId() << "\n";
    LogFile << "ThreadId: " << GetCurrentThreadId() << "\n";

    LogFile << "Exception Code: 0x"
            << std::hex << ExceptionCode << std::dec << "\n";

    LogFile << "Exception Address: 0x"
            << std::hex
            << reinterpret_cast<uintptr_t>(ExceptionAddress)
            << std::dec << "\n";

    LogFile << "\n";
    LogFile << "Note:\n";
    LogFile << "The .dmp file contains the authoritative crash context.\n";
    LogFile << "Open it with Visual Studio or WinDbg using matching PDB files.\n";
    LogFile << "========================================\n\n";
}