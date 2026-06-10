#include "Editor/Selection/SelectionManager.h"
#include "Object/Object.h"
#include "Component/ActorComponent.h"
#include "Component/Debug/GizmoComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Render/Scene/FScene.h"
#include "Object/GarbageCollection.h"

#include <algorithm>

FSelectionDetailTarget FSelectionDetailTarget::FromObject(UObject* Object)
{
    FSelectionDetailTarget Target;
    if (IsValid(Object))
    {
        Target.ObjectPtr = Object;
        Target.StructType = Object->GetClass();
        Target.ContainerPtr = Object;
    }
    return Target;
}

void FSelectionDetailTarget::Reset()
{
    ObjectPtr = nullptr;
    StructType = nullptr;
    ContainerPtr = nullptr;
}

bool FSelectionDetailTarget::HasTarget() const
{
    return StructType != nullptr && ContainerPtr != nullptr;
}

bool FSelectionDetailTarget::IsValidTarget() const
{
    if (!HasTarget())
    {
        return false;
    }

    return ObjectPtr == nullptr || IsValid(ObjectPtr);
}

namespace
{
    UActorComponent* GetComponentFromTarget(const FSelectionDetailTarget& Target)
    {
        return Target.IsValidTarget() ? Cast<UActorComponent>(Target.ObjectPtr) : nullptr;
    }
}

USceneComponent* FSelectionManager::GetSelectedComponent() const
{
    return SelectedComponent.Get();
}

UActorComponent* FSelectionManager::GetSelectedActorComponent() const
{
    const FSelectionDetailTarget* PrimaryTarget = GetPrimaryDetailTarget();
    return PrimaryTarget ? GetComponentFromTarget(*PrimaryTarget) : nullptr;
}

bool FSelectionManager::IsComponentDetailsSelected() const
{
    return GetSelectedActorComponent() != nullptr;
}

const FSelectionDetailTarget* FSelectionManager::GetPrimaryDetailTarget() const
{
    for (const FSelectionDetailTarget& Target : SelectedDetailTargets)
    {
        if (Target.IsValidTarget())
        {
            return &Target;
        }
    }

    return nullptr;
}

bool FSelectionManager::IsSelected(AActor* Actor) const
{
    if (!IsValid(Actor))
    {
        return false;
    }

    return std::find_if(
        SelectedActors.begin(),
        SelectedActors.end(),
        [Actor](const TWeakObjectPtr<AActor>& SelectedActor)
        {
            return SelectedActor.Get() == Actor;
        }) != SelectedActors.end();
}

AActor* FSelectionManager::GetPrimarySelection() const
{
    for (const TWeakObjectPtr<AActor>& ActorRef : SelectedActors)
    {
        if (AActor* Actor = ActorRef.Get())
        {
            return Actor;
        }
    }

    return nullptr;
}

UGizmoComponent* FSelectionManager::GetGizmo() const
{
    return IsValid(Gizmo) ? Gizmo : nullptr;
}

TArray<AActor*> FSelectionManager::GetSelectedActors() const
{
    TArray<AActor*> ValidActors;
    ValidActors.reserve(SelectedActors.size());

    for (const TWeakObjectPtr<AActor>& ActorRef : SelectedActors)
    {
        if (AActor* Actor = ActorRef.Get())
        {
            ValidActors.push_back(Actor);
        }
    }

    return ValidActors;
}

void FSelectionManager::Init()
{
    Gizmo = UObjectManager::Get().CreateObject<UGizmoComponent>();
    if (!Gizmo)
    {
        return;
    }

    Gizmo->SetWorldLocation(FVector(0.0f, 0.0f, 0.0f));
    Gizmo->Deactivate();
}

void FSelectionManager::Shutdown()
{
    ClearSelection();
    World.Reset();

    if (Gizmo)
    {
        UObjectManager::Get().DestroyObject(Gizmo);
        Gizmo = nullptr;
    }
}

