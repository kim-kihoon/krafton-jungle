#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CollisionTypes.h"

class AActor;
class UPrimitiveComponent;
class UWorld;

struct FCollisionCandidate
{
	AActor* Actor = nullptr;
	UPrimitiveComponent* Component = nullptr;
	int32 ObjectIndex = -1;
};

// 월드의 SpatialIndex와 Collision 유틸리티를 이용해 Broad/Narrow 충돌 판정을 수행합니다.
// BVH Query를 통한 Broad Collision → 두 Shape 쌍에 대한 Local Collsion → 충돌 이벤트 처리 순으로 동작합니다.
struct FCollisionSystem
{
	static void UpdateWorldCollision(UWorld* World);

private:
	static void GatherCandidates(UWorld* World, TArray<FCollisionCandidate>& OutCandidates);
	static void ProcessBroadCollision(UWorld* World, const FCollisionCandidate& Candidate);
	static void ProcessNarrowCollision(const FCollisionCandidate& A, const FCollisionCandidate& B);
	static void ClearStaleCollisions(const TArray<FCollisionCandidate>& Candidates);
	static void ProcessBlocking(UPrimitiveComponent* A, UPrimitiveComponent* B);
};
