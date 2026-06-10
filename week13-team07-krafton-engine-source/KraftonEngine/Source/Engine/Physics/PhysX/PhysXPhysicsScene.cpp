#include "Physics/PhysX/PhysXPhysicsScene.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Component/Vehicle/FourWheeledVehicleMovementComponent.h"
#include "Physics/BodySetup/BodySetup.h"
#include "Physics/PhysX/PhysXShapeUtils.h"
#include "Math/MathUtils.h"
#include "Math/Quat.h"
#include "Object/Object.h"  // IsAliveObject
#include "Core/Logging/Log.h"
#include "Core/ProjectSettings.h"

#include <algorithm>
#include <cmath>
#include <vector>

// PhysX headers
#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleSDK.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleUtilControl.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleTireFriction.h>

using namespace physx;

// ============================================================
// PhysX Error Callback
// ============================================================
class FPhysXErrorCallback : public PxErrorCallback
{
public:
	void reportError(PxErrorCode::Enum code, const char* message,
		const char* file, int line) override
	{
		const char* severity = "Info";
		if (code == PxErrorCode::eABORT || code == PxErrorCode::eOUT_OF_MEMORY)
			severity = "Fatal";
		else if (code == PxErrorCode::eINTERNAL_ERROR || code == PxErrorCode::eINVALID_OPERATION)
			severity = "Error";
		else if (code == PxErrorCode::eINVALID_PARAMETER || code == PxErrorCode::ePERF_WARNING)
			severity = "Warning";
		else if (code == PxErrorCode::eDEBUG_WARNING)
			severity = "Warning";

		UE_LOG("[PhysX %s] %s (%s:%d)", severity, message, file, line);
	}
};

static FPhysXErrorCallback GPhysXErrorCallback;

namespace
{
	bool ResolvePhysXRaycastTarget(const PxRaycastHit& Block, FHitResult& OutHit)
	{
		if (Block.shape && Block.shape->userData)
		{
			UPrimitiveComponent* HitComponent = static_cast<UPrimitiveComponent*>(Block.shape->userData);
			if (!IsValid(HitComponent))
			{
				return false;
			}

			OutHit.HitComponent = HitComponent;

			AActor* HitActor = HitComponent->GetOwner();
			if (IsValid(HitActor))
			{
				OutHit.HitActor = HitActor;
			}

			return true;
		}

		if (Block.actor && Block.actor->userData)
		{
			AActor* HitActor = static_cast<AActor*>(Block.actor->userData);
			if (!IsValid(HitActor))
			{
				return false;
			}

			OutHit.HitActor = HitActor;
			return true;
		}

		return false;
	}

	bool ShouldUseBodyInstancePath(UPrimitiveComponent* Comp)
	{
		if (!IsValid(Comp))
		{
			return false;
		}

		if (Cast<UBoxComponent>(Comp) || Cast<USphereComponent>(Comp) || Cast<UCapsuleComponent>(Comp))
		{
			return true;
		}

		UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Comp);
		if (!StaticMeshComp)
		{
			return false;
		}

		UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();
		if (!StaticMesh)
		{
			return false;
		}

		const UBodySetup* BodySetup = StaticMesh->GetBodySetup();
		return BodySetup && BodySetup->HasSimpleCollision();
	}

	bool ShouldIgnoreActorForQuery(
		const PxRigidActor* Actor,
		const AActor* IgnoreActor,
		const TArray<UPrimitiveComponent*>& BodyInstanceComponents)
	{
		if (!IgnoreActor || !Actor)
		{
			return false;
		}

		if (Actor->userData == IgnoreActor)
		{
			return true;
		}

		for (UPrimitiveComponent* Comp : BodyInstanceComponents)
		{
			if (!IsValid(Comp))
			{
				continue;
			}

			const FBodyInstance* BodyInstance = Comp->GetBodyInstance();
			if (!BodyInstance || !BodyInstance->IsValidBodyInstance() || BodyInstance->Actor != Actor)
			{
				continue;
			}

			if (Comp->GetOwner() == IgnoreActor)
			{
				return true;
			}
		}

		return false;
	}
}
static PxDefaultAllocator GPhysXAllocator;

// ============================================================
// PhysX Foundation/Physics 싱글턴
// PxCreateFoundation은 프로세스당 1회만 허용 — 복수 Scene에서 공유
// ============================================================
static PxFoundation* GSharedFoundation = nullptr;
static PxPhysics* GSharedPhysics = nullptr;
static PxPvd* GSharedPvd = nullptr;
static PxPvdTransport* GSharedPvdTransport = nullptr;
static int32 GSharedRefCount = 0;
static bool GVehicleSDKInitialized = false;

static constexpr const char* GPvdHost = "127.0.0.1";
static constexpr int32 GPvdPort = 5425;
static constexpr uint32 GPvdTimeoutMs = 10;

static void FlushSharedPvdTransport()
{
	if (GSharedPvdTransport && GSharedPvdTransport->isConnected())
	{
		GSharedPvdTransport->flush();
	}
}

static bool AcquireSharedPhysX(PxFoundation*& OutFoundation, PxPhysics*& OutPhysics)
{
	OutFoundation = nullptr;
	OutPhysics = nullptr;

	if (GSharedRefCount == 0)
	{
		GSharedFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GPhysXAllocator, GPhysXErrorCallback);
		if (!GSharedFoundation)
		{
			UE_LOG("[PhysX] Failed to create shared foundation");
			return false;
		}

		GSharedPvd = PxCreatePvd(*GSharedFoundation);
		if (GSharedPvd)
		{
			GSharedPvdTransport = PxDefaultPvdSocketTransportCreate(GPvdHost, GPvdPort, GPvdTimeoutMs);
			if (GSharedPvdTransport)
			{
				const PxPvdInstrumentationFlags PvdFlags = PxPvdInstrumentationFlag::eDEBUG;
				if (GSharedPvd->connect(*GSharedPvdTransport, PvdFlags))
				{
					UE_LOG("[PhysX] Connected to PVD (%s:%d)", GPvdHost, GPvdPort);
				}
				else
				{
					UE_LOG("[PhysX] PVD connection unavailable (%s:%d)", GPvdHost, GPvdPort);
				}
			}
			else
			{
				UE_LOG("[PhysX] Failed to create PVD transport");
			}
		}
		else
		{
			UE_LOG("[PhysX] Failed to create PVD instance");
		}

		GSharedPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *GSharedFoundation, PxTolerancesScale(), true, GSharedPvd);
		if (!GSharedPhysics)
		{
			UE_LOG("[PhysX] Failed to create shared physics");
			if (GSharedPvd && GSharedPvd->isConnected())
			{
				GSharedPvd->disconnect();
			}
			if (GSharedPvdTransport)
			{
				GSharedPvdTransport->release();
				GSharedPvdTransport = nullptr;
			}
			if (GSharedPvd)
			{
				GSharedPvd->release();
				GSharedPvd = nullptr;
			}
			GSharedFoundation->release();
			GSharedFoundation = nullptr;
			return false;
		}

		if (!PxInitExtensions(*GSharedPhysics, GSharedPvd))
		{
			UE_LOG("[PhysX] Failed to initialize PhysX extensions");
			GSharedPhysics->release();
			GSharedPhysics = nullptr;
			if (GSharedPvd && GSharedPvd->isConnected())
			{
				GSharedPvd->disconnect();
			}
			if (GSharedPvdTransport)
			{
				GSharedPvdTransport->release();
				GSharedPvdTransport = nullptr;
			}
			if (GSharedPvd)
			{
				GSharedPvd->release();
				GSharedPvd = nullptr;
			}
			GSharedFoundation->release();
			GSharedFoundation = nullptr;
			return false;
		}

		if (PxInitVehicleSDK(*GSharedPhysics))
		{
			GVehicleSDKInitialized = true;
			// Engine Z-up, McLaren/기본 차량 비주얼은 -Y 전진 (UF1CarVisualControlComponent 기본값과 동일).
			PxVehicleSetBasisVectors(PxVec3(0.0f, 0.0f, 1.0f), PxVec3(0.0f, -1.0f, 0.0f));
			PxVehicleSetUpdateMode(PxVehicleUpdateMode::eACCELERATION);
			UE_LOG("[PhysXVehicle] Initialized PhysX vehicle SDK");
		}
		else
		{
			UE_LOG("[PhysXVehicle] Failed to initialize PhysX vehicle SDK");
		}
	}

	++GSharedRefCount;
	OutFoundation = GSharedFoundation;
	OutPhysics = GSharedPhysics;
	return true;
}

static void ReleaseSharedPhysX()
{
	if (GSharedRefCount <= 0)
	{
		GSharedRefCount = 0;
		return;
	}

	--GSharedRefCount;
	if (GSharedRefCount == 0)
	{
		if (GVehicleSDKInitialized)
		{
			PxCloseVehicleSDK();
			GVehicleSDKInitialized = false;
		}
		PxCloseExtensions();
		if (GSharedPhysics) { GSharedPhysics->release(); GSharedPhysics = nullptr; }
		FlushSharedPvdTransport();
		if (GSharedPvd && GSharedPvd->isConnected()) { GSharedPvd->disconnect(); }
		if (GSharedPvdTransport) { GSharedPvdTransport->release(); GSharedPvdTransport = nullptr; }
		if (GSharedPvd) { GSharedPvd->release(); GSharedPvd = nullptr; }
		if (GSharedFoundation) { GSharedFoundation->release(); GSharedFoundation = nullptr; }
	}
}

// ============================================================
// PhysX Simulation Event Callback
//
// PhysX 의 onContact / onTrigger 는 Scene->fetchResults(true) 진행 중에 호출되며,
// 그 안에서 직접 게임 측 핸들러(NotifyComponentHit 등)를 호출하면 핸들러가
// World->DestroyActor 같은 scene-mutating 작업을 해서 fetchResults 와 겹쳐 크래쉬한다.
//
// 따라서 콜백은 이벤트를 큐에 적재만 하고, FPhysXPhysicsScene::Tick 의 post-simulate
// 단계 끝에서 DispatchPendingEvents 가 한꺼번에 게임 측 Notify 를 호출한다. 이 시점은
// simulate/fetchResults 외부이므로 핸들러가 자유롭게 actor/component 를 추가/제거해도 안전.
// ============================================================
class FPhysXSimulationCallback : public PxSimulationEventCallback
{
public:
	struct FQueuedHit
	{
		UPrimitiveComponent* Self      = nullptr;  // Notify 가 호출되는 대상
		UPrimitiveComponent* Other     = nullptr;
		FVector              NormalImpulse{0,0,0};
		FHitResult           Hit;
		bool                 bBegin = true;       // false = end
	};

	struct FQueuedTrigger
	{
		UPrimitiveComponent* Self  = nullptr;
		UPrimitiveComponent* Other = nullptr;
		bool                 bBegin = true;        // false = end
	};