void FSelectionManager::Select(AActor* Actor)
{
    PruneInvalidSelection();

    if (!IsValid(Actor))
    {
        ClearSelection();
        return;
    }

    USceneComponent* RootComponent = Actor->GetRootComponent();
    if (!IsValid(RootComponent))
    {
        ClearSelection();
        return;
    }

    const FSelectionDetailTarget* PrimaryTarget = GetPrimaryDetailTarget();
    if (SelectedActors.size() == 1 &&
        SelectedActors.front().Get() == Actor &&
        SelectedComponent.Get() == RootComponent &&
        PrimaryTarget &&
        PrimaryTarget->ObjectPtr == Actor)
    {
        return;
    }

    // 기존 선택 해제
    for (const TWeakObjectPtr<AActor>& PrevRef : SelectedActors)
    {
        if (AActor* Prev = PrevRef.Get())
        {
            SetActorProxiesSelected(Prev, false);
        }
    }

    SelectedActors.clear();
    SelectedActors.push_back(Actor);
    SetActorProxiesSelected(Actor, true);
    SelectedComponent = RootComponent;
    SetSingleDetailTarget(FSelectionDetailTarget::FromObject(Actor));

    SyncGizmo();
}

void FSelectionManager::SelectRange(AActor* ClickedActor, const TArray<AActor*>& ActorList)
{
    PruneInvalidSelection();

    if (!IsValid(ClickedActor)) return;

    // Find index of clicked actor
    int32 ClickedIdx = -1;
    for (int32 i = 0; i < static_cast<int32>(ActorList.size()); ++i)
    {
        if (ActorList[i] == ClickedActor)
        {
            ClickedIdx = i;
            break;
        }
    }
    if (ClickedIdx == -1) return;

    // Find nearest already-selected actor's index in ActorList
    int32 AnchorIdx = ClickedIdx;
    int32 MinDist   = INT_MAX;
    for (const TWeakObjectPtr<AActor>& SelRef : SelectedActors)
    {
        AActor* Sel = SelRef.Get();
        if (!IsValid(Sel))
        {
            continue;
        }

        for (int32 i = 0; i < static_cast<int32>(ActorList.size()); ++i)
        {
            if (ActorList[i] == Sel)
            {
                int32 Dist = std::abs(i - ClickedIdx);
                if (Dist < MinDist)
                {
                    MinDist   = Dist;
                    AnchorIdx = i;
                }
                break;
            }
        }
    }

    // Replace selection with range [min, max]
    int32 Lo = std::min(AnchorIdx, ClickedIdx);
    int32 Hi = std::max(AnchorIdx, ClickedIdx);

    // 기존 선택 해제
    for (const TWeakObjectPtr<AActor>& PrevRef : SelectedActors) { if (AActor* Prev = PrevRef.Get()) SetActorProxiesSelected(Prev, false); }

    SelectedActors.clear();
    SelectedDetailTargets.clear();
    SelectedComponent.Reset();

    for (int32 i = Lo; i <= Hi; ++i)
    {
        AActor* Actor = ActorList[i];
        if (IsValid(Actor))
        {
            SelectedActors.push_back(Actor);
            SetActorProxiesSelected(Actor, true);
            AddActorDetailTarget(Actor);
        }
    }

    PruneInvalidSelection();
    SyncGizmo();
}

void FSelectionManager::ToggleSelect(AActor* Actor)
{
    PruneInvalidSelection();

    if (!IsValid(Actor)) return;

    auto It = std::find_if(SelectedActors.begin(), SelectedActors.end(), [Actor](const TWeakObjectPtr<AActor>& Ref) { return Ref.Get() == Actor; });
    if (It != SelectedActors.end())
    {
        SetActorProxiesSelected(Actor, false);
        SelectedActors.erase(It);
        SelectedDetailTargets.erase(
            std::remove_if(
                SelectedDetailTargets.begin(),
                SelectedDetailTargets.end(),
                [Actor](const FSelectionDetailTarget& Target)
                {
                    if (Target.ObjectPtr == Actor)
                    {
                        return true;
                    }
                    if (UActorComponent* Component = Cast<UActorComponent>(Target.ObjectPtr))
                    {
                        return Component->GetOwner() == Actor;
                    }
                    return false;
                }),
            SelectedDetailTargets.end());
        if (USceneComponent* AliveComponent = SelectedComponent.GetAlive())
        {
            if (AliveComponent->GetOwner() == Actor)
            {
                SelectedComponent.Reset();
                PruneInvalidSelection();
            }
        }
    }
    else
    {
        SelectedActors.push_back(Actor);
        SetActorProxiesSelected(Actor, true);
        AddActorDetailTarget(Actor);
        if (SelectedActors.size() == 1)
        {
            USceneComponent* RootComponent = Actor->GetRootComponent();
            SelectedComponent              = IsValid(RootComponent) ? RootComponent : nullptr;
        }
    }
    SyncGizmo();
}

