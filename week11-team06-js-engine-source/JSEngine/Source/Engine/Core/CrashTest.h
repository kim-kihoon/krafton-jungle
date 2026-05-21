#pragma once
#include "Core/CoreMinimal.h"

struct FCrashTest
{
    static void CauseCrash();
    static void RaiseTestException();

    // Fault injection: UObject를 랜덤 삭제해서 나중에 자연 크래시를 유도. 스트레스 테스트
    static void EnableRandomObjectDeletion(bool bEnable, int32 DeletionsPerFrame = 1);
    static void TickRandomObjectDeletion();

private:
    static bool bRandomObjectDeletionEnabled;
    static int32 RandomObjectDeletionsPerFrame;
};