	// Block 접촉 → 큐에 적재
	void onContact(const PxContactPairHeader& PairHeader,
		const PxContactPair* Pairs, PxU32 Count) override
	{
		if (PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_0
			|| PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_1)
			return;

		for (PxU32 i = 0; i < Count; ++i)
		{
			const PxContactPair& CP = Pairs[i];
			const bool bBegin = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND);
			const bool bEnd = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST);
			if (!bBegin && !bEnd) continue;

			auto* CompA = CP.shapes[0] ? static_cast<UPrimitiveComponent*>(CP.shapes[0]->userData) : nullptr;
			auto* CompB = CP.shapes[1] ? static_cast<UPrimitiveComponent*>(CP.shapes[1]->userData) : nullptr;
			if (!CompA || !CompB) continue;

			if (bEnd)
			{
				FQueuedHit A;
				A.Self = CompA;
				A.Other = CompB;
				A.bBegin = false;
				PendingHits.push_back(A);

				FQueuedHit B;
				B.Self = CompB;
				B.Other = CompA;
				B.bBegin = false;
				PendingHits.push_back(B);
				continue;
			}

			// Contact point — 큐 dispatch 시점에 PxContactPair 가 이미 무효이므로 여기서 모두 추출.
			PxContactPairPoint ContactPoints[1];
			PxU32 NumPoints = CP.extractContacts(ContactPoints, 1);

			FVector ContactPos(0, 0, 0);
			FVector ContactNormal(0, 0, 1);
			float Penetration = 0.0f;

			if (NumPoints > 0)
			{
				ContactPos    = FVector(ContactPoints[0].position.x, ContactPoints[0].position.y, ContactPoints[0].position.z);
				ContactNormal = FVector(ContactPoints[0].normal.x,   ContactPoints[0].normal.y,   ContactPoints[0].normal.z);
				Penetration   = ContactPoints[0].separation; // 음수 = 관통
			}

			const FVector NormalImpulse = ContactNormal * Penetration;

			FQueuedHit A;
			A.Self                = CompA;
			A.Other               = CompB;
			A.NormalImpulse       = NormalImpulse;
			A.Hit.bHit            = true;
			A.Hit.HitComponent    = CompB;
			A.Hit.HitActor        = CompB->GetOwner();
			A.Hit.WorldHitLocation= ContactPos;
			A.Hit.ImpactNormal    = ContactNormal;
			A.Hit.WorldNormal     = ContactNormal;
			A.Hit.PenetrationDepth= -Penetration;
			PendingHits.push_back(A);

			FQueuedHit B;
			B.Self                 = CompB;
			B.Other                = CompA;
			B.NormalImpulse        = NormalImpulse * -1.0f;
			B.Hit.bHit             = true;
			B.Hit.HitComponent     = CompA;
			B.Hit.HitActor         = CompA->GetOwner();
			B.Hit.WorldHitLocation = ContactPos;
			B.Hit.ImpactNormal     = ContactNormal * -1.0f;
			B.Hit.WorldNormal      = ContactNormal * -1.0f;
			B.Hit.PenetrationDepth = -Penetration;
			PendingHits.push_back(B);
		}
	}

	// Trigger 진입/이탈 → 큐에 적재
	void onTrigger(PxTriggerPair* Pairs, PxU32 Count) override
	{
		for (PxU32 i = 0; i < Count; ++i)
		{
			const PxTriggerPair& TP = Pairs[i];

			if (TP.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
				continue;

			auto* TriggerComp = TP.triggerShape ? static_cast<UPrimitiveComponent*>(TP.triggerShape->userData) : nullptr;
			auto* OtherComp   = TP.otherShape   ? static_cast<UPrimitiveComponent*>(TP.otherShape->userData)   : nullptr;
			if (!TriggerComp || !OtherComp) continue;

			const bool bBegin = (TP.status == PxPairFlag::eNOTIFY_TOUCH_FOUND);
			const bool bEnd   = (TP.status == PxPairFlag::eNOTIFY_TOUCH_LOST);
			if (!bBegin && !bEnd) continue;

			if (TriggerComp->GetGenerateOverlapEvents())
			{
				PendingTriggers.push_back({ TriggerComp, OtherComp, bBegin });
			}
			if (OtherComp->GetGenerateOverlapEvents())
			{
				PendingTriggers.push_back({ OtherComp, TriggerComp, bBegin });
			}
		}
	}

	// FPhysXPhysicsScene::Tick 끝에서 호출. simulate/fetchResults 바깥이므로 핸들러가
	// 자유롭게 World->DestroyActor / SpawnActor / RegisterComponent 호출 가능.
	// 핸들러 도중 다른 컴포넌트가 destroy되는 경우 대비해 dispatch 직전에 IsAliveObject
	// 검증 — destroy된 포인터를 만지지 않는다.
	void DispatchPendingEvents()
	{
		// move-out — dispatch 도중 새 이벤트가 큐에 들어오는 일은 없지만, 안전하게 swap 후 처리.
		std::vector<FQueuedHit> HitsToDispatch;
		HitsToDispatch.swap(PendingHits);
		std::vector<FQueuedTrigger> TriggersToDispatch;
		TriggersToDispatch.swap(PendingTriggers);

		for (FQueuedHit& E : HitsToDispatch)
		{
			if (!IsValid(E.Self) || !IsValid(E.Other)) continue;
			AActor* OtherActor = E.Other->GetOwner();
			if (!IsValid(OtherActor)) continue;
			if (E.bBegin)
			{
				E.Self->NotifyComponentHit(E.Self, OtherActor, E.Other, E.NormalImpulse, E.Hit);
			}
			else
			{
				E.Self->NotifyComponentEndHit(E.Self, OtherActor, E.Other);
			}
		}

		for (FQueuedTrigger& E : TriggersToDispatch)
		{
			if (!IsValid(E.Self) || !IsValid(E.Other)) continue;
			AActor* OtherActor = E.Other->GetOwner();
			if (!IsValid(OtherActor)) continue;
			if (E.bBegin)
			{
				FHitResult DummyHit;
				E.Self->NotifyComponentBeginOverlap(E.Self, OtherActor, E.Other, 0, false, DummyHit);
			}
			else
			{
				E.Self->NotifyComponentEndOverlap(E.Self, OtherActor, E.Other, 0);
			}
		}
	}

	void ClearPendingEvents()
	{
		PendingHits.clear();
		PendingTriggers.clear();
	}

	void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
	void onWake(PxActor**, PxU32) override {}
	void onSleep(PxActor**, PxU32) override {}
	void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

private:
	std::vector<FQueuedHit>     PendingHits;
	std::vector<FQueuedTrigger> PendingTriggers;
};

// ============================================================
// Transform 변환 유틸
// ============================================================
static PxVec3 ToPxVec3(const FVector& V)
{
	return PxVec3(V.X, V.Y, V.Z);
}

static PxQuat ToPxQuat(const FQuat& Q)
{
	return PxQuat(Q.X, Q.Y, Q.Z, Q.W);
}

static FVector ToFVector(const PxVec3& V)
{
	return FVector(V.x, V.y, V.z);
}

static FQuat ToFQuat(const PxQuat& Q)
{
	return FQuat(Q.x, Q.y, Q.z, Q.w);
}

static PxTransform GetPxTransform(UPrimitiveComponent* Comp)
{
	FVector Pos = Comp->GetWorldLocation();
	FQuat Rot = Comp->GetWorldMatrix().ToQuat();
	return PxTransform(ToPxVec3(Pos), ToPxQuat(Rot));
}

static bool IsPoseDifferent(const PxTransform& A, const PxTransform& B, float PosThresholdSq, float RotDotThreshold)
{
	PxVec3 Delta = A.p - B.p;
	const float DistSq = Delta.x * Delta.x + Delta.y * Delta.y + Delta.z * Delta.z;
	const float QDot = std::abs(A.q.x * B.q.x + A.q.y * B.q.y + A.q.z * B.q.z + A.q.w * B.q.w);
	return DistSq > PosThresholdSq || QDot < RotDotThreshold;
}

// ============================================================
// Collision Filtering
// ============================================================
// filterData 레이아웃:
//   word0 = 자신의 ObjectType (ECollisionChannel)
//   word1 = Block 비트마스크 (해당 채널에 Block 응답인 비트)
//   word2 = Overlap 비트마스크 (해당 채널에 Overlap 응답인 비트)
//   word3 = 소유 액터 UUID — 같은 액터의 두 컴포넌트끼리 충돌을 무시하기 위함
//           (Native 측 O(N²) 루프의 `if (A->GetOwner() == B->GetOwner()) continue;` 가드와 동일 의미)
//           Owner가 없거나 UUID가 0이면 가드 미적용.

// PxFilterShader — 엔진의 채널/응답 매트릭스를 PhysX에서 처리
// 양쪽 모두 상대 채널에 대해 Block이면 물리 충돌, 한쪽이라도 Overlap이면 트리거, 그 외 무시
static PxFilterFlags KraftonFilterShader(
	PxFilterObjectAttributes attributes0, PxFilterData filterData0,
	PxFilterObjectAttributes attributes1, PxFilterData filterData1,
	PxPairFlags& pairFlags, const void* /*constantBlock*/, PxU32 /*constantBlockSize*/)
{
	// 같은 액터(같은 owner UUID)의 두 컴포넌트끼리는 충돌 무시.
	// Native 측 O(N²) 루프의 same-owner 가드와 동일 의미. 차량 차체-바퀴처럼
	// 한 액터가 여러 콜라이더를 가질 때 자기끼리 충돌 시뮬레이션되는 문제를 막는다.
	if (filterData0.word3 != 0 && filterData0.word3 == filterData1.word3)
	{
		return PxFilterFlag::eKILL;
	}

	// 트리거 처리 — 한쪽이라도 트리거면 오버랩 통지만
	if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
	{
		pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}

	PxU32 channelA = filterData0.word0; // A의 ObjectType
	PxU32 channelB = filterData1.word0; // B의 ObjectType

	// A가 B의 채널에 대해 Block인지, B가 A의 채널에 대해 Block인지
	bool bABlocksB = (filterData0.word1 & (1u << channelB)) != 0;
	bool bBBlocksA = (filterData1.word1 & (1u << channelA)) != 0;

	// 양쪽 모두 Block → 물리 충돌 + contact 콜백
	if (bABlocksB && bBBlocksA)
	{
		pairFlags = PxPairFlag::eCONTACT_DEFAULT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST
			| PxPairFlag::eNOTIFY_CONTACT_POINTS;
		return PxFilterFlag::eDEFAULT;
	}

	// 한쪽이라도 Overlap → 겹침 감지만 (물리적 밀어내기 없음).
	// 일반적으로 이 케이스는 위 trigger shape 분기에서 이미 처리되지만, 등록 시점에
	// trigger flag로 분류되지 않은 simulation shape pair인데 응답이 Overlap인 경우의
	// 안전망. eSOLVE_CONTACT 명시 제외 + eDETECT_DISCRETE_CONTACT + NOTIFY로 detection만.
	bool bAOverlapsB = (filterData0.word2 & (1u << channelB)) != 0;
	bool bBOverlapsA = (filterData1.word2 & (1u << channelA)) != 0;

	if (bAOverlapsB || bBOverlapsA)
	{
		pairFlags = PxPairFlag::eDETECT_DISCRETE_CONTACT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST;
		return PxFilterFlag::eDEFAULT;
	}

	// Ignore — 쌍 완전히 제거
	return PxFilterFlag::eKILL;
}