void FSelectionManager::Deselect(AActor* Actor)
{
    PruneInvalidSelection();

    auto It = std::find_if(SelectedActors.begin(), SelectedActors.end(), [Actor](const TWeakObjectPtr<AActor>& Ref) { return Ref.Get() == Actor; });
    if (It != SelectedActors.end())
    {
        SetActorProxiesSelected(Actor, false);
        SelectedActors.erase(It);
        SelectedDetailTargets.erase(
            std::remove_if(
                SelectedDetailTargets.begin(),
                SelectedDetailTargets.end(),
                [Actor](const FSelectionDetailTarget& Target)
                {
                    if (Target.ObjectPtr == Actor)
                    {
                        return true;
                    }
                    if (UActorComponent* Component = Cast<UActorComponent>(Target.ObjectPtr))
                    {
                        return Component->GetOwner() == Actor;
                    }
                    return false;
                }),
            SelectedDetailTargets.end());
        if (USceneComponent* AliveComponent = SelectedComponent.GetAlive())
        {
            if (AliveComponent->GetOwner() == Actor)
            {
                SelectedComponent.Reset();
                PruneInvalidSelection();
            }
        }
    }
    SyncGizmo();
}

void FSelectionManager::ClearSelection()
{
    PruneInvalidSelection();

    if (SelectedActors.empty() && SelectedComponent.Get() == nullptr)
    {
        return;
    }

    for (const TWeakObjectPtr<AActor>& ActorRef : SelectedActors)
    {
        if (AActor* Actor = ActorRef.Get())
        {
            SetActorProxiesSelected(Actor, false);
        }
    }

    SelectedActors.clear();
    SelectedDetailTargets.clear();
    SelectedComponent.Reset();
    SyncGizmo();
}

int32 FSelectionManager::DeleteSelectedActors()
{
    PruneInvalidSelection();

    if (!IsValid(World.Get()) || SelectedActors.empty())
    {
        return 0;
    }

    TArray<AActor*> ActorsToDelete = GetSelectedActors();
    const int32     DeletedCount   = static_cast<int32>(ActorsToDelete.size());

    // 파괴 전에 선택/기즈모 참조를 먼저 끊어 dangling target을 방지한다.
    ClearSelection();

    World->BeginDeferredPickingBVHUpdate();
    for (AActor* Actor : ActorsToDelete)
    {
        if (!IsValid(Actor))
        {
            continue;
        }

        World->DestroyActor(Actor);
    }
    World->EndDeferredPickingBVHUpdate();

    return DeletedCount;
}

void FSelectionManager::Tick()
{
    PruneInvalidSelection();

    if (!IsValid(Gizmo) || !bGizmoEnabled)
    {
        return;
    }

    USceneComponent* Primary = SelectedComponent.Get();
    if (!IsValid(Primary))
    {
        return;
    }

    if (Gizmo->GetTargetComponent() != Primary)
    {
        SyncGizmo();
        return;
    }

    Gizmo->UpdateGizmoTransform();
}

