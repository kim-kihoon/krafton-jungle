#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"

// PhysX callback 은 UObject 를 모른다. shape userData 에서 ID·generation 만 뽑아 이 구조로 적재하고,
// Game Thread 의 DispatchPhysicsEvents 단계에서만 UUID+generation 으로 UObject 를 resolve 한다.
struct FPhysicsContactEvent
{
    uint64 StepIndex  = 0;
    uint64 FrameIndex = 0;

    uint32 SelfActorId     = 0;
    uint32 SelfComponentId = 0;
    uint32 SelfGeneration  = 0;

    uint32 OtherActorId     = 0;
    uint32 OtherComponentId = 0;
    uint32 OtherGeneration  = 0;

    FVector WorldLocation = FVector::ZeroVector;
    FVector WorldNormal   = FVector::ZeroVector;
    FVector NormalImpulse = FVector::ZeroVector;

    float PenetrationDepth = 0.0f;

    bool bBegin = true;
};

struct FPhysicsOverlapEvent
{
    uint64 StepIndex  = 0;
    uint64 FrameIndex = 0;

    uint32 SelfActorId     = 0;
    uint32 SelfComponentId = 0;
    uint32 SelfGeneration  = 0;

    uint32 OtherActorId     = 0;
    uint32 OtherComponentId = 0;
    uint32 OtherGeneration  = 0;

    bool bBegin = true;
};

// 한 physics step 에서 수집된 이벤트 묶음. Physics Thread 가 build 후 immutable 로 publish 하고,
// Game Thread 가 shared_ptr<const> 로 acquire 해서 delegate 로 broadcast 한다.
struct FPhysicsEventSnapshot
{
    uint64 StepIndex  = 0;
    uint64 FrameIndex = 0;

    TArray<FPhysicsContactEvent> Contacts;
    TArray<FPhysicsOverlapEvent> Overlaps;
};