// ============================================================
// Lifecycle
// ============================================================

void FPhysXPhysicsScene::Initialize(UWorld* InWorld)
{
	// 재초기화 경로가 들어와도 shared PhysX ref-count가 깨지지 않도록 먼저 정리한다.
	Shutdown();

	World = InWorld;
	bShutdownComplete = false;
	PhysicsAccumulator = 0.0f;

	// Foundation / Physics — 프로세스 싱글턴 공유
	if (!AcquireSharedPhysX(Foundation, Physics))
	{
		UE_LOG("[PhysX] Failed to create Foundation or Physics");
		bShutdownComplete = true;
		World = nullptr;
		return;
	}
	bSharedPhysXAcquired = true;

	// CPU Dispatcher
	Dispatcher = PxDefaultCpuDispatcherCreate(2);
	if (!Dispatcher)
	{
		UE_LOG("[PhysX] Failed to create CPU dispatcher");
		Shutdown();
		return;
	}

	// Event callback
	EventCallback = new FPhysXSimulationCallback();

	// Scene
	PxSceneDesc SceneDesc(Physics->getTolerancesScale());
	SceneDesc.gravity = PxVec3(0.0f, 0.0f, -9.81f); // Z-up, m 단위
	SceneDesc.cpuDispatcher = Dispatcher;
	SceneDesc.filterShader = KraftonFilterShader;
	SceneDesc.simulationEventCallback = EventCallback;
	Scene = Physics->createScene(SceneDesc);

	if (!Scene)
	{
		UE_LOG("[PhysX] Failed to create Scene");
		Shutdown();
		return;
	}

	const bool bTransmitPvdScene = World && (World->GetWorldType() == EWorldType::Game || World->GetWorldType() == EWorldType::PIE);
	if (bTransmitPvdScene)
	{
		if (PxPvdSceneClient* PvdClient = Scene->getScenePvdClient())
		{
			PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
			PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
			PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
		}
	}

	// Default material (static friction, dynamic friction, restitution)
	DefaultMaterial = Physics->createMaterial(0.5f, 0.5f, 0.3f);
	if (!DefaultMaterial)
	{
		UE_LOG("[PhysX] Failed to create default material");
		Shutdown();
		return;
	}

	if (GVehicleSDKInitialized)
	{
		EnsureVehicleFrictionPairs();
	}

	UE_LOG("[PhysX] Initialized successfully (Scene=%p)", Scene);
}

void FPhysXPhysicsScene::Shutdown()
{
	if (bShutdownComplete)
	{
		return;
	}
	bShutdownComplete = true;

	if (EventCallback)
	{
		EventCallback->ClearPendingEvents();
	}
	PhysicsAccumulator = 0.0f;
	PendingBodyForces.clear();

	if (Scene)
	{
		if (PxPvdSceneClient* PvdClient = Scene->getScenePvdClient())
		{
			PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, false);
			PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, false);
			PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, false);
		}
		Scene->setSimulationEventCallback(nullptr);
		FlushSharedPvdTransport();
	}

	ReleaseVehicles();
	ReleaseBodyInstances();

	if (Scene)
	{
		Scene->release();
		Scene = nullptr;
		FlushSharedPvdTransport();
	}

	if (DefaultMaterial)
	{
		DefaultMaterial->release();
		DefaultMaterial = nullptr;
	}

	if (EventCallback)
	{
		delete EventCallback;
		EventCallback = nullptr;
	}

	if (Dispatcher)
	{
		Dispatcher->release();
		Dispatcher = nullptr;
	}

	Foundation = nullptr;
	Physics = nullptr;
	World = nullptr;

	if (bSharedPhysXAcquired)
	{
		bSharedPhysXAcquired = false;
		ReleaseSharedPhysX();
	}
}

FBodyInstanceInitParams FPhysXPhysicsScene::MakeBodyInstanceInitParams() const
{
	FBodyInstanceInitParams Params;
	Params.Physics = Physics;
	Params.Scene = Scene;
	Params.DefaultMaterial = DefaultMaterial;
	return Params;
}

void FPhysXPhysicsScene::ReleaseBodyInstances()
{
	const FBodyInstanceInitParams Params = MakeBodyInstanceInitParams();
	for (UPrimitiveComponent* Comp : BodyInstanceComponents)
	{
		if (IsValid(Comp))
		{
			FBodyInstance* BodyInstance = Comp->GetBodyInstance();
			if (BodyInstance && BodyInstance->IsValidBodyInstance())
			{
				BodyInstance->TermBody(Params);
			}
		}
	}
	BodyInstanceComponents.clear();
}

void FPhysXPhysicsScene::PruneInvalidBodyInstanceComponents()
{
	BodyInstanceComponents.erase(
		std::remove_if(BodyInstanceComponents.begin(), BodyInstanceComponents.end(),
			[](UPrimitiveComponent* Comp)
			{
				return !IsValid(Comp) || !Comp->GetBodyInstance() || !Comp->GetBodyInstance()->IsValidBodyInstance();
			}),
		BodyInstanceComponents.end());
}

bool FPhysXPhysicsScene::ShouldIgnoreActorForQuery(const PxRigidActor* Actor, const AActor* IgnoreActor) const
{
	return ::ShouldIgnoreActorForQuery(Actor, IgnoreActor, BodyInstanceComponents);
}

PxRigidDynamic* FPhysXPhysicsScene::GetDynamicActorForComponent(UPrimitiveComponent* Comp) const
{
	if (!Comp)
	{
		return nullptr;
	}

	const FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	if (!BodyInstance || !BodyInstance->IsValidBodyInstance() || !BodyInstance->Actor)
	{
		return nullptr;
	}

	return BodyInstance->Actor->is<PxRigidDynamic>();
}

FPhysXPhysicsScene::FPendingBodyForces* FPhysXPhysicsScene::FindOrAddPendingBodyForces(UPrimitiveComponent* Comp)
{
	if (!IsValid(Comp))
	{
		return nullptr;
	}

	for (FPendingBodyForces& Pending : PendingBodyForces)
	{
		if (Pending.Comp == Comp)
		{
			return &Pending;
		}
	}

	FPendingBodyForces& Pending = PendingBodyForces.emplace_back();
	Pending.Comp = Comp;
	return &Pending;
}

void FPhysXPhysicsScene::ClearPendingForcesForComponent(UPrimitiveComponent* Comp)
{
	PendingBodyForces.erase(
		std::remove_if(PendingBodyForces.begin(), PendingBodyForces.end(),
			[Comp](const FPendingBodyForces& Pending)
			{
				return Pending.Comp == Comp;
			}),
		PendingBodyForces.end());
}

