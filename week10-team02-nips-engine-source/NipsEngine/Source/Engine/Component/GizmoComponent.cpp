#include "GizmoComponent.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/CylinderComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"
#include "Render/Mesh/MeshManager.h"
#include "Core/ResourceManager.h"

DEFINE_CLASS(UGizmoComponent, UPrimitiveComponent)
REGISTER_FACTORY(UGizmoComponent)

#include <cfloat>
#include <algorithm>
#include <cmath>

UGizmoComponent::UGizmoComponent()
{
    GizmoMeshData = &FEditorMeshLibrary::GetTranslationGizmo();

    // Gizmo 전용 Material 생성
    Material = FResourceManager::Get().GetOrCreateMaterial("GizmoMaterial", "Shaders/Gizmo.hlsl");
}

const FMeshData* UGizmoComponent::GetActiveMeshData() const
{
    return GizmoMeshData;
}

float UGizmoComponent::ApplySnapToDrag(float DragAmount)
{
    bool bSnapEnabled = false;
    float SnapValue = 0.0f;
    switch (CurMode)
    {
    case EGizmoMode::Translate:
        bSnapEnabled = bTranslateSnapEnabled;
        SnapValue = TranslateSnapValue;
        break;
    case EGizmoMode::Rotate:
        bSnapEnabled = bRotateSnapEnabled;
        SnapValue = RotateSnapValue;
        break;
    case EGizmoMode::Scale:
        bSnapEnabled = bScaleSnapEnabled;
        SnapValue = ScaleSnapValue;
        break;
    default:
        break;
    }

    if (!bSnapEnabled || SnapValue <= 0.0f)
    {
        SnapAccumulatedDrag = 0.0f;
        return DragAmount;
    }

    SnapAccumulatedDrag += DragAmount;
    const float StepCount = std::trunc(SnapAccumulatedDrag / SnapValue);
    if (StepCount == 0.0f)
    {
        return 0.0f;
    }

    const float SnappedDrag = StepCount * SnapValue;
    SnapAccumulatedDrag -= SnappedDrag;
    return SnappedDrag;
}

void UGizmoComponent::UpdateWorldAABB() const
{
    WorldAABB.Reset();

    const FMatrix& WorldMatrix = GetWorldMatrix();

    const float NewEx = std::abs(WorldMatrix.M[0][0]) * LocalExtents.X +
        std::abs(WorldMatrix.M[1][0]) * LocalExtents.Y +
        std::abs(WorldMatrix.M[2][0]) * LocalExtents.Z;

    const float NewEy = std::abs(WorldMatrix.M[0][1]) * LocalExtents.X +
        std::abs(WorldMatrix.M[1][1]) * LocalExtents.Y +
        std::abs(WorldMatrix.M[2][1]) * LocalExtents.Z;

    const float NewEz = std::abs(WorldMatrix.M[0][2]) * LocalExtents.X +
        std::abs(WorldMatrix.M[1][2]) * LocalExtents.Y +
        std::abs(WorldMatrix.M[2][2]) * LocalExtents.Z;

    const FVector WorldCenter = GetWorldLocation();
    WorldAABB.Expand(WorldCenter - FVector(NewEx, NewEy, NewEz));
    WorldAABB.Expand(WorldCenter + FVector(NewEx, NewEy, NewEz));
}

