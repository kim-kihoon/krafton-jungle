#include "SkeletalMesh.h"

#include "Asset/Skeleton.h"
#include "Core/Logger.h"

DEFINE_CLASS(USkeletalMesh, UObject)

USkeletalMesh::~USkeletalMesh()
{
    if (MeshData != nullptr)
    {
        delete MeshData;
        MeshData = nullptr;
    }
}

void USkeletalMesh::SetMeshData(FSkeletalMesh* InMeshData)
{
    if (MeshData == InMeshData)
    {
        return;
    }

    delete MeshData;
    MeshData = InMeshData;

    CalculateInvRefMatrices();
    RebuildLocalBoundsFromMeshData();
}

void USkeletalMesh::SetSkeleton(USkeleton* InSkeleton)
{
    Skeleton = InSkeleton;
    CalculateInvRefMatrices();
}

FSkeletalMesh* USkeletalMesh::GetMeshData()
{
    return MeshData;
}

const FSkeletalMesh* USkeletalMesh::GetMeshData() const
{
    return MeshData;
}

USkeleton* USkeletalMesh::GetSkeleton()
{
    return Skeleton;
}

const USkeleton* USkeletalMesh::GetSkeleton() const
{
    return Skeleton;
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
    static FString Empty = {};
    return MeshData ? MeshData->PathFileName : Empty;
}

const FString& USkeletalMesh::GetSkeletonAssetPath() const
{
    static FString Empty = {};
    return MeshData ? MeshData->SkeletonAssetPath : Empty;
}

const TArray<FSkeletalMeshVertex>& USkeletalMesh::GetVertices() const
{
    static const TArray<FSkeletalMeshVertex> Empty = {};
    return MeshData ? MeshData->Vertices : Empty;
}

const TArray<uint32>& USkeletalMesh::GetIndices() const
{
    static const TArray<uint32> Empty = {};
    return MeshData ? MeshData->Indices : Empty;
}

const TArray<FSkeletalMeshSection>& USkeletalMesh::GetSections() const
{
    static const TArray<FSkeletalMeshSection> Empty = {};
    return MeshData ? MeshData->Sections : Empty;
}

const TArray<FSkeletalMeshMaterialSlot>& USkeletalMesh::GetMaterialSlots() const
{
    static const TArray<FSkeletalMeshMaterialSlot> Empty = {};
    return MeshData ? MeshData->MaterialSlots : Empty;
}

const TArray<FSkeletalBone>& USkeletalMesh::GetBones() const
{
    static const TArray<FSkeletalBone> Empty = {};
    if (Skeleton != nullptr)
    {
        return Skeleton->GetBones();
    }
    return MeshData ? MeshData->Bones : Empty;
}

const TArray<FMatrix>& USkeletalMesh::GetInverseBindPoseMatrices() const
{
    static const TArray<FMatrix> Empty = {};
    if (Skeleton != nullptr)
    {
        return Skeleton->GetInverseBindPoseMatrices();
    }
    return MeshData ? MeshData->InverseBindPoseMatrices : Empty;
}

const FAABB& USkeletalMesh::GetLocalBounds() const
{
    static const FAABB Empty = {};
    return MeshData ? MeshData->LocalBounds : Empty;
}

bool USkeletalMesh::HasValidMeshData() const
{
    return MeshData != nullptr && !MeshData->Vertices.empty() && !MeshData->Indices.empty();
}

bool USkeletalMesh::HasValidSkeleton() const
{
    if (Skeleton != nullptr)
    {
        return Skeleton->HasValidSkeleton();
    }
    return MeshData != nullptr && !MeshData->Bones.empty() && MeshData->Bones.size() == MeshData->InverseBindPoseMatrices.size();
}

void USkeletalMesh::CalculateInvRefMatrices()
{
    if (Skeleton != nullptr)
    {
        return;
    }

    if (MeshData == nullptr)
    {
        return;
    }

    const int32 BoneCount = static_cast<int32>(MeshData->Bones.size());

    if (BoneCount == 0)
    {
        return;
    }

    // FBX 로더에서 정밀한 바인드 포즈 역행렬(클러스터 기반)을 이미 채워두었다면 덮어쓰지 않고 넘어갑니다.
    if (MeshData->InverseBindPoseMatrices.size() == BoneCount)
    {
        return;
    }

    MeshData->InverseBindPoseMatrices.clear();
    MeshData->InverseBindPoseMatrices.resize(BoneCount, FMatrix::Identity);

    TArray<FTransform> ComponentSpaceRefPose;
    ComponentSpaceRefPose.resize(BoneCount, FTransform::Identity);

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const FSkeletalBone& Bone = MeshData->Bones[BoneIndex];
        const int32 ParentIndex = Bone.ParentIndex;

        if (ParentIndex >= 0 && ParentIndex < BoneIndex)
        {
            // 이 프로젝트의 FTransform 곱셈은 row-vector 기준으로
            // Local * ParentComponent 순서가 child component-space가 된다.
            ComponentSpaceRefPose[BoneIndex] =
                Bone.ReferenceLocalTransform * ComponentSpaceRefPose[ParentIndex];
        }
        else
        {
            ComponentSpaceRefPose[BoneIndex] = Bone.ReferenceLocalTransform;
        }

        const FMatrix RefMatrix = ComponentSpaceRefPose[BoneIndex].ToMatrixWithScale();
        MeshData->InverseBindPoseMatrices[BoneIndex] = RefMatrix.GetInverse();
    }
}

void USkeletalMesh::RebuildLocalBoundsFromMeshData()
{
    if (MeshData == nullptr)
    {
        return;
    }

    MeshData->LocalBounds.Reset();

    for (const FSkeletalMeshVertex& Vertex : MeshData->Vertices)
    {
        MeshData->LocalBounds.Expand(Vertex.Position);
    }
}