void FPhysXPhysicsScene::ApplyPendingForces()
{
	if (!FProjectSettings::Get().Physics.bUsePendingForces)
	{
		return;
	}

	for (const FPendingBodyForces& Pending : PendingBodyForces)
	{
		PxRigidDynamic* Dyn = GetDynamicActorForComponent(Pending.Comp);
		if (!Dyn)
		{
			continue;
		}
		if (Dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
		{
			continue;
		}

		Dyn->addForce(ToPxVec3(Pending.Force));
		Dyn->addTorque(ToPxVec3(Pending.Torque));
		for (const FPendingForceAtLocation& ForceAtLocation : Pending.ForcesAtLocation)
		{
			PxRigidBodyExt::addForceAtPos(
				*Dyn,
				ToPxVec3(ForceAtLocation.Force),
				ToPxVec3(ForceAtLocation.WorldLocation));
		}
	}
}

namespace
{
	PxQueryHitType::Enum VehicleWheelRaycastPreFilter(
		PxFilterData /*filterData0*/,
		PxFilterData filterData1,
		const void* /*constantBlock*/,
		PxU32 /*constantBlockSize*/,
		PxHitFlags& /*queryFlags*/)
	{
		return ((filterData1.word2 & PhysXShapeUtils::VehicleDrivableQueryBit) != 0)
			? PxQueryHitType::eBLOCK
			: PxQueryHitType::eNONE;
	}

	PxQueryHitType::Enum VehicleWheelRaycastPostFilter(
		PxFilterData /*filterData0*/,
		PxFilterData /*filterData1*/,
		const void* /*constantBlock*/,
		PxU32 /*constantBlockSize*/,
		const PxQueryHit& hit)
	{
		if (static_cast<const PxRaycastHit&>(hit).hadInitialOverlap())
		{
			return PxQueryHitType::eNONE;
		}
		return PxQueryHitType::eBLOCK;
	}
}

struct FPhysXPhysicsScene::FVehicleSceneQueryResources
{
	PxBatchQuery* BatchQuery = nullptr;
	PxRaycastQueryResult* RaycastResults = nullptr;
	PxRaycastHit* RaycastHits = nullptr;
	uint32 Capacity = 0;
};

struct FPhysXPhysicsScene::FPhysXVehicleInstance
{
	UFourWheeledVehicleMovementComponent* Component = nullptr;
	UPrimitiveComponent* ChassisComponent = nullptr;
	PxVehicleDrive4W* Vehicle = nullptr;
	PxVehicleDrive4WRawInputData RawInput;
	PxWheelQueryResult WheelQueryResults[4];
	PxVehicleWheelQueryResult VehicleWheelQueryResult;
	FFourWheeledVehicleRuntimeState State;
	float DisplayEngineOmega = 0.0f;
	float CachedChassisMass = 800.0f;
	bool bCachedEnableDownforce = true;
	float CachedDownforceCoeff = 5.0f;
	float CachedMaxDownforceMultiplier = 3.5f;
};

namespace
{
	PxVehicleTireData MakeRacingSlickTireData(bool bFrontAxle, float LatGripScale)
	{
		PxVehicleTireData TireData;
		TireData.mType = 0;

		const float BaseLatStiffY = 0.3125f * (180.0f / PxPi);
		const float GripScale = std::max(LatGripScale, 1.0f);
		const float AxleBias = bFrontAxle ? 1.05f : 0.95f;
		TireData.mLatStiffY = BaseLatStiffY * GripScale * AxleBias;
		TireData.mLatStiffX = 1.8f;
		TireData.mLongitudinalStiffnessPerUnitGravity = 2200.0f;

		TireData.mFrictionVsSlipGraph[0][0] = 0.0f;
		TireData.mFrictionVsSlipGraph[0][1] = 1.0f;
		TireData.mFrictionVsSlipGraph[1][0] = bFrontAxle ? 0.10f : 0.11f;
		TireData.mFrictionVsSlipGraph[1][1] = bFrontAxle ? 1.20f : 1.15f;
		TireData.mFrictionVsSlipGraph[2][0] = 0.45f;
		TireData.mFrictionVsSlipGraph[2][1] = bFrontAxle ? 0.82f : 0.72f;

		return TireData;
	}

	PxVehicleWheelsSimData* CreateFourWheelSimData(const FFourWheeledVehicleRuntimeParams& Params)
	{
		PxVehicleWheelsSimData* WheelsSimData = PxVehicleWheelsSimData::allocate(4);
		if (!WheelsSimData)
		{
			return nullptr;
		}

		const float WheelRadius = std::max(Params.WheelRadius, 0.01f);
		const float WheelWidth = std::max(Params.WheelWidth, 0.01f);
		const float ChassisMass = std::max(Params.ChassisMass, 1.0f);
		const float SprungMass = ChassisMass * 0.25f;
		const float MaxSteerRad = Params.MaxSteerAngleDeg * PxPi / 180.0f;

		for (PxU32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			PxVehicleWheelData WheelData;
			WheelData.mRadius = WheelRadius;
			WheelData.mWidth = WheelWidth;
			WheelData.mMass = 20.0f;
			WheelData.mMOI = 0.5f * WheelData.mMass * WheelRadius * WheelRadius;
			WheelData.mMaxBrakeTorque = Params.BrakeTorque;
			WheelData.mMaxHandBrakeTorque = WheelIndex >= 2 ? Params.BrakeTorque : 0.0f;
			WheelData.mMaxSteer = WheelIndex < 2 ? MaxSteerRad : 0.0f;
			WheelsSimData->setWheelData(WheelIndex, WheelData);

			PxVehicleSuspensionData SuspensionData;
			SuspensionData.mSprungMass = SprungMass;
			SuspensionData.mSpringStrength = 35000.0f;
			SuspensionData.mSpringDamperRate = 4500.0f;
			SuspensionData.mMaxCompression = 0.12f;
			SuspensionData.mMaxDroop = 0.18f;
			WheelsSimData->setSuspensionData(WheelIndex, SuspensionData);

			const bool bFrontAxle = WheelIndex < 2;
			PxVehicleTireData TireData = MakeRacingSlickTireData(bFrontAxle, Params.TireLatGripScale);
			WheelsSimData->setTireData(WheelIndex, TireData);

			const PxVec3 WheelOffset = ToPxVec3(Params.WheelCenterOffsets[WheelIndex] - Params.CenterOfMassOffset);
			WheelsSimData->setWheelCentreOffset(WheelIndex, WheelOffset);
			WheelsSimData->setSuspTravelDirection(WheelIndex, PxVec3(0.0f, 0.0f, -1.0f));
			WheelsSimData->setSuspForceAppPointOffset(WheelIndex, WheelOffset);
			WheelsSimData->setTireForceAppPointOffset(WheelIndex, WheelOffset);
			WheelsSimData->setWheelShapeMapping(WheelIndex, -1);
			WheelsSimData->setSceneQueryFilterData(WheelIndex, PxFilterData());
		}

		PxVehicleTireLoadFilterData TireLoadFilter;
		TireLoadFilter.mMinNormalisedLoad = 0.0f;
		TireLoadFilter.mMinFilteredNormalisedLoad = 0.15f;
		TireLoadFilter.mMaxNormalisedLoad = 5.0f;
		TireLoadFilter.mMaxFilteredNormalisedLoad = 5.0f;
		WheelsSimData->setTireLoadFilterData(TireLoadFilter);
		return WheelsSimData;
	}

	PxVehicleDriveSimData4W CreateDriveSimData(const FFourWheeledVehicleRuntimeParams& Params)
	{
		PxVehicleDriveSimData4W DriveData;

		PxVehicleDifferential4WData DiffData;
		DiffData.mType = Params.bUseRearWheelDrive
			? PxVehicleDifferential4WData::eDIFF_TYPE_LS_REARWD
			: PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
		DriveData.setDiffData(DiffData);

		PxVehicleEngineData EngineData;
		const float MaxRPM = std::max(Params.EngineMaxRPM, 1000.0f);
		EngineData.mPeakTorque = std::max(Params.EnginePeakTorque, 1.0f);
		EngineData.mMaxOmega = MaxRPM * PxPi * 2.0f / 60.0f;
		EngineData.mMOI = 1.2f;
		EngineData.mDampingRateFullThrottle = 0.35f;
		EngineData.mDampingRateZeroThrottleClutchEngaged = 2.0f;
		EngineData.mDampingRateZeroThrottleClutchDisengaged = 0.35f;
		EngineData.mTorqueCurve.clear();
		EngineData.mTorqueCurve.addPair(0.0f, 0.20f);
		EngineData.mTorqueCurve.addPair(0.25f, 0.45f);
		EngineData.mTorqueCurve.addPair(0.45f, 0.70f);
		EngineData.mTorqueCurve.addPair(0.65f, 0.90f);
		EngineData.mTorqueCurve.addPair(0.82f, 1.00f);
		EngineData.mTorqueCurve.addPair(1.00f, 0.88f);
		DriveData.setEngineData(EngineData);

		PxVehicleGearsData GearsData;
		GearsData.mNbRatios = PxVehicleGearsData::eNINTH;
		GearsData.mFinalRatio = 3.60f;
		GearsData.mSwitchTime = 0.04f;
		GearsData.mRatios[PxVehicleGearsData::eREVERSE] = -3.40f;
		GearsData.mRatios[PxVehicleGearsData::eFIRST] = 4.58f;
		GearsData.mRatios[PxVehicleGearsData::eSECOND] = 3.44f;
		GearsData.mRatios[PxVehicleGearsData::eTHIRD] = 2.68f;
		GearsData.mRatios[PxVehicleGearsData::eFOURTH] = 2.20f;
		GearsData.mRatios[PxVehicleGearsData::eFIFTH] = 1.90f;
		GearsData.mRatios[PxVehicleGearsData::eSIXTH] = 1.72f;
		GearsData.mRatios[PxVehicleGearsData::eSEVENTH] = 1.59f;
		GearsData.mRatios[PxVehicleGearsData::eEIGHTH] = 1.51f;
		DriveData.setGearsData(GearsData);

		PxVehicleClutchData ClutchData;
		ClutchData.mStrength = 150.0f;
		DriveData.setClutchData(ClutchData);

		// PxVehicle basis: forward = -Y (see PxVehicleSetBasisVectors). Axle separation is along Y, track width along X.
		PxVehicleAckermannGeometryData AckermannData;
		AckermannData.mAccuracy = 1.0f;
		AckermannData.mAxleSeparation = std::abs(Params.WheelCenterOffsets[0].Y - Params.WheelCenterOffsets[2].Y);
		AckermannData.mFrontWidth = std::abs(Params.WheelCenterOffsets[1].X - Params.WheelCenterOffsets[0].X);
		AckermannData.mRearWidth = std::abs(Params.WheelCenterOffsets[3].X - Params.WheelCenterOffsets[2].X);
		DriveData.setAckermannGeometryData(AckermannData);

		return DriveData;
	}

	bool ReplaceWithVehicleChassisShape(
		PxRigidDynamic* ChassisActor,
		UPrimitiveComponent* ChassisComp,
		PxMaterial* Material,
		const FFourWheeledVehicleRuntimeParams& Params)
	{
		if (!ChassisActor || !ChassisComp || !Material)
		{
			return false;
		}

		const PxU32 NumShapes = ChassisActor->getNbShapes();
		if (NumShapes > 0)
		{
			std::vector<PxShape*> Shapes(NumShapes);
			const PxU32 Fetched = ChassisActor->getShapes(Shapes.data(), NumShapes);
			for (PxU32 ShapeIndex = 0; ShapeIndex < Fetched; ++ShapeIndex)
			{
				if (Shapes[ShapeIndex])
				{
					ChassisActor->detachShape(*Shapes[ShapeIndex]);
				}
			}
		}

		const float TrackWidth = std::abs(Params.WheelCenterOffsets[1].X - Params.WheelCenterOffsets[0].X);
		const float AxleSeparation = std::abs(Params.WheelCenterOffsets[2].Y - Params.WheelCenterOffsets[0].Y);
		const float HalfWidth = std::max(0.45f, TrackWidth * 0.35f);
		const float HalfLength = std::max(0.75f, AxleSeparation * 0.52f);
		const float HalfHeight = 0.22f;
		const float LocalZ = std::max(Params.WheelRadius + 0.32f, HalfHeight + 0.30f);

		PxShape* ChassisShape = PxRigidActorExt::createExclusiveShape(
			*ChassisActor,
			PxBoxGeometry(HalfWidth, HalfLength, HalfHeight),
			*Material);
		if (!ChassisShape)
		{
			return false;
		}

		ChassisShape->setLocalPose(PxTransform(PxVec3(0.0f, 0.0f, LocalZ)));
		PhysXShapeUtils::FinalizeShape(ChassisShape, ChassisComp);
		return true;
	}

	FString GearIndexToDisplay(uint32_t GearIndex)
	{
		switch (GearIndex)
		{
		case PxVehicleGearsData::eREVERSE: return "R";
		case PxVehicleGearsData::eNEUTRAL: return "N";
		case PxVehicleGearsData::eFIRST: return "1";
		case PxVehicleGearsData::eSECOND: return "2";
		case PxVehicleGearsData::eTHIRD: return "3";
		case PxVehicleGearsData::eFOURTH: return "4";
		case PxVehicleGearsData::eFIFTH: return "5";
		case PxVehicleGearsData::eSIXTH: return "6";
		case PxVehicleGearsData::eSEVENTH: return "7";
		case PxVehicleGearsData::eEIGHTH: return "8";
		default: return "?";
		}
	}

	FString GearDisplayForVehicle(const PxVehicleDrive4W* Vehicle)
	{
		if (!Vehicle)
		{
			return "--";
		}

		const uint32_t CurrentGear = Vehicle->mDriveDynData.getCurrentGear();
		const uint32_t TargetGear = Vehicle->mDriveDynData.getTargetGear();
		if (CurrentGear == PxVehicleGearsData::eNEUTRAL && TargetGear > PxVehicleGearsData::eNEUTRAL)
		{
			return GearIndexToDisplay(TargetGear);
		}
		return GearIndexToDisplay(CurrentGear);
	}

	float ComputeWheelDrivenEngineOmega(const PxVehicleDrive4W* Vehicle, uint32_t GearIndex)
	{
		if (!Vehicle || GearIndex == PxVehicleGearsData::eNEUTRAL)
		{
			return 0.0f;
		}

		const PxVehicleGearsData& GearsData = Vehicle->mDriveSimData.getGearsData();
		if (GearIndex >= GearsData.mNbRatios)
		{
			return 0.0f;
		}

		const float GearRatio = GearsData.mRatios[GearIndex] * GearsData.mFinalRatio;
		if (FMath::IsNearlyZero(GearRatio))
		{
			return 0.0f;
		}

		const float WheelRadius = std::max(Vehicle->mWheelsSimData.getWheelData(0).mRadius, 0.01f);
		const float WheelOmega = std::abs(Vehicle->computeForwardSpeed()) / WheelRadius;
		return WheelOmega * std::abs(GearRatio);
	}

	float ComputeGearShiftRevRatio(const PxVehicleDrive4W* Vehicle, uint32_t GearIndex, float DisplayEngineOmega)
	{
		if (!Vehicle)
		{
			return 0.0f;
		}

		const float MaxOmega = std::max(Vehicle->mDriveSimData.getEngineData().mMaxOmega, 1.0f);
		const float EngineOmega = std::max(
			std::max(Vehicle->mDriveDynData.getEngineRotationSpeed(), DisplayEngineOmega),
			ComputeWheelDrivenEngineOmega(Vehicle, GearIndex));
		return FMath::Clamp(EngineOmega / MaxOmega, 0.0f, 2.0f);
	}

	bool CanShiftUp(const PxVehicleDrive4W* Vehicle, float DisplayEngineOmega)
	{
		if (!Vehicle)
		{
			return false;
		}

		const uint32_t CurrentGear = Vehicle->mDriveDynData.getCurrentGear();
		const uint32_t TargetGear = Vehicle->mDriveDynData.getTargetGear();
		const uint32_t EffectiveGear = TargetGear > PxVehicleGearsData::eNEUTRAL ? TargetGear : CurrentGear;
		if (EffectiveGear >= PxVehicleGearsData::eEIGHTH)
		{
			return false;
		}
		if (EffectiveGear < PxVehicleGearsData::eFIRST)
		{
			return true;
		}

		constexpr float MinUpshiftRevRatio = 0.72f;
		return ComputeGearShiftRevRatio(Vehicle, EffectiveGear, DisplayEngineOmega) >= MinUpshiftRevRatio;
	}

	bool CanShiftDown(const PxVehicleDrive4W* Vehicle)
	{
		if (!Vehicle)
		{
			return false;
		}

		const uint32_t CurrentGear = Vehicle->mDriveDynData.getCurrentGear();
		const uint32_t TargetGear = Vehicle->mDriveDynData.getTargetGear();
		const uint32_t EffectiveGear = TargetGear > PxVehicleGearsData::eNEUTRAL ? TargetGear : CurrentGear;
		return EffectiveGear > PxVehicleGearsData::eREVERSE;
	}

	void UpdateVehicleEngineState(PxVehicleDrive4W* Vehicle, FFourWheeledVehicleRuntimeState& State, float& DisplayEngineOmega, float ThrottleInput, float DeltaTime)
	{
		if (!Vehicle)
		{
			return;
		}

		const uint32_t CurrentGear = Vehicle->mDriveDynData.getCurrentGear();
		const uint32_t TargetGear = Vehicle->mDriveDynData.getTargetGear();
		const uint32_t EffectiveGear = TargetGear > PxVehicleGearsData::eNEUTRAL ? TargetGear : CurrentGear;
		const float SimEngineOmega = std::max(
			Vehicle->mDriveDynData.getEngineRotationSpeed(),
			ComputeWheelDrivenEngineOmega(Vehicle, EffectiveGear));
		const float MaxOmega = std::max(Vehicle->mDriveSimData.getEngineData().mMaxOmega, 1.0f);
		const float IdleRPM = 1500.0f;
		const float IdleOmega = IdleRPM * (2.0f * PxPi) / 60.0f;
		if (DisplayEngineOmega <= 0.0f)
		{
			DisplayEngineOmega = IdleOmega;
		}

		if (EffectiveGear == PxVehicleGearsData::eNEUTRAL)
		{
			const float TargetOmega = IdleOmega + FMath::Clamp(ThrottleInput, 0.0f, 1.0f) * (MaxOmega - IdleOmega);
			const float RiseRate = MaxOmega * 8.0f;
			const float FallRate = MaxOmega * 1.8f;
			const float MaxDelta = (TargetOmega > DisplayEngineOmega ? RiseRate : FallRate) * std::max(DeltaTime, 0.0f);
			const float Delta = FMath::Clamp(TargetOmega - DisplayEngineOmega, -MaxDelta, MaxDelta);
			DisplayEngineOmega += Delta;
		}
		else
		{
			DisplayEngineOmega = std::max(SimEngineOmega, IdleOmega);
		}

		const float DisplayRPM = DisplayEngineOmega * 60.0f / (2.0f * PxPi);
		const float DisplayOmega = DisplayRPM * (2.0f * PxPi) / 60.0f;
		State.EngineRPM = DisplayRPM;
		State.EngineRPMRatio = FMath::Clamp(DisplayOmega / MaxOmega, 0.0f, 1.0f);

	}
}

FPhysXPhysicsScene::FPhysXVehicleInstance* FPhysXPhysicsScene::FindVehicle(UFourWheeledVehicleMovementComponent* VehicleComp) const
{
	for (FPhysXVehicleInstance* Vehicle : Vehicles)
	{
		if (Vehicle && Vehicle->Component == VehicleComp)
		{
			return Vehicle;
		}
	}
	return nullptr;
}

void FPhysXPhysicsScene::RegisterVehicle(UFourWheeledVehicleMovementComponent* VehicleComp, const FFourWheeledVehicleRuntimeParams& Params)
{
	if (!IsValid(VehicleComp) || !IsValid(Params.ChassisComponent) || !Scene || !Physics || !DefaultMaterial || !GVehicleSDKInitialized)
	{
		return;
	}

	if (FindVehicle(VehicleComp))
	{
		UpdateVehicle(VehicleComp, Params);
		return;
	}

	UPrimitiveComponent* ChassisComp = Params.ChassisComponent;
	if (UStaticMeshComponent* ChassisMeshComp = Cast<UStaticMeshComponent>(ChassisComp))
	{
		if (UStaticMesh* ChassisMesh = ChassisMeshComp->GetStaticMesh())
		{
			ChassisMesh->EnsureSimpleCollisionFromBounds();
		}
	}

	if (!ChassisComp->GetSimulatePhysics())
	{
		ChassisComp->SetSimulatePhysics(true);
	}

	if (!ChassisComp->GetBodyInstance()->IsValidBodyInstance())
	{
		RegisterComponent(ChassisComp);
	}

	PxRigidDynamic* ChassisActor = GetDynamicActorForComponent(ChassisComp);
	if (!ChassisActor)
	{
		UE_LOG("[PhysXVehicle] Failed to create vehicle: chassis has no PxRigidDynamic");
		return;
	}

	if (!ReplaceWithVehicleChassisShape(ChassisActor, ChassisComp, DefaultMaterial, Params))
	{
		UE_LOG("[PhysXVehicle] Failed to create vehicle chassis collision shape");
		return;
	}

	PxVehicleWheelsSimData* WheelsSimData = CreateFourWheelSimData(Params);
	if (!WheelsSimData)
	{
		UE_LOG("[PhysXVehicle] Failed to allocate wheel sim data");
		return;
	}

	const PxVehicleDriveSimData4W DriveSimData = CreateDriveSimData(Params);

	PxVehicleDrive4W* Vehicle = PxVehicleDrive4W::allocate(4);
	if (!Vehicle)
	{
		WheelsSimData->free();
		UE_LOG("[PhysXVehicle] Failed to allocate PxVehicleDrive4W");
		return;
	}

	PxRigidBodyExt::setMassAndUpdateInertia(*ChassisActor, std::max(Params.ChassisMass, 1.0f));
	ChassisActor->setCMassLocalPose(PxTransform(ToPxVec3(Params.CenterOfMassOffset)));
	Vehicle->setup(Physics, ChassisActor, *WheelsSimData, DriveSimData, 0);
	WheelsSimData->free();

	if (!Vehicle->getRigidDynamicActor())
	{
		Vehicle->free();
		UE_LOG("[PhysXVehicle] PxVehicleDrive4W::setup failed (chassis actor not bound)");
		return;
	}

	Vehicle->setToRestState();
	Vehicle->mDriveDynData.setUseAutoGears(false);
	Vehicle->mDriveDynData.setCurrentGear(PxVehicleGearsData::eNEUTRAL);
	Vehicle->mDriveDynData.setTargetGear(PxVehicleGearsData::eNEUTRAL);
	ChassisActor->wakeUp();

	FPhysXVehicleInstance* Instance = new FPhysXVehicleInstance();
	Instance->Component = VehicleComp;
	Instance->ChassisComponent = ChassisComp;
	Instance->Vehicle = Vehicle;
	Instance->VehicleWheelQueryResult.wheelQueryResults = Instance->WheelQueryResults;
	Instance->VehicleWheelQueryResult.nbWheelQueryResults = 4;
	Instance->State.bValid = true;
	CacheVehicleAeroParams(*Instance, Params);
	Vehicles.push_back(Instance);

	ApplyVehicleTireFriction(Params);
	EnsureVehicleFrictionPairs();
	EnsureVehicleBatchQuery();
	if (VehicleSceneQuery && VehicleSceneQuery->BatchQuery && VehicleSceneQuery->RaycastResults)
	{
		PxVehicleWheels* VehiclePtrs[1] = { Vehicle };
		PxVehicleSuspensionRaycasts(
			VehicleSceneQuery->BatchQuery,
			1,
			VehiclePtrs,
			WheelsPerVehicle,
			VehicleSceneQuery->RaycastResults);
		VehicleSceneQuery->BatchQuery->execute();
	}

	UE_LOG("[PhysXVehicle] Registered PxVehicleDrive4W");
}

void FPhysXPhysicsScene::UnregisterVehicle(UFourWheeledVehicleMovementComponent* VehicleComp)
{
	for (auto It = Vehicles.begin(); It != Vehicles.end(); ++It)
	{
		FPhysXVehicleInstance* Vehicle = *It;
		if (!Vehicle || Vehicle->Component != VehicleComp)
		{
			continue;
		}

		if (Vehicle->Vehicle)
		{
			Vehicle->Vehicle->free();
			Vehicle->Vehicle = nullptr;
		}
		delete Vehicle;
		Vehicles.erase(It);
		UE_LOG("[PhysXVehicle] Unregistered PxVehicleDrive4W");
		return;
	}
}

void FPhysXPhysicsScene::UpdateVehicle(UFourWheeledVehicleMovementComponent* VehicleComp, const FFourWheeledVehicleRuntimeParams& Params)
{
	FPhysXVehicleInstance* Instance = FindVehicle(VehicleComp);
	if (!Instance || !Instance->Vehicle)
	{
		return;
	}

	CacheVehicleAeroParams(*Instance, Params);
	ApplyVehicleTireFriction(Params);

	Instance->RawInput.setAnalogAccel(FMath::Clamp(Params.ThrottleInput, 0.0f, 1.0f));
	Instance->RawInput.setAnalogBrake(FMath::Clamp(Params.BrakeInput, 0.0f, 1.0f));
	Instance->RawInput.setAnalogSteer(FMath::Clamp(Params.SteerInput, -1.0f, 1.0f));

	Instance->Vehicle->mDriveDynData.setAnalogInput(PxVehicleDrive4WControl::eANALOG_INPUT_ACCEL, Instance->RawInput.getAnalogAccel());
	Instance->Vehicle->mDriveDynData.setAnalogInput(PxVehicleDrive4WControl::eANALOG_INPUT_BRAKE, Instance->RawInput.getAnalogBrake());
	Instance->Vehicle->mDriveDynData.setAnalogInput(PxVehicleDrive4WControl::eANALOG_INPUT_STEER_RIGHT, std::max(Instance->RawInput.getAnalogSteer(), 0.0f));
	Instance->Vehicle->mDriveDynData.setAnalogInput(PxVehicleDrive4WControl::eANALOG_INPUT_STEER_LEFT, std::max(-Instance->RawInput.getAnalogSteer(), 0.0f));

	if (Params.bUseManualGears)
	{
		Instance->Vehicle->mDriveDynData.setUseAutoGears(false);
		Instance->Vehicle->mDriveDynData.setGearUp(false);
		Instance->Vehicle->mDriveDynData.setGearDown(false);
		if (Params.bGearNeutralPressed)
		{
			Instance->Vehicle->mDriveDynData.setCurrentGear(PxVehicleGearsData::eNEUTRAL);
			Instance->Vehicle->mDriveDynData.setTargetGear(PxVehicleGearsData::eNEUTRAL);
		}
		else if (Params.bGearShiftUpPressed && CanShiftUp(Instance->Vehicle, Instance->DisplayEngineOmega))
		{
			Instance->Vehicle->mDriveDynData.setGearUp(true);
		}
		if (Params.bGearShiftDownPressed && CanShiftDown(Instance->Vehicle))
		{
			Instance->Vehicle->mDriveDynData.setGearDown(true);
		}
	}
	else
	{
		Instance->Vehicle->mDriveDynData.setUseAutoGears(true);
	}

	Instance->State.bValid = true;
	Instance->State.CurrentGear = static_cast<int32>(Instance->Vehicle->mDriveDynData.getCurrentGear());
	Instance->State.GearDisplay = GearDisplayForVehicle(Instance->Vehicle);
	Instance->State.ForwardSpeed = Instance->Vehicle->computeForwardSpeed();
	UpdateVehicleEngineState(Instance->Vehicle, Instance->State, Instance->DisplayEngineOmega, Params.ThrottleInput, 0.0f);
	const float FrontSteerDeg = Params.SteerInput * Params.MaxSteerAngleDeg;
	Instance->State.WheelSteerDeg[0] = FrontSteerDeg;
	Instance->State.WheelSteerDeg[1] = FrontSteerDeg;
}

bool FPhysXPhysicsScene::GetVehicleState(const UFourWheeledVehicleMovementComponent* VehicleComp, FFourWheeledVehicleRuntimeState& OutState) const
{
	for (const FPhysXVehicleInstance* Vehicle : Vehicles)
	{
		if (Vehicle && Vehicle->Component == VehicleComp)
		{
			OutState = Vehicle->State;
			return OutState.bValid;
		}
	}
	return false;
}

bool FPhysXPhysicsScene::IsVehicleChassisComponent(const UPrimitiveComponent* Comp) const
{
	for (const FPhysXVehicleInstance* Instance : Vehicles)
	{
		if (Instance && Instance->ChassisComponent == Comp)
		{
			return true;
		}
	}
	return false;
}

void FPhysXPhysicsScene::EnsureVehicleFrictionPairs()
{
	if (VehicleFrictionPairs || !DefaultMaterial || !GVehicleSDKInitialized)
	{
		return;
	}

	constexpr PxU32 NumTireTypes = 1;
	constexpr PxU32 NumSurfaceTypes = 1;
	VehicleFrictionPairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(NumTireTypes, NumSurfaceTypes);
	if (!VehicleFrictionPairs)
	{
		UE_LOG("[PhysXVehicle] Failed to allocate tire friction pairs");
		return;
	}

	PxVehicleDrivableSurfaceType SurfaceTypes[1];
	SurfaceTypes[0].mType = 0;
	const PxMaterial* SurfaceMaterial = DefaultMaterial;
	VehicleFrictionPairs->setup(NumTireTypes, NumSurfaceTypes, &SurfaceMaterial, SurfaceTypes);
	VehicleFrictionPairs->setTypePairFriction(0, 0, 1.85f);
}

void FPhysXPhysicsScene::ApplyVehicleTireFriction(const FFourWheeledVehicleRuntimeParams& Params)
{
	EnsureVehicleFrictionPairs();
	if (!VehicleFrictionPairs)
	{
		return;
	}

	const float Friction = std::clamp(Params.TireFrictionMultiplier, 0.5f, 3.0f);
	VehicleFrictionPairs->setTypePairFriction(0, 0, Friction);
}

void FPhysXPhysicsScene::ReleaseVehicleSceneQuery()
{
	if (VehicleSceneQuery)
	{
		if (VehicleSceneQuery->BatchQuery)
		{
			VehicleSceneQuery->BatchQuery->release();
		}
		delete[] VehicleSceneQuery->RaycastResults;
		delete[] VehicleSceneQuery->RaycastHits;
		delete VehicleSceneQuery;
		VehicleSceneQuery = nullptr;
	}

	if (VehicleFrictionPairs)
	{
		VehicleFrictionPairs->release();
		VehicleFrictionPairs = nullptr;
	}
}

void FPhysXPhysicsScene::EnsureVehicleBatchQuery()
{
	if (!Scene || !GVehicleSDKInitialized)
	{
		return;
	}

	const uint32 RequiredWheels = static_cast<uint32>(Vehicles.size()) * WheelsPerVehicle;
	if (RequiredWheels == 0)
	{
		if (VehicleSceneQuery)
		{
			if (VehicleSceneQuery->BatchQuery)
			{
				VehicleSceneQuery->BatchQuery->release();
			}
			delete[] VehicleSceneQuery->RaycastResults;
			delete[] VehicleSceneQuery->RaycastHits;
			delete VehicleSceneQuery;
			VehicleSceneQuery = nullptr;
		}
		return;
	}

	if (VehicleSceneQuery && VehicleSceneQuery->BatchQuery && VehicleSceneQuery->Capacity >= RequiredWheels)
	{
		return;
	}

	if (VehicleSceneQuery)
	{
		if (VehicleSceneQuery->BatchQuery)
		{
			VehicleSceneQuery->BatchQuery->release();
		}
		delete[] VehicleSceneQuery->RaycastResults;
		delete[] VehicleSceneQuery->RaycastHits;
		delete VehicleSceneQuery;
		VehicleSceneQuery = nullptr;
	}

	const uint32 Capacity = std::max(RequiredWheels, WheelsPerVehicle);
	FVehicleSceneQueryResources* QueryResources = new FVehicleSceneQueryResources();
	QueryResources->RaycastResults = new PxRaycastQueryResult[Capacity];
	QueryResources->RaycastHits = new PxRaycastHit[Capacity];
	for (uint32 Index = 0; Index < Capacity; ++Index)
	{
		new(QueryResources->RaycastResults + Index) PxRaycastQueryResult();
		new(QueryResources->RaycastHits + Index) PxRaycastHit();
	}

	PxBatchQueryDesc SqDesc(Capacity, 0, 0);
	SqDesc.queryMemory.userRaycastResultBuffer = QueryResources->RaycastResults;
	SqDesc.queryMemory.userRaycastTouchBuffer = QueryResources->RaycastHits;
	SqDesc.queryMemory.raycastTouchBufferSize = Capacity;
	SqDesc.preFilterShader = VehicleWheelRaycastPreFilter;
	SqDesc.postFilterShader = VehicleWheelRaycastPostFilter;
	QueryResources->BatchQuery = Scene->createBatchQuery(SqDesc);
	QueryResources->Capacity = Capacity;

	if (!QueryResources->BatchQuery)
	{
		UE_LOG("[PhysXVehicle] Failed to create vehicle batch query");
		delete[] QueryResources->RaycastResults;
		delete[] QueryResources->RaycastHits;
		delete QueryResources;
		return;
	}

	VehicleSceneQuery = QueryResources;
}

void FPhysXPhysicsScene::RunVehicleSuspensionRaycasts()
{
	if (Vehicles.empty() || !Scene)
	{
		return;
	}

	EnsureVehicleFrictionPairs();
	EnsureVehicleBatchQuery();
	if (!VehicleSceneQuery || !VehicleSceneQuery->BatchQuery || !VehicleSceneQuery->RaycastResults)
	{
		return;
	}

	PxVehicleWheels* VehiclePtrs[MaxVehicles];
	PxU32 NbVehicles = 0;
	for (FPhysXVehicleInstance* Instance : Vehicles)
	{
		if (Instance && Instance->Vehicle && NbVehicles < MaxVehicles)
		{
			VehiclePtrs[NbVehicles++] = Instance->Vehicle;
		}
	}
	if (NbVehicles == 0)
	{
		return;
	}

	const PxU32 NbWheels = NbVehicles * WheelsPerVehicle;
	if (NbWheels > VehicleSceneQuery->Capacity)
	{
		UE_LOG("[PhysXVehicle] Raycast buffer too small (%u wheels, capacity %u)", NbWheels, VehicleSceneQuery->Capacity);
		return;
	}

	PxVehicleSuspensionRaycasts(
		VehicleSceneQuery->BatchQuery,
		NbVehicles,
		VehiclePtrs,
		NbWheels,
		VehicleSceneQuery->RaycastResults);
	VehicleSceneQuery->BatchQuery->execute();
}

void FPhysXPhysicsScene::RunVehicleUpdates(float DeltaTime)
{
	if (Vehicles.empty() || !Scene || !VehicleFrictionPairs)
	{
		return;
	}

	PxVehicleWheels* VehiclePtrs[MaxVehicles];
	PxVehicleWheelQueryResult WheelQueryResultPtrs[MaxVehicles];
	PxU32 NbVehicles = 0;
	for (FPhysXVehicleInstance* Instance : Vehicles)
	{
		if (!Instance || !Instance->Vehicle || NbVehicles >= MaxVehicles)
		{
			continue;
		}

		for (PxU32 WheelIndex = 0; WheelIndex < WheelsPerVehicle; ++WheelIndex)
		{
			Instance->WheelQueryResults[WheelIndex] = PxWheelQueryResult();
		}
		Instance->VehicleWheelQueryResult.wheelQueryResults = Instance->WheelQueryResults;
		Instance->VehicleWheelQueryResult.nbWheelQueryResults = WheelsPerVehicle;

		VehiclePtrs[NbVehicles] = Instance->Vehicle;
		WheelQueryResultPtrs[NbVehicles] = Instance->VehicleWheelQueryResult;
		++NbVehicles;
	}
	if (NbVehicles == 0)
	{
		return;
	}

	for (FPhysXVehicleInstance* Instance : Vehicles)
	{
		if (Instance)
		{
			ApplySimpleDownforce(*Instance);
		}
	}

	const PxVec3 Gravity = Scene->getGravity();
	PxVehicleUpdates(DeltaTime, Gravity, *VehicleFrictionPairs, NbVehicles, VehiclePtrs, WheelQueryResultPtrs);

	for (FPhysXVehicleInstance* Instance : Vehicles)
	{
		if (!Instance || !Instance->Vehicle)
		{
			continue;
		}

		Instance->State.bValid = true;
		Instance->State.ForwardSpeed = Instance->Vehicle->computeForwardSpeed();
		Instance->State.CurrentGear = static_cast<int32>(Instance->Vehicle->mDriveDynData.getCurrentGear());
		Instance->State.GearDisplay = GearDisplayForVehicle(Instance->Vehicle);
		UpdateVehicleEngineState(Instance->Vehicle, Instance->State, Instance->DisplayEngineOmega, Instance->RawInput.getAnalogAccel(), DeltaTime);
		Instance->State.WheelSteerDeg[0] = Instance->WheelQueryResults[0].steerAngle * 180.0f / PxPi;
		Instance->State.WheelSteerDeg[1] = Instance->WheelQueryResults[1].steerAngle * 180.0f / PxPi;
		for (PxU32 WheelIndex = 0; WheelIndex < WheelsPerVehicle; ++WheelIndex)
		{
			const float SpinRadPerSec = Instance->Vehicle->mWheelsDynData.getWheelRotationSpeed(WheelIndex);
			Instance->State.WheelRotationDeg[WheelIndex] += SpinRadPerSec * DeltaTime * 180.0f / PxPi;
		}
	}
}

void FPhysXPhysicsScene::ReleaseVehicles()
{
	ReleaseVehicleSceneQuery();
	for (FPhysXVehicleInstance* Vehicle : Vehicles)
	{
		if (!Vehicle)
		{
			continue;
		}
		if (Vehicle->Vehicle)
		{
			Vehicle->Vehicle->free();
			Vehicle->Vehicle = nullptr;
		}
		delete Vehicle;
	}
	Vehicles.clear();
}

// ============================================================
// Body 관리 — UE-style per-component FBodyInstance
// ============================================================

void FPhysXPhysicsScene::RegisterComponent(UPrimitiveComponent* Comp)
{
	if (!IsValid(Comp) || !Scene || !Physics || !DefaultMaterial) return;
	if (!ShouldUseBodyInstancePath(Comp)) return;
	if (Comp->GetBodyInstance()->IsValidBodyInstance()) return;

	UBodySetup* BodySetup = nullptr;
	if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Comp))
	{
		if (UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh())
		{
			BodySetup = StaticMesh->GetBodySetup();
		}
	}

	Comp->GetBodyInstance()->InitBody(
		BodySetup,
		Comp,
		MakeBodyInstanceInitParams(),
		FInitBodySpawnParams(Comp));
	if (Comp->GetBodyInstance()->IsValidBodyInstance())
	{
		BodyInstanceComponents.push_back(Comp);
	}
}