bool UGizmoComponent::IntersectRayAxis(const FRay& Ray, FVector AxisEnd, float& OutRayT)
{
    FVector AxisStart = GetWorldLocation();
    FVector RayOrigin = Ray.Origin;
    FVector RayDirection = Ray.Direction;

    FVector AxisVector = AxisEnd - AxisStart;
    FVector DiffOrigin = RayOrigin - AxisStart;

    float RayDirDotRayDir = RayDirection.X * RayDirection.X + RayDirection.Y * RayDirection.Y + RayDirection.Z * RayDirection.Z;
    float RayDirDotAxis = RayDirection.X * AxisVector.X + RayDirection.Y * AxisVector.Y + RayDirection.Z * AxisVector.Z;
    float AxisDotAxis = AxisVector.X * AxisVector.X + AxisVector.Y * AxisVector.Y + AxisVector.Z * AxisVector.Z;
    float RayDirDotDiff = RayDirection.X * DiffOrigin.X + RayDirection.Y * DiffOrigin.Y + RayDirection.Z * DiffOrigin.Z;
    float AxisDotDiff = AxisVector.X * DiffOrigin.X + AxisVector.Y * DiffOrigin.Y + AxisVector.Z * DiffOrigin.Z;

    float Denominator = (RayDirDotRayDir * AxisDotAxis) - (RayDirDotAxis * RayDirDotAxis);

    float RayT;
    float AxisS;

    if (Denominator < 1e-6f)
    {
        RayT = 0.0f;
        AxisS = (AxisDotAxis > 0.0f) ? (AxisDotDiff / AxisDotAxis) : 0.0f;
    }
    else
    {
        RayT = (RayDirDotAxis * AxisDotDiff - AxisDotAxis * RayDirDotDiff) / Denominator;
        AxisS = (RayDirDotRayDir * AxisDotDiff - RayDirDotAxis * RayDirDotDiff) / Denominator;
    }

    if (RayT < 0.0f) RayT = 0.0f;

    if (AxisS < 0.0f) AxisS = 0.0f;
    else if (AxisS > 1.0f) AxisS = 1.0f;

    FVector ClosestPointOnRay = RayOrigin + (RayDirection * RayT);
    FVector ClosestPointOnAxis = AxisStart + (AxisVector * AxisS);

    FVector DistanceVector = ClosestPointOnRay - ClosestPointOnAxis;
    float DistSq = (DistanceVector.X * DistanceVector.X) +
        (DistanceVector.Y * DistanceVector.Y) +
        (DistanceVector.Z * DistanceVector.Z);

    float ClickThresholdSquared = Radius * Radius;

    if (DistSq < ClickThresholdSquared)
    {
        OutRayT = RayT;
        return true;
    }

    return false;
}

void UGizmoComponent::HandleDrag(float DragAmount)
{
    DragAmount = ApplySnapToDrag(DragAmount);
    if (DragAmount == 0.0f)
    {
        return;
    }

    switch (CurMode)
    {
    case EGizmoMode::Translate: TranslateTarget(DragAmount); break;
    case EGizmoMode::Rotate:    RotateTarget(DragAmount); break;
    case EGizmoMode::Scale:     ScaleTarget(DragAmount); break;
    default: break;
    }

    UpdateGizmoTransform();
}

void UGizmoComponent::TranslateTarget(float DragAmount)
{
    if (!HasTarget() || !TransformTarget->SupportsTranslate()) return;

    FVector ConstrainedDelta = GetVectorForAxis(SelectedAxis) * DragAmount;

    FTransform T = TransformTarget->GetWorldTransform();
    T.SetLocation(T.GetTranslation() + ConstrainedDelta);
    TransformTarget->SetWorldTransform(T);
}

void UGizmoComponent::RotateTarget(float DragAmount)
{
    if (!HasTarget() || !TransformTarget->SupportsRotate()) return;

    FVector RotationAxis = GetVectorForAxis(SelectedAxis);
    FQuat DeltaQuat(RotationAxis, DragAmount);

    FTransform T = TransformTarget->GetWorldTransform();
    FQuat NewRot = T.GetRotation() * DeltaQuat;
    NewRot.Normalize();
    T.SetRotation(NewRot);

    TransformTarget->SetWorldTransform(T);
}

void UGizmoComponent::ScaleTarget(float DragAmount)
{
    if (!HasTarget() || !TransformTarget->SupportsScale()) return;

    float ScaleDelta = DragAmount * ScaleSensitivity;
    FTransform T = TransformTarget->GetWorldTransform();
    FVector NewScale = T.GetScale3D();

    switch (SelectedAxis)
    {
    case 0: NewScale.X += ScaleDelta; break;
    case 1: NewScale.Y += ScaleDelta; break;
    case 2: NewScale.Z += ScaleDelta; break;
    }

    NewScale.X = std::max(0.001f, NewScale.X);
    NewScale.Y = std::max(0.001f, NewScale.Y);
    NewScale.Z = std::max(0.001f, NewScale.Z);

    T.SetScale3D(NewScale);
    TransformTarget->SetWorldTransform(T);
}

void UGizmoComponent::SetTargetLocation(FVector NewLocation)
{
    if (!HasTarget()) return;
    FTransform T = TransformTarget->GetWorldTransform();
    T.SetLocation(NewLocation);
    TransformTarget->SetWorldTransform(T);
    UpdateGizmoTransform();
}

void UGizmoComponent::SetTargetRotation(FVector NewRotation)
{
    if (!HasTarget()) return;
    FTransform T = TransformTarget->GetWorldTransform();
    T.SetRotation(FQuat::MakeFromEuler(NewRotation));
    TransformTarget->SetWorldTransform(T);
    UpdateGizmoTransform();
}

