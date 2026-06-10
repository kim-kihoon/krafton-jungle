#include "SocketTransformGizmoTarget.h"

#include "Animation/Skeleton/Skeleton.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/SceneComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    constexpr float SocketMatrixDecomposeTolerance = 1.0e-6f;

    FTransform DecomposeSocketMatrix(const FMatrix& Matrix)
    {
        FTransform Result;
        Result.Location = Matrix.GetLocation();
        Result.Scale = Matrix.GetScale();

        FMatrix RotationMatrix = Matrix;
        RotationMatrix.M[3][0] = 0.0f;
        RotationMatrix.M[3][1] = 0.0f;
        RotationMatrix.M[3][2] = 0.0f;
        RotationMatrix.M[3][3] = 1.0f;

        if (std::fabs(Result.Scale.X) > SocketMatrixDecomposeTolerance)
        {
            RotationMatrix.M[0][0] /= Result.Scale.X;
            RotationMatrix.M[0][1] /= Result.Scale.X;
            RotationMatrix.M[0][2] /= Result.Scale.X;
        }
        if (std::fabs(Result.Scale.Y) > SocketMatrixDecomposeTolerance)
        {
            RotationMatrix.M[1][0] /= Result.Scale.Y;
            RotationMatrix.M[1][1] /= Result.Scale.Y;
            RotationMatrix.M[1][2] /= Result.Scale.Y;
        }
        if (std::fabs(Result.Scale.Z) > SocketMatrixDecomposeTolerance)
        {
            RotationMatrix.M[2][0] /= Result.Scale.Z;
            RotationMatrix.M[2][1] /= Result.Scale.Z;
            RotationMatrix.M[2][2] /= Result.Scale.Z;
        }

        Result.Rotation = RotationMatrix.ToQuat().GetNormalized();
        return Result;
    }

    FMatrix GetAffineInverseForSocketEdit(const FMatrix& Matrix)
    {
        const double A = Matrix.M[0][0];
        const double B = Matrix.M[0][1];
        const double C = Matrix.M[0][2];
        const double D = Matrix.M[1][0];
        const double E = Matrix.M[1][1];
        const double F = Matrix.M[1][2];
        const double G = Matrix.M[2][0];
        const double H = Matrix.M[2][1];
        const double I = Matrix.M[2][2];

        const double Det = A * (E * I - F * H) - B * (D * I - F * G) + C * (D * H - E * G);
        if (std::fabs(Det) < 1.0e-12)
        {
            return Matrix.GetInverse();
        }

        const double InvDet = 1.0 / Det;

        FMatrix Result = FMatrix::Identity;
        Result.M[0][0] = static_cast<float>((E * I - F * H) * InvDet);
        Result.M[0][1] = static_cast<float>((C * H - B * I) * InvDet);
        Result.M[0][2] = static_cast<float>((B * F - C * E) * InvDet);
        Result.M[1][0] = static_cast<float>((F * G - D * I) * InvDet);
        Result.M[1][1] = static_cast<float>((A * I - C * G) * InvDet);
        Result.M[1][2] = static_cast<float>((C * D - A * F) * InvDet);
        Result.M[2][0] = static_cast<float>((D * H - E * G) * InvDet);
        Result.M[2][1] = static_cast<float>((B * G - A * H) * InvDet);
        Result.M[2][2] = static_cast<float>((A * E - B * D) * InvDet);

        const FVector Translation = Matrix.GetLocation();
        Result.M[3][0] = -(Translation.X * Result.M[0][0] + Translation.Y * Result.M[1][0] + Translation.Z * Result.M[2][0]);
        Result.M[3][1] = -(Translation.X * Result.M[0][1] + Translation.Y * Result.M[1][1] + Translation.Z * Result.M[2][1]);
        Result.M[3][2] = -(Translation.X * Result.M[0][2] + Translation.Y * Result.M[1][2] + Translation.Z * Result.M[2][2]);
        return Result;
    }

    FVector ClampSocketScale(const FVector& Scale)
    {
        return FVector(
            std::max(0.001f, Scale.X),
            std::max(0.001f, Scale.Y),
            std::max(0.001f, Scale.Z));
    }

    float ClosestAngleEquivalent(float NewAngle, float PreviousAngle)
    {
        float Best = NewAngle;
        float BestDelta = std::fabs(Best - PreviousAngle);

        for (int32 Offset = -1; Offset <= 1; ++Offset)
        {
            const float Candidate = NewAngle + 360.0f * static_cast<float>(Offset);
            const float CandidateDelta = std::fabs(Candidate - PreviousAngle);
            if (CandidateDelta < BestDelta)
            {
                Best = Candidate;
                BestDelta = CandidateDelta;
            }
        }

        return Best;
    }

    FRotator MakeClosestEquivalentRotator(const FRotator& NewRotator, const FRotator& PreviousRotator)
    {
        auto Score = [](const FRotator& A, const FRotator& B)
        {
            const float PitchDelta = A.Pitch - B.Pitch;
            const float YawDelta = A.Yaw - B.Yaw;
            const float RollDelta = A.Roll - B.Roll;
            return PitchDelta * PitchDelta + YawDelta * YawDelta + RollDelta * RollDelta;
        };

        auto ClosestByAxis = [](const FRotator& Candidate, const FRotator& Previous)
        {
            return FRotator(
                ClosestAngleEquivalent(Candidate.Pitch, Previous.Pitch),
                ClosestAngleEquivalent(Candidate.Yaw, Previous.Yaw),
                ClosestAngleEquivalent(Candidate.Roll, Previous.Roll));
        };

        const FRotator Direct = ClosestByAxis(NewRotator, PreviousRotator);
        const FRotator GimbalEquivalent = ClosestByAxis(
            FRotator(180.0f - NewRotator.Pitch, NewRotator.Yaw + 180.0f, NewRotator.Roll + 180.0f),
            PreviousRotator);

        return Score(GimbalEquivalent, PreviousRotator) < Score(Direct, PreviousRotator)
            ? GimbalEquivalent
            : Direct;
    }
}