void FPhysXPhysicsScene::UnregisterComponent(UPrimitiveComponent* Comp)
{
	if (!IsValid(Comp) || !Scene) return;

	ClearPendingForcesForComponent(Comp);

	if (!Comp->GetBodyInstance()->IsValidBodyInstance())
	{
		return;
	}

	Comp->GetBodyInstance()->TermBody(MakeBodyInstanceInitParams());
	BodyInstanceComponents.erase(
		std::remove(BodyInstanceComponents.begin(), BodyInstanceComponents.end(), Comp),
		BodyInstanceComponents.end());
}

void FPhysXPhysicsScene::RebuildBody(UPrimitiveComponent* Comp)
{
	if (!IsValid(Comp) || !Scene) return;

	ClearPendingForcesForComponent(Comp);
	UnregisterComponent(Comp);
	if (ShouldUseBodyInstancePath(Comp))
	{
		RegisterComponent(Comp);
	}
}

// ============================================================
// Simulation
// ============================================================

void FPhysXPhysicsScene::Tick(float DeltaTime)
{
	if (bShutdownComplete || !Scene || DeltaTime <= 0.0f) return;

	const float FixedPhysicsFPS = (std::max)(1.0f, (std::min)(FProjectSettings::Get().Physics.FixedPhysicsFPS, 1000.0f));
	const float FixedPhysicsStep = 1.0f / FixedPhysicsFPS;
	constexpr int32 MaxSubsteps = 6;
	const float MaxAccumulatedTime = FixedPhysicsStep * MaxSubsteps;
	constexpr float MaxClampedPhysicsDeltaTime = 0.1f;

	PruneInvalidBodyInstanceComponents();

	// ── Pre-simulate: Engine → PhysX Transform 동기화 (per-component) ──
	constexpr float TeleportPosThresholdSq = 1.0f;
	constexpr float TeleportRotThreshold = 0.99f;
	constexpr float StaticSyncPosThresholdSq = 0.0001f;
	constexpr float StaticSyncRotThreshold = 0.99999f;

	for (UPrimitiveComponent* Comp : BodyInstanceComponents)
	{
		if (!IsValid(Comp))
		{
			continue;
		}

		FBodyInstance* BodyInstance = Comp->GetBodyInstance();
		if (!BodyInstance || !BodyInstance->IsValidBodyInstance() || !BodyInstance->Actor)
		{
			continue;
		}

		// Vehicle chassis pose is driven by PxVehicleUpdates after scene simulate.
		if (IsVehicleChassisComponent(Comp))
		{
			continue;
		}

		PxTransform NewPose = GetPxTransform(Comp);

		if (PxRigidDynamic* Dynamic = BodyInstance->Actor->is<PxRigidDynamic>())
		{
			if (Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
			{
				if (IsPoseDifferent(NewPose, Dynamic->getGlobalPose(), StaticSyncPosThresholdSq, StaticSyncRotThreshold))
				{
					Dynamic->setKinematicTarget(NewPose);
				}
			}
			else
			{
				PxTransform PxPose = Dynamic->getGlobalPose();
				if (IsPoseDifferent(NewPose, PxPose, TeleportPosThresholdSq, TeleportRotThreshold))
				{
					Dynamic->setGlobalPose(NewPose);
				}
			}
		}
		else if (BodyInstance->Actor->is<PxRigidStatic>())
		{
			if (IsPoseDifferent(NewPose, BodyInstance->Actor->getGlobalPose(), StaticSyncPosThresholdSq, StaticSyncRotThreshold))
			{
				BodyInstance->Actor->setGlobalPose(NewPose);
			}
		}
	}

	bool bRanSimulationStep = false;
	if (FProjectSettings::Get().Physics.bUseFixedPhysicsStep)
	{
		PhysicsAccumulator = (std::min)(PhysicsAccumulator + DeltaTime, MaxAccumulatedTime);

		int32 SubstepCount = 0;
		while (PhysicsAccumulator >= FixedPhysicsStep && SubstepCount < MaxSubsteps)
		{
			RunVehicleSuspensionRaycasts();
			ApplyPendingForces();

			// ── Simulate ──
			Scene->simulate(FixedPhysicsStep);
			Scene->fetchResults(true);

			RunVehicleUpdates(FixedPhysicsStep);

			PhysicsAccumulator -= FixedPhysicsStep;
			++SubstepCount;
			bRanSimulationStep = true;
		}
	}
	else
	{
		PhysicsAccumulator = 0.0f;
		const float ClampedDeltaTime = (std::min)(DeltaTime, MaxClampedPhysicsDeltaTime);

		RunVehicleSuspensionRaycasts();
		ApplyPendingForces();

		// ── Simulate ──
		Scene->simulate(ClampedDeltaTime);
		Scene->fetchResults(true);

		RunVehicleUpdates(ClampedDeltaTime);
		bRanSimulationStep = true;
	}
	if (bRanSimulationStep)
	{
		PendingBodyForces.clear();
	}

	// ── Post-simulate: PhysX → Engine Transform 동기화 (per-component) ──
	for (UPrimitiveComponent* Comp : BodyInstanceComponents)
	{
		if (!IsValid(Comp))
		{
			continue;
		}

		FBodyInstance* BodyInstance = Comp->GetBodyInstance();
		if (!BodyInstance || !BodyInstance->IsValidBodyInstance() || !BodyInstance->Actor)
		{
			continue;
		}

		PxRigidDynamic* Dynamic = BodyInstance->Actor->is<PxRigidDynamic>();
		if (!Dynamic)
		{
			continue;
		}
		if (Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
		{
			continue;
		}

		BodyInstance->SyncBodyToComponent();
	}

	// ── Dispatch deferred contact/trigger events ──
	// onContact / onTrigger 는 fetchResults 안에서 fire 되므로 거기서 직접 게임 핸들러를
	// 부르면 핸들러의 World->DestroyActor 등이 PhysX scene 변경 타이밍과 겹쳐 크래쉬한다.
	// 그래서 큐에만 적재했고, 이 시점(simulate/fetchResults 외부)에서 한꺼번에 dispatch.
	if (EventCallback)
	{
		EventCallback->DispatchPendingEvents();
	}
}

bool FPhysXPhysicsScene::Sweep(const FVector& Start, const FVector& Dir, float MaxDist, const FCollisionShape& Shape, const FQuat& ShapeRot, FHitResult& OutHit, ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	if (!Scene) return false;

	// Raycast의 FChannelRaycastFilter와 동일한 로직, Sweep용으로 재선언
	struct FChannelSweepFilter : PxQueryFilterCallback
	{
		const TArray<UPrimitiveComponent*>& BodyInstanceComponents;
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;
		FChannelSweepFilter(
			const TArray<UPrimitiveComponent*>& InBodyInstanceComponents,
			const AActor* InIgnoreActor,
			ECollisionChannel InChannel)
			: BodyInstanceComponents(InBodyInstanceComponents)
			, IgnoreActor(InIgnoreActor)
			, TraceBit(1u << static_cast<PxU32>(InChannel))
		{
		}
		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (::ShouldIgnoreActorForQuery(Actor, IgnoreActor, BodyInstanceComponents))
				return PxQueryHitType::eNONE;

			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				if ((ShapeData.word1 & TraceBit) == 0)
					return PxQueryHitType::eNONE;
			}
			return PxQueryHitType::eBLOCK;
		}
		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	// FCollisionShape → PxGeometry 변환
	// GeometryHolder로 스택에 geometry 보관 (Sphere / Capsule / Box 지원)
	PxGeometryHolder GeomHolder;
	switch (Shape.ShapeType)
	{
	case ECollisionShape::Sphere:
		GeomHolder.storeAny(PxSphereGeometry(Shape.GetSphereRadius()));
		break;
	case ECollisionShape::Capsule:
		// PhysX capsule: halfHeight는 구 제외 실린더 절반 높이
		GeomHolder.storeAny(PxCapsuleGeometry(Shape.GetCapsuleRadius(),
			Shape.GetCapsuleHalfHeight() - Shape.GetCapsuleRadius()));
		break;
	case ECollisionShape::Box:
		GeomHolder.storeAny(PxBoxGeometry(ToPxVec3(Shape.GetExtent())));
		break;
	default:
		return false;
	}

	PxQuat SweepRot = ToPxQuat(ShapeRot);
	if (Shape.ShapeType == ECollisionShape::Capsule)
	{
		SweepRot = (SweepRot * PhysXShapeUtils::GetCapsuleAxisCorrectionQuat()).getNormalized();
	}

	const PxTransform PxStartPose(ToPxVec3(Start), SweepRot);
	const PxVec3 PxDir = ToPxVec3(Dir);

	PxSweepBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FChannelSweepFilter FilterCallback(BodyInstanceComponents, IgnoreActor, TraceChannel);

	bool bStatus = Scene->sweep(
		GeomHolder.any(),    // sweep geometry
		PxStartPose,         // 시작 pose (위치 + 회전)
		PxDir,               // 방향 (unit vector)
		MaxDist,             // 최대 거리
		Hit,
		PxHitFlag::eDEFAULT,
		FilterData,
		&FilterCallback
	);

	if (!bStatus || !Hit.hasBlock) return false;

	const PxSweepHit& Block = Hit.block;
	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = ToFVector(PxStartPose.p) + ToFVector(PxDir) * Block.distance;
	OutHit.ImpactNormal = ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;

	// distance == 0 이면 시작 지점에서 이미 겹침 (initial overlap)
	OutHit.bStartPenetrating = Block.distance <= 0.0f;

	if (Block.shape && Block.shape->userData)
	{
		OutHit.HitComponent = static_cast<UPrimitiveComponent*>(Block.shape->userData);
		OutHit.HitActor = OutHit.HitComponent->GetOwner();
	}
	else if (Block.actor && Block.actor->userData)
	{
		OutHit.HitActor = static_cast<AActor*>(Block.actor->userData);
	}

	return true;
}

// ============================================================
// Force / Torque
// ============================================================

void FPhysXPhysicsScene::AddForce(UPrimitiveComponent* Comp, const FVector& Force)
{
	if (!FProjectSettings::Get().Physics.bUsePendingForces)
	{
		PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
		if (!Dyn) return;
		Dyn->addForce(ToPxVec3(Force));
		return;
	}

	FPendingBodyForces* Pending = FindOrAddPendingBodyForces(Comp);
	if (!Pending) return;
	Pending->Force = Pending->Force + Force;
}

void FPhysXPhysicsScene::AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation)
{
	if (!FProjectSettings::Get().Physics.bUsePendingForces)
	{
		PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
		if (!Dyn) return;
		PxRigidBodyExt::addForceAtPos(*Dyn, ToPxVec3(Force), ToPxVec3(WorldLocation));
		return;
	}

	FPendingBodyForces* Pending = FindOrAddPendingBodyForces(Comp);
	if (!Pending) return;
	Pending->ForcesAtLocation.push_back(FPendingForceAtLocation{ Force, WorldLocation });
}