void UGizmoComponent::SetTargetScale(FVector NewScale)
{
    if (!HasTarget()) return;
    FVector SafeScale = NewScale;
    SafeScale.X = std::max(0.001f, SafeScale.X);
    SafeScale.Y = std::max(0.001f, SafeScale.Y);
    SafeScale.Z = std::max(0.001f, SafeScale.Z);

    FTransform T = TransformTarget->GetWorldTransform();
    T.SetScale3D(SafeScale);
    TransformTarget->SetWorldTransform(T);
}

bool UGizmoComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
    const FMeshData* MeshData = GetActiveMeshData();
    if (!MeshData || MeshData->Indices.empty())
    {
        return false;
    }

    const FMatrix InvWorld = GetWorldMatrix().GetInverse();
    FVector LocalOrigin = InvWorld.TransformPosition(Ray.Origin);
    FVector LocalDirection = InvWorld.TransformVector(Ray.Direction);
    LocalDirection.NormalizeSafe();

    bool bHit = false;
    float ClosestT = FLT_MAX;

    for (size_t i = 0; i + 2 < MeshData->Indices.size(); i += 3)
    {
        const FVector& V0 = MeshData->Vertices[MeshData->Indices[i]].Position;
        const FVector& V1 = MeshData->Vertices[MeshData->Indices[i + 1]].Position;
        const FVector& V2 = MeshData->Vertices[MeshData->Indices[i + 2]].Position;

        float HitT = 0.0f;
        if (IntersectTriangle(LocalOrigin, LocalDirection, V0, V1, V2, HitT) && HitT < ClosestT)
        {
            ClosestT = HitT;
            bHit = true;
            OutHitResult.FaceIndex = static_cast<int32>(i);
        }
    }

    OutHitResult.bHit = bHit;
    if (!bHit)
    {
        UpdateHoveredAxis(-1);
        return false;
    }

    const FVector LocalHitPoint = LocalOrigin + (LocalDirection * ClosestT);
    const FVector WorldHitPoint = GetWorldMatrix().TransformPosition(LocalHitPoint);
    OutHitResult.Distance = FVector::Distance(Ray.Origin, WorldHitPoint);
    OutHitResult.Location = WorldHitPoint;
    OutHitResult.HitComponent = this;

    UpdateHoveredAxis(OutHitResult.FaceIndex);

    return OutHitResult.bHit;
}


FVector UGizmoComponent::GetVectorForAxis(int32 Axis)
{
    switch (Axis)
    {
    case 0:
        return GetForwardVector();
    case 1:
        return GetRightVector();
    case 2:
        return GetUpVector();
    default:
        return FVector(0.f, 0.f, 0.f);
    }
}

void UGizmoComponent::SetTransformTarget(std::unique_ptr<IGizmoTransformTarget> InTarget)
{
    DragEnd();
    TransformTarget = std::move(InTarget);

    if (HasTarget())
    {
        SetWorldLocation(GetTargetLocation());
        UpdateGizmoTransform();
		SetVisibility(true);
    }
    else
    {
        Deactivate();
    }
}

void UGizmoComponent::ClearTransformTarget()
{
    TransformTarget.reset();
    TrackingActor = nullptr;
    TrackingComponent = nullptr;
    TrackingActors = nullptr;
    TrackingBoneIndex = -1; 
    Deactivate();
}

USceneComponent* UGizmoComponent::GetEffectiveTargetComponent() const
{
    return TrackingComponent ? TrackingComponent : (TrackingActor ? TrackingActor->GetRootComponent() : nullptr);
}

FVector UGizmoComponent::GetTargetLocation() const
{
    return HasTarget() ? TransformTarget->GetPivotLocation() : FVector::ZeroVector;
}

FVector UGizmoComponent::GetTargetRotation() const
{
    return HasTarget() ? TransformTarget->GetWorldTransform().GetRotation().Euler() : FVector::ZeroVector;
}

void UGizmoComponent::SetTarget(AActor* NewTarget)
{
    if (!NewTarget || !NewTarget->GetRootComponent())
    {
        return;
    }

    if (TrackingActor == NewTarget && TrackingComponent == nullptr)
    {
        return;
    }

    ClearTransformTarget();
    TrackingActor = NewTarget;
    TrackingComponent = nullptr;

    SetTransformTarget(std::make_unique<FActorGizmoTarget>(NewTarget));
}