FSocketTransformGizmoTarget::FSocketTransformGizmoTarget()
    : MeshComponent(nullptr), SocketName(FName::None)
{
}

FSocketTransformGizmoTarget::FSocketTransformGizmoTarget(USkeletalMeshComponent* InMeshComp, const FName& InSocketName)
    : MeshComponent(InMeshComp), SocketName(InSocketName)
{
}

void FSocketTransformGizmoTarget::SetSocket(USkeletalMeshComponent* InMeshComp, const FName& InSocketName)
{
    MeshComponent = InMeshComp;
    SocketName = InSocketName;
}

void FSocketTransformGizmoTarget::SetOnSocketChanged(std::function<void(const FName&)> InCallback)
{
    OnSocketChanged = std::move(InCallback);
}

bool FSocketTransformGizmoTarget::IsValid() const
{
    return MeshComponent.IsValid() && SocketName != FName::None && GetMutableSocket() != nullptr;
}

UWorld* FSocketTransformGizmoTarget::GetWorld() const
{
    USkeletalMeshComponent* Comp = MeshComponent.Get();
    return Comp ? Comp->GetWorld() : nullptr;
}

FVector FSocketTransformGizmoTarget::GetWorldLocation() const
{
    return GetWorldTransform().Location;
}

FRotator FSocketTransformGizmoTarget::GetWorldRotation() const
{
    return GetWorldTransform().Rotation.ToRotator();
}

FQuat FSocketTransformGizmoTarget::GetWorldQuat() const
{
    return GetWorldTransform().Rotation;
}

FVector FSocketTransformGizmoTarget::GetWorldScale() const
{
    return GetWorldTransform().Scale;
}

void FSocketTransformGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
    FTransform Desired = GetWorldTransform();
    Desired.Location = NewLocation;
    ApplyWorldTransform(Desired);
}

void FSocketTransformGizmoTarget::SetWorldRotation(const FRotator& NewRotation)
{
    FTransform Desired = GetWorldTransform();
    Desired.Rotation = NewRotation.ToQuaternion().GetNormalized();
    ApplyWorldTransform(Desired);
}

void FSocketTransformGizmoTarget::SetWorldRotation(const FQuat& NewQuat)
{
    FTransform Desired = GetWorldTransform();
    Desired.Rotation = NewQuat.GetNormalized();
    ApplyWorldTransform(Desired);
}

void FSocketTransformGizmoTarget::SetWorldScale(const FVector& NewScale)
{
    FTransform Desired = GetWorldTransform();
    Desired.Scale = ClampSocketScale(NewScale);
    ApplyWorldTransform(Desired);
}

void FSocketTransformGizmoTarget::AddWorldOffset(const FVector& Delta)
{
    FTransform Desired = GetWorldTransform();
    Desired.Location += Delta;
    ApplyWorldTransform(Desired);
}