void FPhysXPhysicsScene::AddTorque(UPrimitiveComponent* Comp, const FVector& Torque)
{
	if (!FProjectSettings::Get().Physics.bUsePendingForces)
	{
		PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
		if (!Dyn) return;
		Dyn->addTorque(ToPxVec3(Torque));
		return;
	}

	FPendingBodyForces* Pending = FindOrAddPendingBodyForces(Comp);
	if (!Pending) return;
	Pending->Torque = Pending->Torque + Torque;
}

// ============================================================
// Velocity
// ============================================================

FVector FPhysXPhysicsScene::GetLinearVelocity(UPrimitiveComponent* Comp) const
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return { 0, 0, 0 };
	return ToFVector(Dyn->getLinearVelocity());
}

void FPhysXPhysicsScene::SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;
	Dyn->setLinearVelocity(ToPxVec3(Vel));
}

FVector FPhysXPhysicsScene::GetAngularVelocity(UPrimitiveComponent* Comp) const
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return { 0, 0, 0 };
	return ToFVector(Dyn->getAngularVelocity());
}

void FPhysXPhysicsScene::SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;
	Dyn->setAngularVelocity(ToPxVec3(Vel));
}

// ============================================================
// Gravity
// ============================================================

void FPhysXPhysicsScene::SetEnableGravity(UPrimitiveComponent* Comp, bool bEnable)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;

	Dyn->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !bEnable);
	Dyn->wakeUp();
}