void FSelectionManager::SelectComponent(USceneComponent* Component)
{
    PruneInvalidSelection();

    if (!IsValid(Component))
    {
        return;
    }

    // [버그 수정] 에디터 전용 컴포넌트(광원 아이콘 등)는 개별 조작 대상이 아니므로,
    // 부모 컴포넌트로 리다이렉트하여 함께 움직이도록 합니다.
    USceneComponent* Target = Component;
    if (Component->IsEditorOnlyComponent())
    {
        if (IsValid(Component->GetParent()))
        {
            Target = Component->GetParent();
        }
        else
        {
            AActor* ComponentOwner = Component->GetOwner();
            if (IsValid(ComponentOwner))
            {
                Target = ComponentOwner->GetRootComponent();
            }
        }
    }

    if (!IsValid(Target))
    {
        return;
    }

    if (SelectedComponent.Get() == Target && GetSelectedActorComponent() == Target)
    {
        return;
    }

    AActor* Owner = Target->GetOwner();
    if (!IsValid(Owner))
    {
        return;
    }

    if (!IsSelected(Owner))
    {
        Select(Owner);
    }

    // Select(Owner)는 actor root를 선택 대상으로 잡기 때문에, owner 선택 보장 후
    // 실제 component 선택 대상을 다시 설정합니다.
    SelectedComponent = Target;
    SetSingleDetailTarget(FSelectionDetailTarget::FromObject(Target));

    SyncGizmo();
}

void FSelectionManager::SelectActorDetails(AActor* Actor)
{
    Select(Actor);
}

void FSelectionManager::SelectActorComponent(UActorComponent* Component)
{
    PruneInvalidSelection();

    if (!IsValid(Component))
    {
        return;
    }

    if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
    {
        SelectComponent(SceneComponent);
        return;
    }

    AActor* Owner = Component->GetOwner();
    if (!IsValid(Owner))
    {
        return;
    }

    if (!IsSelected(Owner))
    {
        for (const TWeakObjectPtr<AActor>& PrevRef : SelectedActors)
        {
            if (AActor* Prev = PrevRef.Get())
            {
                SetActorProxiesSelected(Prev, false);
            }
        }

        SelectedActors.clear();
        SelectedActors.push_back(Owner);
        SetActorProxiesSelected(Owner, true);
    }

    SelectedComponent.Reset();
    SetSingleDetailTarget(FSelectionDetailTarget::FromObject(Component));

    SyncGizmo();
}

void FSelectionManager::SetGizmoEnabled(bool bEnabled)
{
    if (bGizmoEnabled == bEnabled)
    {
        return;
    }

    bGizmoEnabled = bEnabled;
    SyncGizmo();
}

void FSelectionManager::SetWorld(UWorld* InWorld)
{
    PruneInvalidSelection();

    // 기존 Scene에서 Gizmo 프록시 해제
    if (Gizmo)
        Gizmo->DestroyRenderState();
    if (Gizmo)
        Gizmo->SetScene(nullptr);

    World = IsValid(InWorld) ? InWorld : nullptr;

    // 새 Scene에 Gizmo 프록시 등록
    if (IsValid(Gizmo) && IsValid(World.Get()))
    {
        Gizmo->SetScene(&World.Get()->GetScene());
        Gizmo->CreateRenderState();
    }

    SyncGizmo();
}

void FSelectionManager::AddReferencedObjects(FReferenceCollector& Collector)
{
    // Selection targets/world are weak references. The editor-owned gizmo is the only UObject
    // whose lifetime is owned by the selection manager.
    Collector.AddReferencedObject(Gizmo);
}

void FSelectionManager::SetSingleDetailTarget(const FSelectionDetailTarget& Target)
{
    SelectedDetailTargets.clear();
    if (Target.IsValidTarget())
    {
        SelectedDetailTargets.push_back(Target);
    }
}

void FSelectionManager::AddActorDetailTarget(AActor* Actor)
{
    if (!IsValid(Actor))
    {
        return;
    }

    const bool bAlreadySelected = std::any_of(
        SelectedDetailTargets.begin(),
        SelectedDetailTargets.end(),
        [Actor](const FSelectionDetailTarget& Target)
        {
            return Target.ObjectPtr == Actor;
        });

    if (!bAlreadySelected)
    {
        SelectedDetailTargets.push_back(FSelectionDetailTarget::FromObject(Actor));
    }
}

void FSelectionManager::RefreshDetailTargetsFromActors()
{
    SelectedDetailTargets.clear();
    for (const TWeakObjectPtr<AActor>& ActorRef : SelectedActors)
    {
        AddActorDetailTarget(ActorRef.Get());
    }
}