void UGizmoComponent::SetTargetComponent(USceneComponent* NewTargetComponent)
{
    if (!NewTargetComponent || !NewTargetComponent->GetOwner()) return;

    if (TrackingComponent == NewTargetComponent && TrackingBoneIndex == -1)
    {
        return;
    }

    ClearTransformTarget();
    TrackingActor = NewTargetComponent->GetOwner();
    TrackingComponent = NewTargetComponent;

    SetTransformTarget(std::make_unique<FSceneComponentGizmoTarget>(NewTargetComponent));
}

void UGizmoComponent::SetSelectedActors(const TArray<AActor*>* InSelectedActors)
{
    if (!InSelectedActors || InSelectedActors->empty())
    {
        TrackingActors = nullptr;
        return;
    }

    bool bIsSame = false;
    if (TrackingActors && TrackingActors->size() == InSelectedActors->size())
    {
        bIsSame = true;
        for (size_t i = 0; i < TrackingActors->size(); ++i)
        {
            if ((*TrackingActors)[i] != (*InSelectedActors)[i])
            {
                bIsSame = false;
                break;
            }
        }
    }

    if (bIsSame)
    {
        return;
    }

    ClearTransformTarget();

    TrackingActors = InSelectedActors;

    TrackingActor = (*InSelectedActors)[0];
    TrackingComponent = nullptr;

    SetTransformTarget(std::make_unique<FActorGizmoTarget>(TrackingActor));
}

void UGizmoComponent::SetTargetBone(USkeletalMeshComponent* MeshComponent, int32 BoneIndex)
{
    if (!MeshComponent || BoneIndex < 0) return;

    if (TrackingComponent == MeshComponent && TrackingBoneIndex == BoneIndex)
    {
        return;
    }

    ClearTransformTarget();
    TrackingComponent = MeshComponent;
    TrackingActor = MeshComponent->GetOwner();
    TrackingBoneIndex = BoneIndex;

    SetTransformTarget(std::make_unique<FBoneGizmoTarget>(MeshComponent, BoneIndex));
}

bool UGizmoComponent::BeginLinearDrag(const FRay& Ray)
{
    DragAxisVector = GetVectorForAxis(SelectedAxis).GetSafeNormal();
    if (DragAxisVector.IsNearlyZero())
    {
        return false;
    }

    FVector ViewDir = (GetWorldLocation() - Ray.Origin);
    ViewDir.NormalizeSafe();
    if (ViewDir.IsNearlyZero())
    {
        return false;
    }

    DragPlaneOrigin = GetWorldLocation();
    DragStartWorldTransform = TransformTarget->GetWorldTransform();

    FVector PlaneNormal = DragAxisVector.CrossProduct(ViewDir);

    // 시선과 기즈모 축이 완벽하게 일직선이 되어 외적 결과가 영벡터가 되는 특수 경우 예외 처리
    if (PlaneNormal.SizeSquared() < 1e-6f)
    {
        PlaneNormal = DragAxisVector.CrossProduct(FVector::UpVector);
        if (PlaneNormal.SizeSquared() < 1e-6f)
        {
            PlaneNormal = DragAxisVector.CrossProduct(FVector::RightVector);
        }
    }
    PlaneNormal.NormalizeSafe();

    DragProjectDir = PlaneNormal.CrossProduct(DragAxisVector).GetSafeNormal();
    if (DragProjectDir.IsNearlyZero())
    {
        return false;
    }

    return GetLinearDragIntersection(Ray, DragStartIntersectionLocation);
}

bool UGizmoComponent::GetLinearDragIntersection(const FRay& Ray, FVector& OutIntersection) const
{
    const float Denom = Ray.Direction.DotProduct(DragProjectDir);
    if (std::abs(Denom) < 1e-4f)
    {
        return false;
    }

    const float DistanceToPlane = (DragPlaneOrigin - Ray.Origin).DotProduct(DragProjectDir) / Denom;
    if (!std::isfinite(DistanceToPlane))
    {
        return false;
    }

    OutIntersection = Ray.Origin + (Ray.Direction * DistanceToPlane);
    return std::isfinite(OutIntersection.X) &&
        std::isfinite(OutIntersection.Y) &&
        std::isfinite(OutIntersection.Z);
}