// ============================================================
// Mass
// ============================================================

void FPhysXPhysicsScene::SetMass(UPrimitiveComponent* Comp, float NewMass)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;

	PxVec3 LocalCOM = ToPxVec3(Comp->GetCenterOfMass());
	PxRigidBodyExt::setMassAndUpdateInertia(*Dyn, NewMass, &LocalCOM);
}

float FPhysXPhysicsScene::GetMass(UPrimitiveComponent* Comp) const
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return 1.0f;
	return Dyn->getMass();
}

void FPhysXPhysicsScene::SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset)
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return;
	Dyn->setCMassLocalPose(PxTransform(ToPxVec3(LocalOffset)));
}

FVector FPhysXPhysicsScene::GetCenterOfMass(UPrimitiveComponent* Comp) const
{
	PxRigidDynamic* Dyn = GetDynamicActorForComponent(Comp);
	if (!Dyn) return { 0, 0, 0 };
	return ToFVector(Dyn->getCMassLocalPose().p);
}

// ============================================================
// Raycast
// ============================================================

bool FPhysXPhysicsScene::Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	if (!Scene) return false;

	// Channel + IgnoreActor 통합 filter.
	// shape의 queryFilterData는 SetupFilterData에서 word0=ObjectType, word1=Block 마스크.
	// 응답이 TraceChannel에 대해 Block(=word1의 해당 비트 set)인 shape만 hit으로 인정.
	// trigger flag가 set된 shape는 PhysX 측 query에서 자동 제외되므로 별도 처리 불필요.
	struct FChannelRaycastFilter : PxQueryFilterCallback
	{
		const TArray<UPrimitiveComponent*>& BodyInstanceComponents;
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;

		FChannelRaycastFilter(
			const TArray<UPrimitiveComponent*>& InBodyInstanceComponents,
			const AActor* InIgnoreActor,
			ECollisionChannel InChannel)
			: BodyInstanceComponents(InBodyInstanceComponents)
			, IgnoreActor(InIgnoreActor)
			, TraceBit(1u << static_cast<PxU32>(InChannel))
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (::ShouldIgnoreActorForQuery(Actor, IgnoreActor, BodyInstanceComponents))
			{
				return PxQueryHitType::eNONE;
			}

			// shape의 응답이 TraceChannel에 대해 Block인지 확인.
			// (word1[TraceChannel 비트]가 set이면 Block 응답)
			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				if ((ShapeData.word1 & TraceBit) == 0)
				{
					return PxQueryHitType::eNONE;
				}
			}

			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxRaycastBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FChannelRaycastFilter FilterCallback(BodyInstanceComponents, IgnoreActor, TraceChannel);

	bool bStatus = Scene->raycast(ToPxVec3(Start), ToPxVec3(Dir), MaxDist, Hit, PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxRaycastHit& Block = Hit.block;
	if (!ResolvePhysXRaycastTarget(Block, OutHit))
	{
		return false;
	}

	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = ToFVector(Block.position);
	OutHit.ImpactNormal = ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;

	return true;
}

