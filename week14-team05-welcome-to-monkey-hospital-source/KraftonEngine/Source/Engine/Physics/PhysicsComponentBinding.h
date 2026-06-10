#pragma once

#include "Core/Types/CoreTypes.h"
#include "Physics/PhysicsBodyHandle.h"
#include "Physics/PhysicsTypes.h"

// Game Thread 전용 binding — UObject(UUID) ↔ physics handle 연결을 Game Thread 에서만 관리한다.
// Physics Thread 는 이 구조를 보지 못한다. Generation 은 destroy/recreate 경계에서 증가시켜
// 늦게 도착한 stale command/event/query 를 폐기하는 단일 기준점이 된다.
struct FPhysicsComponentBinding
{
    uint32 ActorId     = 0;
    uint32 ComponentId = 0; // = UObject UUID
    uint32 Generation  = 1;

    FPhysicsBodyHandle  Body;
    FPhysicsShapeHandle Shape;
    EPhysicsSyncMode    SyncMode = EPhysicsSyncMode::EngineToPhysics;

    bool bPendingCreate        = false;
    bool bAliveOnPhysicsThread = false;
    bool bPendingDestroy       = false;
};
