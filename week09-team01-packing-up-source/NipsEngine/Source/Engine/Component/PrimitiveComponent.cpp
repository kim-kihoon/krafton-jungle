#include "PrimitiveComponent.h"
#include "Engine/Geometry/Ray.h"
#include "Core/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Utils.h"

#include <algorithm>

DEFINE_CLASS(UPrimitiveComponent, USceneComponent)

void UPrimitiveComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    USceneComponent::GetEditableProperties(OutProps);

    OutProps.push_back({"Visible", EPropertyType::Bool, &bIsVisible});
    OutProps.push_back({"Enable Cull", EPropertyType::Bool, &bEnableCull});
    static const char* DebugBoundsVisibilityNames[] = { "Use Global", "Force Show", "Force Hide" };
    OutProps.push_back({"Show Bounds", EPropertyType::Enum, &DebugBoundsVisibility, 0.0f, 0.0f, 0.0f, DebugBoundsVisibilityNames, 3});
    OutProps.push_back({"Generate Overlap Events", EPropertyType::Bool, &bGenerateOverlapEvents});
    OutProps.push_back({"Block Component", EPropertyType::Bool, &bBlockComponent});
}

void UPrimitiveComponent::PostEditProperty(const char* PropertyName)
{
    USceneComponent::PostEditProperty(PropertyName);
    DebugBoundsVisibility = std::clamp(DebugBoundsVisibility, 0, static_cast<int32>(EDebugDrawVisibility::Count) - 1);
    NotifySpatialIndexDirty();
}

void UPrimitiveComponent::Serialize(FArchive& Ar)
{
    USceneComponent::Serialize(Ar);
    Ar << "Visible" << bIsVisible;
    Ar << "EnableCull" << bEnableCull;
    Ar << "DebugBoundsVisibility" << DebugBoundsVisibility;
    Ar << "GenerateOverlapEvents" << bGenerateOverlapEvents;
    Ar << "BlockComponent" << bBlockComponent;

    if (Ar.IsLoading())
    {
        DebugBoundsVisibility = std::clamp(DebugBoundsVisibility, 0, static_cast<int32>(EDebugDrawVisibility::Count) - 1);
    }
}

void UPrimitiveComponent::SetVisibility(bool bVisible)
{
    if (bIsVisible == bVisible)
    {
        return;
    }

    bIsVisible = bVisible;
    NotifySpatialIndexDirty();
}

void UPrimitiveComponent::SetSceneQueryEnabled(bool bEnabled)
{
    if (bSceneQueryEnabled == bEnabled)
    {
        return;
    }

    bSceneQueryEnabled = bEnabled;

    if (!bSceneQueryEnabled)
    {
        if (bRegistered)
        {
            OnUnregister();
        }
        return;
    }

    if (!bRegistered)
    {
        OnRegister();
    }

    NotifySpatialIndexDirty();
}

bool UPrimitiveComponent::Raycast(const FRay& Ray, FHitResult& OutHitResult)
{
    if (!bIsVisible || !bSceneQueryEnabled || !IsRaycastTarget())
    {
        return false;
    }

    UpdateWorldAABB();

    float BoxT = 0.0f;
    if (!WorldAABB.IntersectRay(Ray, BoxT))
    {
        return false;
    }

    return RaycastMesh(Ray, OutHitResult);
}

bool UPrimitiveComponent::IntersectTriangle(const FVector& RayOrigin, const FVector& RayDir, const FVector& V0,
    const FVector& V1, const FVector& V2, float& OutT)
{
    const FVector Edge1 = V1 - V0;
    const FVector Edge2 = V2 - V0;

    const FVector PVec = FVector::CrossProduct(RayDir, Edge2);
    const float Det = FVector::DotProduct(Edge1, PVec);

    if (std::fabs(Det) < MathUtil::Epsilon)
    {
        return false;
    }

    const float InvDet = 1.0f / Det;
    const FVector TVec = RayOrigin - V0;

    const float U = FVector::DotProduct(TVec, PVec) * InvDet;
    if (U < 0.0f || U > 1.0f)
    {
        return false;
    }

    const FVector QVec = FVector::CrossProduct(TVec, Edge1);
    const float V = FVector::DotProduct(RayDir, QVec) * InvDet;
    if (V < 0.0f || (U + V) > 1.0f)
    {
        return false;
    }

    const float T = FVector::DotProduct(Edge2, QVec) * InvDet;
    if (T < 0.0f)
    {
        return false;
    }

    OutT = T;
    return true;
}

void UPrimitiveComponent::UpdateWorldMatrix() const
{
    USceneComponent::UpdateWorldMatrix();
    UpdateWorldAABB();
}

void UPrimitiveComponent::AddWorldOffset(const FVector& WorldDelta)
{
    USceneComponent::AddWorldOffset(WorldDelta);
    UpdateWorldAABB();
}

void UPrimitiveComponent::OnTransformDirty()
{
    NotifySpatialIndexDirty();
}

void UPrimitiveComponent::OnRegister()
{
    if (!Owner || bRegistered || !bSceneQueryEnabled) { return; }
    UWorld* World = Owner->GetFocusedWorld();
    if (!World) { return; }

    World->GetSpatialIndex().RegisterPrimitive(this);
    bRegistered = true;
}

void UPrimitiveComponent::OnUnregister()
{
    if (!Owner || !bRegistered) { return; }
    UWorld* World = Owner->GetFocusedWorld();
    if (!World) { return; }

    ClearCollisionReferences();
    World->GetSpatialIndex().UnregisterPrimitive(this);
    bRegistered = false;
}