bool FPhysXPhysicsScene::RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	uint32 ObjectTypeMask, const AActor* IgnoreActor) const
{
	if (!Scene || ObjectTypeMask == 0) return false;

	// SetupFilterData (line ~322) 에서 word0 = ObjectType (채널 enum 값) 으로 set.
	// ObjectType 마스크 비트 검사로 hit 후보 필터.
	// Trigger flag shape 는 PhysX 측 query 단계에서 자동 제외.
	struct FObjectTypeRaycastFilter : PxQueryFilterCallback
	{
		const TArray<UPrimitiveComponent*>& BodyInstanceComponents;
		const AActor* IgnoreActor = nullptr;
		PxU32 ObjectTypeMask = 0;

		FObjectTypeRaycastFilter(
			const TArray<UPrimitiveComponent*>& InBodyInstanceComponents,
			const AActor* InIgnoreActor,
			PxU32 InMask)
			: BodyInstanceComponents(InBodyInstanceComponents)
			, IgnoreActor(InIgnoreActor)
			, ObjectTypeMask(InMask)
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (::ShouldIgnoreActorForQuery(Actor, IgnoreActor, BodyInstanceComponents))
			{
				return PxQueryHitType::eNONE;
			}
			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				const PxU32 ShapeObjectBit = 1u << ShapeData.word0;
				if ((ShapeObjectBit & ObjectTypeMask) == 0)
				{
					return PxQueryHitType::eNONE;
				}
			}
			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxRaycastBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FObjectTypeRaycastFilter FilterCallback(BodyInstanceComponents, IgnoreActor, ObjectTypeMask);

	bool bStatus = Scene->raycast(ToPxVec3(Start), ToPxVec3(Dir), MaxDist, Hit, PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxRaycastHit& Block = Hit.block;
	if (!ResolvePhysXRaycastTarget(Block, OutHit))
	{
		return false;
	}

	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = ToFVector(Block.position);
	OutHit.ImpactNormal = ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;

	return true;
}

void FPhysXPhysicsScene::CacheVehicleAeroParams(FPhysXVehicleInstance& Instance, const FFourWheeledVehicleRuntimeParams& Params) const
{
	Instance.CachedChassisMass = std::max(Params.ChassisMass, 1.0f);
	Instance.bCachedEnableDownforce = Params.bEnableDownforce;
	Instance.CachedDownforceCoeff = std::max(Params.DownforceCoeff, 0.0f);
	Instance.CachedMaxDownforceMultiplier = std::max(Params.MaxDownforceMultiplier, 0.0f);
}

void FPhysXPhysicsScene::ApplySimpleDownforce(FPhysXVehicleInstance& Instance) const
{
	if (!Instance.bCachedEnableDownforce || Instance.CachedDownforceCoeff <= 0.0f || !Instance.Vehicle)
	{
		return;
	}

	PxRigidDynamic* ChassisActor = Instance.Vehicle->getRigidDynamicActor();
	if (!ChassisActor)
	{
		return;
	}

	const PxVec3 Velocity = ChassisActor->getLinearVelocity();
	const float HorizontalSpeedSq = Velocity.x * Velocity.x + Velocity.y * Velocity.y;
	if (HorizontalSpeedSq < 0.25f)
	{
		return;
	}

	float DownforceN = Instance.CachedDownforceCoeff * HorizontalSpeedSq;
	if (Instance.CachedMaxDownforceMultiplier > 0.0f)
	{
		const float MaxDownforceN = Instance.CachedChassisMass * 9.81f * Instance.CachedMaxDownforceMultiplier;
		DownforceN = std::min(DownforceN, MaxDownforceN);
	}

	ChassisActor->addForce(PxVec3(0.0f, 0.0f, -DownforceN), PxForceMode::eFORCE);
}