float UGizmoComponent::ApplySnapToTotalDrag(float DragAmount) const
{
    bool bSnapEnabled = false;
    float SnapValue = 0.0f;
    switch (CurMode)
    {
    case EGizmoMode::Translate:
        bSnapEnabled = bTranslateSnapEnabled;
        SnapValue = TranslateSnapValue;
        break;
    case EGizmoMode::Scale:
        bSnapEnabled = bScaleSnapEnabled;
        SnapValue = ScaleSnapValue;
        break;
    default:
        break;
    }

    if (!bSnapEnabled || SnapValue <= 0.0f)
    {
        return DragAmount;
    }

    return std::trunc(DragAmount / SnapValue) * SnapValue;
}

void UGizmoComponent::ApplyLinearDragFromStart(float DragAmount)
{
    if (!HasTarget())
    {
        return;
    }

    FTransform NewTransform = DragStartWorldTransform;

    switch (CurMode)
    {
    case EGizmoMode::Translate:
        if (!TransformTarget->SupportsTranslate()) return;
        NewTransform.SetLocation(DragStartWorldTransform.GetTranslation() + DragAxisVector * DragAmount);
        break;
    case EGizmoMode::Scale:
    {
        if (!TransformTarget->SupportsScale()) return;

        FVector NewScale = DragStartWorldTransform.GetScale3D();
        const float ScaleDelta = DragAmount * ScaleSensitivity;
        switch (SelectedAxis)
        {
        case 0: NewScale.X += ScaleDelta; break;
        case 1: NewScale.Y += ScaleDelta; break;
        case 2: NewScale.Z += ScaleDelta; break;
        default: return;
        }

        NewScale.X = std::max(0.001f, NewScale.X);
        NewScale.Y = std::max(0.001f, NewScale.Y);
        NewScale.Z = std::max(0.001f, NewScale.Z);
        NewTransform.SetScale3D(NewScale);
        break;
    }
    default:
        return;
    }

    TransformTarget->SetWorldTransform(NewTransform);
    UpdateGizmoTransform();
}

void UGizmoComponent::UpdateLinearDrag(const FRay& Ray)
{
    if (bIsFirstFrameOfDrag)
    {
        if (BeginLinearDrag(Ray))
        {
            bIsFirstFrameOfDrag = false;
        }
        return;
    }

    FVector CurrentIntersectionLocation;
    if (!GetLinearDragIntersection(Ray, CurrentIntersectionLocation))
    {
        return;
    }

    const FVector FullDelta = CurrentIntersectionLocation - DragStartIntersectionLocation;
    float DragAmount = FullDelta.DotProduct(DragAxisVector);
    if (!std::isfinite(DragAmount))
    {
        return;
    }

    ApplyLinearDragFromStart(ApplySnapToTotalDrag(DragAmount));
}

void UGizmoComponent::UpdateAngularDrag(const FRay& Ray)
{
    FVector AxisVector = GetVectorForAxis(SelectedAxis);
    FVector PlaneNormal = AxisVector;

    float Denom = Ray.Direction.DotProduct(PlaneNormal);
    if (std::abs(Denom) < 1e-6f) return;

    float DistanceToPlane = (GetWorldLocation() - Ray.Origin).DotProduct(PlaneNormal) / Denom;
    FVector CurrentIntersectionLocation = Ray.Origin + (Ray.Direction * DistanceToPlane);

    if (bIsFirstFrameOfDrag)
    {
        LastIntersectionLocation = CurrentIntersectionLocation;
        bIsFirstFrameOfDrag = false;
        return;
    }

    FVector CenterToLast = (LastIntersectionLocation - GetWorldLocation()).Normalized();
    FVector CenterToCurrent = (CurrentIntersectionLocation - GetWorldLocation()).Normalized();

    float DotProduct = MathUtil::Clamp(CenterToLast.DotProduct(CenterToCurrent), -1.0f, 1.0f);
    float AngleRadians = std::acos(DotProduct);

    FVector CrossProduct = CenterToLast.CrossProduct(CenterToCurrent);
    float Sign = (CrossProduct.DotProduct(AxisVector) >= 0.0f) ? 1.0f : -1.0f;

    float DeltaAngle = Sign * AngleRadians;

    constexpr float AngularThreshold = 0.001f; // 약 0.05도

    // 역치를 넘었을 때만 이전 위치에서 갱신됩니다.
    if (std::abs(DeltaAngle) > AngularThreshold)
    {
        HandleDrag(DeltaAngle);
        LastIntersectionLocation = CurrentIntersectionLocation;
    }
}

