#include "CrashTest.h"
#include "Object/Object.h"
#include <Windows.h>

bool FCrashTest::bRandomObjectDeletionEnabled = false;
int32 FCrashTest::RandomObjectDeletionsPerFrame = 1;

//나중엔 crash타입을 enum으로 고르게 
void FCrashTest::CauseCrash()
{
    // deterministic Access Violation
    volatile int* ptr = nullptr;
	*ptr = 123;
}

void FCrashTest::RaiseTestException()
{
    RaiseException(
        0xE0000001,
        EXCEPTION_NONCONTINUABLE,
        0,
        nullptr
	);
}

void FCrashTest::EnableRandomObjectDeletion(bool bEnable, int32 DeletionsPerFrame)
{
    bRandomObjectDeletionEnabled = bEnable;

    if (DeletionsPerFrame < 1)
    {
        DeletionsPerFrame = 1;
    }

    RandomObjectDeletionsPerFrame = DeletionsPerFrame;
}

void FCrashTest::TickRandomObjectDeletion()
{
    if (!bRandomObjectDeletionEnabled)
    {
        return;
    }

    for (int32 i = 0; i < RandomObjectDeletionsPerFrame; ++i)
    {
        UObject* Obj = UObjectManager::Get().GetRandomObject();

        if (!Obj)
        {
            continue;
        }

        UObjectManager::Get().DestroyObject(Obj);
    }
}