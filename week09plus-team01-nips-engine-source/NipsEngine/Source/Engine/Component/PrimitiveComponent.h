#pragma once

#include "Object/ObjectFactory.h"
#include "SceneComponent.h"
#include "Render/Common/RenderTypes.h"
#include "Render/Common/ViewTypes.h"
#include "Engine/Geometry/Ray.h"
#include "Core/CollisionTypes.h"
#include "Engine/Geometry/AABB.h"

enum ECollisionType
{
    None,
    Box,
    Sphere,
    Capsule,
    Cylinder
};

class UPrimitiveComponent : public USceneComponent
{
public:
    DECLARE_CLASS(UPrimitiveComponent, USceneComponent)

    /* For Property window */
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char * PropertyName) override;

    virtual void Serialize(FArchive& Ar) override;

    /* Visibility */
    void SetVisibility(bool bVisible);
    bool IsVisible() const { return bIsVisible; }

    void SetSceneQueryEnabled(bool bEnabled);
    bool IsSceneQueryEnabled() const { return bSceneQueryEnabled; }

    void SetEnableCull(const bool bInEnableCull) { bEnableCull = bInEnableCull; }
    bool IsEnableCull() const { return bEnableCull; }
    bool ShouldDrawDebugBounds(bool bGlobalEnabled) const { return ResolveDebugDrawVisibility(DebugBoundsVisibility, bGlobalEnabled); }

    /* Getter */
    virtual const FAABB& GetWorldAABB() const 
    { 
        UpdateWorldAABB();
        return WorldAABB;
    }

    /* For Collision(Ray-casting) */
    virtual void UpdateWorldAABB() const = 0;
    virtual bool IsRaycastTarget() const { return true; }
    bool Raycast(const FRay& Ray, FHitResult& OutHitResult);
    bool IntersectTriangle(const FVector& RayOrigin, const FVector& RayDir, const FVector& V0, const FVector& V1,
                           const FVector& V2, float& OutT);
    virtual bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) = 0;

    /* For Transform */
    void UpdateWorldMatrix() const override;
    void AddWorldOffset(const FVector& WorldDelta) override;
    virtual EPrimitiveType GetPrimitiveType() const = 0;

    /* For Material */
    virtual int32 GetNumMaterials() const { return 0; }
    virtual class UMaterialInterface* GetMaterial(int32 SlotIndex) const { return nullptr; }
    virtual void SetMaterial(int32 SlotIndex, class UMaterialInterface* InMaterial) {}

    virtual bool SupportsOutline() const { return true; }

    /* for BVH Query */
    virtual void OnRegister() override;
    virtual void OnUnregister() override;

    /* For Collision */
    virtual ECollisionType GetCollisionType() const { return CollisionType; }

    bool IsGenerateOverlapEvents() const { return bGenerateOverlapEvents; }
    bool IsBlockComponent() const { return bBlockComponent; }
    void SetGenerateOverlapEvents(bool bInGenerateOverlapEvents) { bGenerateOverlapEvents = bInGenerateOverlapEvents; }
    void SetBlockComponent(bool bInBlockComponent) { bBlockComponent = bInBlockComponent; }
    bool IsOverlappingActor(const AActor* Other) const;
    bool IsBlockingActor(const AActor* Other) const;

    const TArray<FOverlapResult>& GetOverlapInfos() const { return OverlapInfos; }
    bool HasOverlapInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp) const;
    void AddOverlapInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp);
    void RemoveOverlapInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp);
    
    const TArray<FBlockingResult>& GetBlockingInfos() const { return BlockingInfos; }
    bool HasBlockingInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp) const;
    void AddBlockingInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp, const FHitResult& Hit);
    void RemoveBlockingInfo(AActor* OtherActor, UPrimitiveComponent* OtherComp);

    FComponentHitSignature OnComponentHit;
    FComponentBeginOverlapSignature OnComponentBeginOverlap;
    FComponentEndOverlapSignature OnComponentEndOverlap;

protected:
    void OnTransformDirty() override;
    void NotifySpatialIndexDirty() const;
    void ClearCollisionReferences();

protected:
    mutable FAABB WorldAABB;
    bool bIsVisible = true;
    bool bSceneQueryEnabled = true;
    bool bEnableCull = true; // frustum, occlusion culling으로 컬링될지 여부 판정
    int32 DebugBoundsVisibility = static_cast<int32>(EDebugDrawVisibility::UseGlobal);
    
    ECollisionType CollisionType = ECollisionType::None;

    bool bGenerateOverlapEvents = false; // 켜지면 Overlap 이벤트를 발생시킵니다.
    bool bBlockComponent = false;        // 켜지면 다른 컴포넌트를 밀어냅니다.
    TArray<FOverlapResult> OverlapInfos;
    TArray<FBlockingResult> BlockingInfos;
};
