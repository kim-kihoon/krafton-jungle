#pragma once
#include "Object/Object.h"
#include "GameFramework/AActor.h"
#include "Level.h"
#include "Spatial/WorldSpatialIndex.h"
#include "Core/CollisionTypes.h"

class UCameraComponent;
class ULineBatchComponent;
class FViewportCamera;
class ULightComponentBase;
class APawnActor;
class APlayerStartActor;

/**
 * 원래는 데이터 복사본을 넣고 Dirty 여부에 따라 업데이트 해줘야 하지만
 * SceneProxy 개념 도입 전이므로 아래와 같이 포인터를 통해 접근
 * Pointer 에 대한 안전 체크는 Slot 을 통해 처리
 */
struct FLightSlot
{
    ULightComponentBase* LightData = nullptr;
    uint32 Generation = 0;
    bool bAlive = false;
};

struct FLightHandle
{
    uint32 Index = 0xFFFFFFFF;
    // Invalidate 검증용
    uint32 Generation = 0;

    bool IsValid() const { return Index != 0xFFFFFFFF; }
};

class UWorld : public UObject {
public:
    DECLARE_CLASS(UWorld, UObject)
	UWorld();
	~UWorld() override;

	virtual void PostDuplicate(UObject* Original) override;

	// 프로퍼티 시스템 — UObject 에서 상속
	// UWorld 는 현재 에디터에 노출할 스칼라 프로퍼티가 없습니다.
	// (PersistentLevel 은 PostDuplicate 에서 별도 처리)
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override {}
	void PostEditProperty(const char* PropertyName) override {}


    // Actor lifecycle
    template<typename T>
    T* SpawnActor()
	{
        // create and register an actor
        T* Actor = UObjectManager::Get().CreateObject<T>();
        Actor->SetWorld(this);
		PersistentLevel->AddActor(Actor);
        if (bHasBegunPlay && !Actor->HasBegunPlay())
        {
            Actor->BeginPlay();
        }
        SpatialIndex.FlushDirtyBounds();
        return Actor;
    }

    void DestroyActor(AActor* Actor) 
	{
        if (!Actor) return;

        Actor->EndPlay(EEndPlayReason::Type::Destroyed);

		/**
		 *  TODO:
		 * Light 들의 경우 부모 -> 자식 순으로 AddComponent 되는 구조라 임시로 역순 Unregister 로 해결
		 * 실제론 Actor - Component 생애주기를 잘 관리해줘야함
		 */
		const TArray<UActorComponent*>& Comps = Actor->GetComponents();
        for (int32 i = static_cast<int32>(Comps.size()) - 1; i >= 0; --i)
        {
            if (Comps[i])
                Comps[i]->OnUnregister();
        }
		
		PersistentLevel->RemoveActor(Actor);
        Actor->SetWorld(nullptr);
        UObjectManager::Get().DestroyObject(Actor);
    }

    void DeactivateActor(AActor* Actor);

	TArray<AActor*> GetActors() const { return PersistentLevel->GetActors(); }
	APawnActor* FindPawn() const;
	APlayerStartActor* FindPlayerStart() const;

	ULevel* GetPersistentLevel() const { return PersistentLevel; }

    void BeginPlay();      // Triggers BeginPlay on all actors
    void Tick(float DeltaTime);  // Drives the game loop every frame
    void EndPlay(EEndPlayReason::Type EndPlayReason); // Cleanup before world is destroyed

    /** @brief Rebuild the world BVH and bounds snapshot from all current primitives. */
    void RebuildSpatialIndex();

    /** @brief Flush pending bounds and visibility dirties into the world BVH. */
    void SyncSpatialIndex();

    bool HasBegunPlay() const { return bHasBegunPlay; }

    // Active Camera — EditorViewportClient 또는 PlayerController가 세팅
    void SetActiveCamera(FViewportCamera* InCamera) { ActiveCamera = InCamera; ActiveCameraComponent = nullptr; }
	void SetActiveCameraComponent(UCameraComponent* InCamera) { ActiveCameraComponent = InCamera; ActiveCamera = nullptr; }
	FViewportCamera* GetActiveCamera() const { return ActiveCamera; }
    UCameraComponent* GetActiveCameraComponent() const { return ActiveCameraComponent; }

    /** @brief Access the world-level primitive AABB/BVH manager. */
    FWorldSpatialIndex& GetSpatialIndex() { return SpatialIndex; }

    /** @brief Access the world-level primitive AABB/BVH manager. */
    const FWorldSpatialIndex& GetSpatialIndex() const { return SpatialIndex; }

    bool LineTraceSingle(const FRay& Ray, float MaxDistance, FHitResult& OutHit, const AActor* IgnoredActor = nullptr);
    bool LineTraceMulti(const FRay& Ray, float MaxDistance, TArray<FHitResult>& OutHits, const AActor* IgnoredActor = nullptr);

	EWorldType GetWorldType() const { return WorldType; }
	void SetWorldType(EWorldType InWorldType) { WorldType = InWorldType; }
	
	FLightHandle RegisterLight(ULightComponentBase* Comp);
    void UnregisterLight(ULightComponentBase* Comp);
    const TArray<FLightSlot>& GetWorldLightSlots() const { return WorldLightSlots; }

private:
	EWorldType WorldType = EWorldType::Editor;
	ULevel* PersistentLevel = nullptr;
	FViewportCamera* ActiveCamera = nullptr;
	UCameraComponent* ActiveCameraComponent = nullptr;
    FWorldSpatialIndex SpatialIndex;
    bool bHasBegunPlay = false;

	TArray<FLightSlot> WorldLightSlots;
    TArray<uint32> FreeLightSlotList;  // 삭제된 Light 의 Index 만 Free 로 등록
};