void FSocketTransformGizmoTarget::AddWorldRotation(const FQuat& Delta, bool bWorldSpace)
{
    const FQuat NormalizedDelta = Delta.GetNormalized();
    FSkeletonSocket* Socket = GetMutableSocket();
    if (!Socket)
    {
        return;
    }

    FQuat NewLocalQuat = Socket->RelativeRotation.ToQuaternion().GetNormalized();
    if (bWorldSpace)
    {
        const int32 BoneIndex = ResolveSocketBoneIndex(*Socket);
        if (BoneIndex < 0)
        {
            return;
        }

        const FQuat BoneWorldQuat = DecomposeSocketMatrix(GetBoneWorldMatrix(BoneIndex)).Rotation;
        const FQuat CurrentWorldQuat = (NewLocalQuat * BoneWorldQuat).GetNormalized();
        const FQuat NewWorldQuat = (NormalizedDelta * CurrentWorldQuat).GetNormalized();
        NewLocalQuat = (NewWorldQuat * BoneWorldQuat.Inverse()).GetNormalized();
    }
    else
    {
        NewLocalQuat = (NewLocalQuat * NormalizedDelta).GetNormalized();
    }

    Socket->RelativeRotation = MakeClosestEquivalentRotator(NewLocalQuat.ToRotator(), Socket->RelativeRotation);
    NotifySocketChanged();
}

void FSocketTransformGizmoTarget::AddScaleDelta(const FVector& Delta)
{
    FTransform Desired = GetWorldTransform();
    Desired.Scale = ClampSocketScale(Desired.Scale + Delta);
    ApplyWorldTransform(Desired);
}

FSkeletonSocket* FSocketTransformGizmoTarget::GetMutableSocket() const
{
    USkeletalMeshComponent* Comp = MeshComponent.Get();
    USkeletalMesh* Mesh = Comp ? Comp->GetSkeletalMesh() : nullptr;
    USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
    return Skeleton ? Skeleton->FindSocket(SocketName) : nullptr;
}

int32 FSocketTransformGizmoTarget::ResolveSocketBoneIndex(const FSkeletonSocket& Socket) const
{
    USkeletalMeshComponent* Comp = MeshComponent.Get();
    USkeletalMesh* Mesh = Comp ? Comp->GetSkeletalMesh() : nullptr;
    USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
    return Skeleton ? Skeleton->ResolveSocketBoneIndex(Socket) : -1;
}

FMatrix FSocketTransformGizmoTarget::GetBoneWorldMatrix(int32 BoneIndex) const
{
    USkeletalMeshComponent* Comp = MeshComponent.Get();
    if (!Comp || BoneIndex < 0)
    {
        return FMatrix::Identity;
    }

    TArray<FMatrix> GlobalMatrices;
    Comp->GetCurrentBoneGlobalMatrices(GlobalMatrices);
    if (BoneIndex >= static_cast<int32>(GlobalMatrices.size()))
    {
        return FMatrix::Identity;
    }

    return GlobalMatrices[BoneIndex] * Comp->GetWorldMatrix();
}

FTransform FSocketTransformGizmoTarget::GetWorldTransform() const
{
    USkeletalMeshComponent* Comp = MeshComponent.Get();
    FSkeletonSocket* Socket = GetMutableSocket();
    if (!Comp || !Socket)
    {
        return FTransform();
    }

    const int32 BoneIndex = ResolveSocketBoneIndex(*Socket);
    if (BoneIndex < 0)
    {
        return FTransform(Comp->GetWorldMatrix());
    }

    const FMatrix SocketWorldMatrix = Socket->GetRelativeTransform() * GetBoneWorldMatrix(BoneIndex);
    return DecomposeSocketMatrix(SocketWorldMatrix);
}

void FSocketTransformGizmoTarget::ApplyWorldTransform(const FTransform& DesiredWorldTransform)
{
    FSkeletonSocket* Socket = GetMutableSocket();
    if (!Socket)
    {
        return;
    }

    const int32 BoneIndex = ResolveSocketBoneIndex(*Socket);
    if (BoneIndex < 0)
    {
        return;
    }

    const FMatrix BoneWorldMatrix = GetBoneWorldMatrix(BoneIndex);
    const FMatrix SocketLocalMatrix = DesiredWorldTransform.ToMatrix() * GetAffineInverseForSocketEdit(BoneWorldMatrix);
    FTransform LocalTransform = DecomposeSocketMatrix(SocketLocalMatrix);
    LocalTransform.Scale = ClampSocketScale(LocalTransform.Scale);

    Socket->RelativeLocation = LocalTransform.Location;
    Socket->RelativeRotation = MakeClosestEquivalentRotator(LocalTransform.Rotation.ToRotator(), Socket->RelativeRotation);
    Socket->RelativeScale = LocalTransform.Scale;

    NotifySocketChanged();
}

void FSocketTransformGizmoTarget::NotifySocketChanged() const
{
    if (USkeletalMeshComponent* Comp = MeshComponent.Get())
    {
        for (USceneComponent* Child : Comp->GetChildren())
        {
            if (Child && Child->GetAttachSocketName() == SocketName)
            {
                Child->MarkTransformDirty();
            }
        }
    }

    if (OnSocketChanged)
    {
        OnSocketChanged(SocketName);
    }
}