void UPrimitiveComponent::NotifySpatialIndexDirty() const
{
    if (!bSceneQueryEnabled)
    {
        return;
    }

    AActor* Owner = GetOwner();
    if (Owner == nullptr)
    {
        return;
    }

    UWorld* World = Owner->GetFocusedWorld();
    if (World == nullptr)
    {
        return;
    }

    World->GetSpatialIndex().MarkPrimitiveDirty(const_cast<UPrimitiveComponent*>(this));
}

// UPrimitiveComponent가 삭제되기 전에 Actor들을 돌면서 컴포넌트를 삭제합니다.
void UPrimitiveComponent::ClearCollisionReferences()
{
    AActor* ThisOwner = GetOwner();
    UWorld* World = ThisOwner ? ThisOwner->GetFocusedWorld() : nullptr;
    if (World == nullptr)
    {
        OverlapInfos.clear();
        BlockingInfos.clear();
        return;
    }

    for (AActor* Actor : World->GetActors())
    {
        if (Actor == nullptr)
        {
            continue;
        }

        for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
        {
            if (Primitive == nullptr || Primitive == this)
            {
                continue;
            }

            if (Primitive->HasOverlapInfo(ThisOwner, this))
            {
                FOverlapResult EndOverlapInfo{ ThisOwner, this };
                Primitive->RemoveOverlapInfo(ThisOwner, this);
                Primitive->OnComponentEndOverlap.Broadcast(EndOverlapInfo);
            }

            if (Primitive->HasBlockingInfo(ThisOwner, this))
            {
                Primitive->RemoveBlockingInfo(ThisOwner, this);
            }
        }
    }

    OverlapInfos.clear();
    BlockingInfos.clear();
}

// 저장된 overlap 목록에 대상 Actor가 포함되어 있는지 확인한다.
bool UPrimitiveComponent::IsOverlappingActor(const AActor* Other) const
{
    for (const FOverlapResult& OverlapInfo : OverlapInfos)
    {
        if (OverlapInfo.OtherActor == Other)
        {
            return true;
        }

        if (OverlapInfo.OtherComp && OverlapInfo.OtherComp->GetOwner() == Other)
        {
            return true;
        }
    }

    return false;
}

// 저장된 Blcok 목록에 대상 Actor가 포함되어 있는지 확인한다.
bool UPrimitiveComponent::IsBlockingActor(const AActor* Other) const
{
    for (const FBlockingResult& BlockingInfo : BlockingInfos)
    {
        if (BlockingInfo.OtherActor == Other)
        {
            return true;
        }

        if (BlockingInfo.OtherComp && BlockingInfo.OtherComp->GetOwner() == Other)
        {
            return true;
        }
    }

    return false;
}

bool UPrimitiveComponent::HasOverlapInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp) const
{
    for (const FOverlapResult& Info : OverlapInfos)
    {
        if (Info.OtherActor == OtherActor && Info.OtherComp == OtherComp)
        {
            return true;
        }
    }

    return false;
}

// 같은 Actor/Component pair가 중복 저장되지 않도록 overlap 정보를 추가한다.
void UPrimitiveComponent::AddOverlapInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp)
{
    for (const FOverlapResult& OverlapInfo : OverlapInfos)
    {
        if (OverlapInfo.OtherActor == OtherActor && OverlapInfo.OtherComp == OtherComp)
        {
            return;
        }
    }

    FOverlapResult OverlapInfo;
    OverlapInfo.OtherActor = OtherActor;
    OverlapInfo.OtherComp = OtherComp;
    OverlapInfos.push_back(OverlapInfo);
}

// 지정한 Actor/Component 조건과 일치하는 overlap 정보를 제거한다.
void UPrimitiveComponent::RemoveOverlapInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp)
{
    const int32 OverlapCount = static_cast<int32>(OverlapInfos.size());
    for (int32 i = OverlapCount - 1; i >= 0; --i)
    {
        const auto& it = OverlapInfos[i];
        if (it.OtherActor == OtherActor && it.OtherComp == OtherComp)
        {
            std::swap(OverlapInfos[i], OverlapInfos.back());
            OverlapInfos.pop_back(); // swap-erase
        }
    }
}

bool UPrimitiveComponent::HasBlockingInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp) const
{
    for (const FBlockingResult& Info : BlockingInfos)
    {
        if (Info.OtherActor == OtherActor && Info.OtherComp == OtherComp)
        {
            return true;
        }
    }

    return false;
}

// 같은 Actor/Component pair가 중복 저장되지 않도록 blocking 정보를 추가한다.
void UPrimitiveComponent::AddBlockingInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp, const FHitResult& Hit)
{
    for (FBlockingResult& BlockingInfo : BlockingInfos)
    {
        if (BlockingInfo.OtherActor == OtherActor && BlockingInfo.OtherComp == OtherComp)
        {
            BlockingInfo.Hit = Hit;
            return;
        }
    }

    FBlockingResult BlockingInfo;
    BlockingInfo.OtherActor = OtherActor;
    BlockingInfo.OtherComp = OtherComp;
    BlockingInfo.Hit = Hit;
    BlockingInfos.push_back(BlockingInfo);
}

// 지정한 Actor/Component 조건과 일치하는 blocking 정보를 제거한다.
void UPrimitiveComponent::RemoveBlockingInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp)
{
    const int32 BlockingCount = static_cast<int32>(BlockingInfos.size());
    for (int32 i = BlockingCount - 1; i >= 0; --i)
    {
        const auto& it = BlockingInfos[i];
        if (it.OtherActor == OtherActor && it.OtherComp == OtherComp)
        {
            std::swap(BlockingInfos[i], BlockingInfos.back());
            BlockingInfos.pop_back(); // swap-erase
        }
    }
}