void FSelectionManager::PruneInvalidSelection()
{
    bool bSelectionChanged = false;

    const size_t OldActorCount = SelectedActors.size();
    SelectedActors.erase(
        std::remove_if(
            SelectedActors.begin(),
            SelectedActors.end(),
            [](const TWeakObjectPtr<AActor>& ActorRef)
            {
                return ActorRef.Get() == nullptr;
            }
        ),
        SelectedActors.end()
    );
    bSelectionChanged = bSelectionChanged || OldActorCount != SelectedActors.size();

    const size_t OldDetailTargetCount = SelectedDetailTargets.size();
    SelectedDetailTargets.erase(
        std::remove_if(
            SelectedDetailTargets.begin(),
            SelectedDetailTargets.end(),
            [this](const FSelectionDetailTarget& Target)
            {
                if (!Target.IsValidTarget())
                {
                    return true;
                }
                if (UActorComponent* Component = Cast<UActorComponent>(Target.ObjectPtr))
                {
                    return !IsSelected(Component->GetOwner());
                }
                if (AActor* Actor = Cast<AActor>(Target.ObjectPtr))
                {
                    return !IsSelected(Actor);
                }
                return false;
            }),
        SelectedDetailTargets.end());
    bSelectionChanged = bSelectionChanged || OldDetailTargetCount != SelectedDetailTargets.size();

    if (SelectedComponent.GetAlive())
    {
        AActor* Owner = SelectedComponent.GetAlive()->GetOwner();
        if (!IsValid(SelectedComponent.Get()) || !IsValid(Owner))
        {
            SelectedComponent.Reset();
            bSelectionChanged = true;
        }
    }

    if (SelectedComponent.Get())
    {
        AActor* Owner = SelectedComponent.Get()->GetOwner();
        if (!IsValid(Owner) || !IsSelected(Owner))
        {
            SelectedComponent.Reset();
            bSelectionChanged = true;
        }
    }

    if (!SelectedComponent.Get() && !SelectedActors.empty())
    {
        for (const TWeakObjectPtr<AActor>& ActorRef : SelectedActors)
        {
            AActor* Actor = ActorRef.Get();
            if (!IsValid(Actor))
            {
                continue;
            }

            USceneComponent* Root = Actor->GetRootComponent();
            if (IsValid(Root))
            {
                SelectedComponent = Root;
                break;
            }
        }
    }

    if (SelectedDetailTargets.empty() && !SelectedActors.empty())
    {
        RefreshDetailTargetsFromActors();
        bSelectionChanged = true;
    }

    if (bSelectionChanged && IsValid(Gizmo))
    {
        RefreshSelectedActorCache();
        Gizmo->SetSelectedActors(SelectedActorCache.empty() ? nullptr : &SelectedActorCache);
    }
}

void FSelectionManager::RefreshSelectedActorCache()
{
    SelectedActorCache.clear();
    SelectedActorCache.reserve(SelectedActors.size());
    for (const TWeakObjectPtr<AActor>& ActorRef : SelectedActors)
    {
        if (AActor* Actor = ActorRef.Get())
        {
            SelectedActorCache.push_back(Actor);
        }
    }
}

void FSelectionManager::SyncGizmo()
{
    PruneInvalidSelection();

    if (!IsValid(Gizmo)) return;

    if (!bGizmoEnabled)
    {
        Gizmo->Deactivate();
        return;
    }

    USceneComponent* Primary = SelectedComponent.Get();
    if (IsValid(Primary))
    {
        RefreshSelectedActorCache();
        Gizmo->SetSelectedActors(SelectedActorCache.empty() ? nullptr : &SelectedActorCache);
        Gizmo->SetTarget(Primary);
    }
    else
    {
        Gizmo->SetSelectedActors(nullptr);
        Gizmo->Deactivate();
    }
}

void FSelectionManager::SetActorProxiesSelected(AActor* Actor, bool bSelected)
{
    if (!IsValid(Actor) || !IsValid(World.Get())) return;

    FScene& Scene = World->GetScene();
    for (UPrimitiveComponent* Prim : Actor->GetPrimitiveComponents())
    {
        if (!IsValid(Prim))
        {
            continue;
        }

        if (FPrimitiveSceneProxy* Proxy = Prim->GetSceneProxy())
        {
            if (Proxy->HasValidOwner())
            {
                Scene.SetProxySelected(Proxy, bSelected);
            }
        }
    }
}