void UGizmoComponent::UpdateHoveredAxis(int Index)
{
    if (IsHolding() || IsPressedOnHandle())
    {
        return;
    }

    // 조작 중이 아닐 때만 마우스 Raycast 결과에 따라 축을 갱신합니다.
    if (Index < 0)
    {
        SelectedAxis = -1;
    }
    else
    {
        const FMeshData* MeshData = GetActiveMeshData();
        if (!MeshData)
        {
            SelectedAxis = -1;
            return;
        }

        uint32 VertexIndex = MeshData->Indices[Index];
        SelectedAxis = MeshData->Vertices[VertexIndex].SubID;
    }
}

void UGizmoComponent::UpdateDrag(const FRay& Ray)
{
    if (IsHolding() == false || IsActive() == false) return;

    if (SelectedAxis == -1 || !HasTarget()) return;

    if (CurMode == EGizmoMode::Rotate) UpdateAngularDrag(Ray);
    else UpdateLinearDrag(Ray);
}

void UGizmoComponent::DragEnd()
{
    bIsFirstFrameOfDrag = true;
    DragPlaneOrigin = FVector::ZeroVector;
    DragStartIntersectionLocation = FVector::ZeroVector;
    DragAxisVector = FVector::ZeroVector;
    DragProjectDir = FVector::ZeroVector;
    DragStartWorldTransform = FTransform::Identity;
    SnapAccumulatedDrag = 0.0f;
    SetHolding(false);
    SetPressedOnHandle(false);
    SelectedAxis = -1;
}

void UGizmoComponent::SetNextMode()
{
    EGizmoMode NextMode = static_cast<EGizmoMode>((static_cast<int>(CurMode) + 1) % EGizmoMode::End);
    UpdateGizmoMode(NextMode);
}

void UGizmoComponent::UpdateGizmoMode(EGizmoMode NewMode)
{
    DragEnd();
    CurMode = NewMode;
    SnapAccumulatedDrag = 0.0f;
    UpdateGizmoTransform();
}

void UGizmoComponent::UpdateGizmoTransform()
{
    if (!HasTarget()) return;

    SetWorldLocation(GetTargetLocation());

    FVector TargetRot = GetTargetRotation();

    switch (CurMode)
    {
    case EGizmoMode::Scale:
        SetRelativeRotation(TargetRot);
        GizmoMeshData = &FEditorMeshLibrary::Get().GetScaleGizmo();
        break;

    case EGizmoMode::Rotate:
        SetRelativeRotation(bIsWorldSpace ? FVector() : TargetRot);
        GizmoMeshData = &FEditorMeshLibrary::Get().GetRotationGizmo();
        break;

    case EGizmoMode::Translate:
        SetRelativeRotation(bIsWorldSpace ? FVector() : TargetRot);
        GizmoMeshData = &FEditorMeshLibrary::Get().GetTranslationGizmo();
        break;
    }
}

void UGizmoComponent::ApplyScreenSpaceScaling(const FVector& CameraLocation)
{
    float Distance = FVector::Distance(CameraLocation, GetWorldLocation());

    float NewScale = Distance * 0.17f;

    if (NewScale < 0.01f) NewScale = 0.01f;

    SetRelativeScale(FVector(NewScale, NewScale, NewScale));
}

void UGizmoComponent::ApplyScreenSpaceScalingOrtho(float OrthoHeight)
{
    float NewScale = OrthoHeight * 0.15f;
    if (NewScale < 0.01f) NewScale = 0.01f;
    SetRelativeScale(FVector(NewScale, NewScale, NewScale));
}

void UGizmoComponent::SetWorldSpace(bool bWorldSpace)
{
    bIsWorldSpace = bWorldSpace;
    UpdateGizmoTransform();
}


void UGizmoComponent::Deactivate()
{
    SetVisibility(false);
    SelectedAxis = -1;
}

EPrimitiveType UGizmoComponent::GetPrimitiveType() const
{
    EPrimitiveType CurPrimitiveType = EPrimitiveType::EPT_TransGizmo;
    switch (CurMode)
    {
    case EGizmoMode::Translate:
        break;
    case EGizmoMode::Rotate:
        CurPrimitiveType = EPrimitiveType::EPT_RotGizmo;
        break;
    case EGizmoMode::Scale:
        CurPrimitiveType = EPrimitiveType::EPT_ScaleGizmo;
        break;
    }
    return CurPrimitiveType;
}
