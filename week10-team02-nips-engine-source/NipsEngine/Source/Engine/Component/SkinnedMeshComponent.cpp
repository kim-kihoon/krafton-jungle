#include "SkinnedMeshComponent.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <cmath>

#include "Asset/SkeletalMesh.h"
#include "Core/ResourceManager.h"
#include "Runtime/Engine.h"

DEFINE_CLASS(USkinnedMeshComponent, UMeshComponent)

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    if (SkeletalMesh == InSkeletalMesh)
    {
        return;
    }

    SkeletalMesh = InSkeletalMesh;

    // Mesh asset을 교체하면 section/material 구성, bone 개수, vertex/index 배열이 모두
    // 달라질 수 있다. 기존 mesh 기준으로 만든 material instance, CPU skinning cache,
    // GPU vertex/index buffer는 더 이상 유효하지 않으므로 전부 비운 뒤 새 mesh 기준으로 다시 초기화한다.
    ReleaseOwnedMaterialInstances();
    Materials.clear();

    if (SkeletalMesh != nullptr)
    {
        SkeletalMeshAssetPath = SkeletalMesh->GetAssetPathFileName();

        const TArray<FSkeletalMeshSection>& Sections = SkeletalMesh->GetSections();
        const TArray<FSkeletalMeshMaterialSlot>& Slots = SkeletalMesh->GetMaterialSlots();

        Materials.reserve(Sections.size());

        for (const FSkeletalMeshSection& Section : Sections)
        {
            UMaterialInterface* Material = nullptr;

            if (Section.MaterialSlotIndex >= 0 &&
                Section.MaterialSlotIndex < static_cast<int32>(Slots.size()))
            {
                Material = Slots[Section.MaterialSlotIndex].Material;
            }

            Materials.push_back(Material);
        }
    }
    else
    {
        SkeletalMeshAssetPath.clear();
    }

    ComponentSpaceBoneTransforms.clear();
    SkinningMatrices.clear();
    SkinnedVertices.clear();
    LocalBoneTransforms.clear();
    LocalSkinnedAABB.Reset();
    MeshBuffer.Release();

    if (SkeletalMesh != nullptr && SkeletalMesh->HasValidSkeleton())
    {
        // Reference pose는 "아무 animation도 적용하지 않은 기본 자세"이다.
        // component가 처음 mesh를 받았을 때는 이 자세를 현재 local pose로 복사한다.
        InitializePoseFromReference();
    }

    // 이후 Tick 또는 render collection 시점에 필요한 cache만 lazy하게 다시 계산한다.
    MarkBoneTransformsDirty();
    MarkSkinningDirty();
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

const TArray<FNormalVertex>& USkinnedMeshComponent::GetSkinnedVertices() const
{
    return SkinnedVertices;
}

const TArray<FMatrix>& USkinnedMeshComponent::GetSkinningMatrices() const
{
    return SkinningMatrices;
}

const TArray<FTransform>& USkinnedMeshComponent::GetComponentSpaceBoneTransforms() const
{
    return ComponentSpaceBoneTransforms;
}

void USkinnedMeshComponent::UpdateRenderBuffer()
{
    if (GEngine == nullptr)
    {
        return;
    }

    ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
    ID3D11DeviceContext* DeviceContext = GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
    if (Device == nullptr || DeviceContext == nullptr || !HasValidMesh())
    {
        MeshBuffer.Release();
        bRenderStateDirty = false;
        return;
    }

    ComputeSkinnedVertices();

    // 현재 구현은 CPU에서 스키닝한 vertex를 일반 mesh vertex buffer처럼 업로드한다.
    // 그래서 shader는 bone index/weight를 몰라도 되고, static mesh와 비슷한 렌더 경로를 쓸 수 있다.
    // 대신 bone pose가 바뀌면 vertex buffer 내용을 다시 갱신해야 한다.
    const TArray<FNormalVertex>& Vertices = GetSkinnedVertices();
    const TArray<uint32>& Indices = SkeletalMesh->GetIndices();
    if (Vertices.empty() || Indices.empty())
    {
        MeshBuffer.Release();
        bRenderStateDirty = false;
        return;
    }

    const uint32 VertexCount = static_cast<uint32>(Vertices.size());
    const bool bNeedCreate =
        !MeshBuffer.IsValid() ||
        MeshBuffer.GetVertexBuffer().GetVertexCount() < VertexCount;

    if (bNeedCreate)
    {
        MeshBuffer.Release();
        MeshBuffer.Create(Device, Vertices, Indices);
        bRenderStateDirty = false;
        return;
    }

    if (bRenderStateDirty)
    {
        MeshBuffer.Update(DeviceContext, Vertices);
        bRenderStateDirty = false;
    }
}

FMeshBuffer* USkinnedMeshComponent::GetRenderMeshBuffer()
{
    return MeshBuffer.IsValid() ? &MeshBuffer : nullptr;
}

const FMeshBuffer* USkinnedMeshComponent::GetRenderMeshBuffer() const
{
    return MeshBuffer.IsValid() ? &MeshBuffer : nullptr;
}

bool USkinnedMeshComponent::HasValidMesh() const
{
    return SkeletalMesh != nullptr && SkeletalMesh->HasValidMeshData();
}

void USkinnedMeshComponent::RefreshBoneTransforms()
{
    if (!HasValidMesh())
    {
        ComponentSpaceBoneTransforms.clear();
        bBoneTransformsDirty = false;
        MarkSkinningDirty();
        return;
    }

    // Skeletal mesh의 bone 배열은 parent가 child보다 앞에 오도록 구성되어 있다.
    // 이 전제가 있어야 한 번의 forward loop로 parent의 component-space transform을
    // 이미 계산된 값으로 참조할 수 있다.
    const TArray<FSkeletalBone>& Bones = SkeletalMesh->GetBones();
    const int32 BoneCount = static_cast<int32>(Bones.size());

    ComponentSpaceBoneTransforms.clear();
    ComponentSpaceBoneTransforms.resize(BoneCount, FTransform::Identity);

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const FSkeletalBone& Bone = Bones[BoneIndex];
        const int32 ParentIndex = Bone.ParentIndex;

        FTransform CurrentLocalTransform = Bone.ReferenceLocalTransform;
        if (BoneIndex < static_cast<int32>(LocalBoneTransforms.size()))
        {
            // LocalBoneTransforms는 현재 component instance의 pose이다.
            // 값이 있으면 reference pose 대신 현재 편집/animation 결과를 사용한다.
            CurrentLocalTransform = LocalBoneTransforms[BoneIndex];
        }

        if (ParentIndex >= 0 && ParentIndex < BoneIndex)
        {
            // Bone local transform은 parent 기준이므로 parent의 component-space transform을
            // 뒤에 곱해 component 기준 transform으로 올린다.
            // 결과적으로 root -> ... -> parent -> current 순서의 누적 transform이 된다.
            ComponentSpaceBoneTransforms[BoneIndex] = CurrentLocalTransform * ComponentSpaceBoneTransforms[ParentIndex];
        }
        else
        {
            // Root bone은 parent가 없으므로 local transform이 곧 component-space transform이다.
            // ParentIndex가 현재 bone보다 뒤에 있는 비정상 데이터도 안전하게 root처럼 처리한다.
            ComponentSpaceBoneTransforms[BoneIndex] = CurrentLocalTransform;
        }
    }

    bBoneTransformsDirty = false;
    MarkSkinningDirty();
}

void USkinnedMeshComponent::ComputeSkinningMatrices()
{
    if (!HasValidMesh())
    {
        SkinningMatrices.clear();
        return;
    }

    if (bBoneTransformsDirty)
    {
        RefreshBoneTransforms();
    }

    const TArray<FMatrix>& InverseBindPoseMatrices = SkeletalMesh->GetInverseBindPoseMatrices();
    const int32 BoneCount = static_cast<int32>(ComponentSpaceBoneTransforms.size());

    SkinningMatrices.clear();
    SkinningMatrices.resize(BoneCount, FMatrix::Identity);

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        // ComponentSpaceBoneTransforms[BoneIndex]는 현재 pose에서의 bone 위치이다.
        // 하지만 mesh vertex는 import 당시 bind pose 위치로 저장되어 있다.
        // 따라서 바로 CurrentBoneMatrix를 곱하면 bind pose 기준 오프셋이 중복 적용된다.
        const FMatrix CurrentBoneMatrix =
            ComponentSpaceBoneTransforms[BoneIndex].ToMatrixWithScale();

        if (BoneIndex < static_cast<int32>(InverseBindPoseMatrices.size()))
        {
            // InverseBindPose는 bind pose에서 bone 공간으로 되돌리는 행렬이다.
            // 그 뒤 CurrentBoneMatrix를 곱하면 같은 vertex를 현재 pose의 bone 위치로 보낸다.
            //
            // 이 엔진의 FVector/FMatrix helper는 아래 사용 방식처럼
            // Position * Matrix 순서의 의미로 쓰이고 있으므로 최종 행렬은:
            //   VertexBindPose * InverseBindPose * CurrentBoneMatrix
            // 가 되도록 InverseBindPose * CurrentBoneMatrix를 저장한다.
            SkinningMatrices[BoneIndex] = InverseBindPoseMatrices[BoneIndex] * CurrentBoneMatrix;
        }
        else
        {
            // Skeleton 데이터가 불완전한 경우의 fallback이다.
            // 올바른 skeletal mesh라면 inverse bind pose 수와 bone 수가 같아야 한다.
            SkinningMatrices[BoneIndex] = CurrentBoneMatrix;
        }
    }
}

void USkinnedMeshComponent::ComputeSkinnedVertices()
{
    if (!HasValidMesh())
    {
        SkinnedVertices.clear();
        LocalSkinnedAABB.Reset();
        bSkinningDirty = false;
        return;
    }

    if (!bSkinningDirty && SkinnedVertices.size() == SkeletalMesh->GetVertices().size())
    {
        return;
    }

    ComputeSkinningMatrices();

    const TArray<FSkeletalMeshVertex>& SourceVertices = SkeletalMesh->GetVertices();
    SkinnedVertices.clear();
    SkinnedVertices.resize(SourceVertices.size());
    LocalSkinnedAABB.Reset();

    for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(SourceVertices.size()); ++VertexIndex)
    {
        const FSkeletalMeshVertex& SourceVertex = SourceVertices[VertexIndex];

        // FSkeletalMeshVertex는 import된 원본 vertex이다.
        // Position/Normal/Tangent/Bitangent는 bind pose 기준이고,
        // BoneIndices/BoneWeights가 "어떤 bone들이 이 vertex를 얼마나 움직이는지"를 담는다.
        FNormalVertex OutVertex = SourceVertex.ToNormalVertex();

        // Linear Blend Skinning(LBS):
        // 각 influence마다 vertex를 해당 bone의 skinning matrix로 변환한 뒤,
        // weight만큼 더한다. 예를 들어 팔꿈치 주변 vertex는 upper_arm 0.4,
        // lower_arm 0.6처럼 두 bone의 결과가 섞여 부드럽게 접힌다.
        FVector SkinnedPosition = FVector::ZeroVector;
        FVector SkinnedNormal = FVector::ZeroVector;
        FVector SkinnedTangent = FVector::ZeroVector;
        FVector SkinnedBitangent = FVector::ZeroVector;

        float TotalWeight = 0.0f;

        for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_SKELETAL_BONE_INFLUENCES; ++InfluenceIndex)
        {
            const int32 BoneIndex = SourceVertex.BoneIndices[InfluenceIndex];
            const float Weight = SourceVertex.BoneWeights[InfluenceIndex];

            if (Weight <= 0.0f)
            {
                continue;
            }

            if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(SkinningMatrices.size()))
            {
                continue;
            }

            const FMatrix& SkinningMatrix = SkinningMatrices[BoneIndex];

            // 위치는 translation을 포함해야 하므로 TransformPosition을 사용한다.
            // 방향 벡터인 normal/tangent/bitangent는 위치 이동의 영향을 받으면 안 되므로
            // TransformVector로 회전/스케일 성분만 적용한다.
            SkinnedPosition += SkinningMatrix.TransformPosition(SourceVertex.Position) * Weight;
            SkinnedNormal += SkinningMatrix.TransformVector(SourceVertex.Normal) * Weight;
            SkinnedTangent += SkinningMatrix.TransformVector(SourceVertex.Tangent) * Weight;
            SkinnedBitangent += SkinningMatrix.TransformVector(SourceVertex.Bitangent) * Weight;

            TotalWeight += Weight;
        }

        if (TotalWeight > 1.0e-6f)
        {
            // Import 단계에서 weight 합이 1로 정규화되어 있어도,
            // invalid bone index를 건너뛰면 누적 weight가 1보다 작아질 수 있다.
            // 남은 영향만 기준으로 다시 나눠 vertex가 의도치 않게 원점 쪽으로 줄어드는 것을 막는다.
            const float InvTotalWeight = 1.0f / TotalWeight;

            OutVertex.Position = SkinnedPosition * InvTotalWeight;
            OutVertex.Normal = (SkinnedNormal * InvTotalWeight).GetSafeNormal();
            OutVertex.Tangent = (SkinnedTangent * InvTotalWeight).GetSafeNormal();
            OutVertex.Bitangent = (SkinnedBitangent * InvTotalWeight).GetSafeNormal();
        }
        else
        {
            // Bone weight가 없는 vertex는 skinned mesh 안의 rigid/static 부분일 수 있으므로 원본 그대로 둔다.
            OutVertex = SourceVertex.ToNormalVertex();
        }

        SkinnedVertices[VertexIndex] = OutVertex;
        // Skinned bounds는 현재 pose 기준이어야 한다.
        // 팔을 뻗거나 다리를 움직이면 reference pose bounds만으로는 culling/raycast가 틀릴 수 있다.
        LocalSkinnedAABB.Expand(OutVertex.Position);
    }

    bSkinningDirty = false;
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

void USkinnedMeshComponent::UpdateWorldAABB() const
{
    WorldAABB.Reset();

    if (!HasValidMesh())
    {
        bBoundsDirty = false;
        return;
    }

    if (bSkinningDirty || !LocalSkinnedAABB.IsValid())
    {
        // Bounds는 skinned vertex 결과에 의존한다.
        // const 함수지만 cache 갱신이므로 mutable dirty flag와 const_cast를 사용한다.
        const_cast<USkinnedMeshComponent*>(this)->ComputeSkinnedVertices();
    }

    if (!LocalSkinnedAABB.IsValid())
    {
        bBoundsDirty = false;
        return;
    }

    const FVector LocalCorners[8] = {
        FVector(LocalSkinnedAABB.Min.X, LocalSkinnedAABB.Min.Y, LocalSkinnedAABB.Min.Z),
        FVector(LocalSkinnedAABB.Max.X, LocalSkinnedAABB.Min.Y, LocalSkinnedAABB.Min.Z),
        FVector(LocalSkinnedAABB.Min.X, LocalSkinnedAABB.Max.Y, LocalSkinnedAABB.Min.Z),
        FVector(LocalSkinnedAABB.Max.X, LocalSkinnedAABB.Max.Y, LocalSkinnedAABB.Min.Z),
        FVector(LocalSkinnedAABB.Min.X, LocalSkinnedAABB.Min.Y, LocalSkinnedAABB.Max.Z),
        FVector(LocalSkinnedAABB.Max.X, LocalSkinnedAABB.Min.Y, LocalSkinnedAABB.Max.Z),
        FVector(LocalSkinnedAABB.Min.X, LocalSkinnedAABB.Max.Y, LocalSkinnedAABB.Max.Z),
        FVector(LocalSkinnedAABB.Max.X, LocalSkinnedAABB.Max.Y, LocalSkinnedAABB.Max.Z)
    };

    const FMatrix& WorldMatrix = GetWorldMatrix();

    // LocalSkinnedAABB는 component local space의 축정렬 박스이다.
    // Component가 회전/스케일되면 단순히 Min/Max만 변환할 수 없으므로,
    // 8개 corner를 모두 world로 보낸 뒤 world-space AABB를 다시 만든다.
    for (const FVector& Corner : LocalCorners)
    {
        WorldAABB.Expand(WorldMatrix.TransformPosition(Corner));
    }

    bBoundsDirty = false;
}

bool USkinnedMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
    if (!HasValidMesh())
    {
        return false;
    }

    EnsureBoundsUpdated();

    // 먼저 world-space AABB로 빠르게 탈락시킨다.
    // Triangle 전체를 검사하는 것은 비싸기 때문에 broad phase 역할을 한다.
    float BoxT = 0.0f;
    if (!WorldAABB.IntersectRay(Ray, BoxT))
    {
        return false;
    }

    ComputeSkinnedVertices();

    const TArray<uint32>& Indices = SkeletalMesh->GetIndices();

    if (SkinnedVertices.empty() || Indices.empty())
    {
        return false;
    }

    const FMatrix InvWorld = GetWorldMatrix().GetInverse();

    // SkinnedVertices는 component local space에 있으므로,
    // world ray를 component local space로 변환한 뒤 triangle intersection을 수행한다.
    FRay LocalRay = Ray;
    LocalRay.Origin = InvWorld.TransformPosition(LocalRay.Origin);
    LocalRay.Direction = InvWorld.TransformVector(LocalRay.Direction);
    LocalRay.Direction.NormalizeSafe();

    bool bHit = false;
    float ClosestT = FLT_MAX;
    int32 BestFaceIndex = -1;
    FVector BestLocalNormal = FVector::ZeroVector;

    for (uint32 i = 0; i + 2 < static_cast<uint32>(Indices.size()); i += 3)
    {
        const uint32 I0 = Indices[i];
        const uint32 I1 = Indices[i + 1];
        const uint32 I2 = Indices[i + 2];

        if (I0 >= SkinnedVertices.size() ||
            I1 >= SkinnedVertices.size() ||
            I2 >= SkinnedVertices.size())
        {
            continue;
        }

        const FVector& V0 = SkinnedVertices[I0].Position;
        const FVector& V1 = SkinnedVertices[I1].Position;
        const FVector& V2 = SkinnedVertices[I2].Position;

        float HitT = 0.0f;
        if (IntersectTriangle(LocalRay.Origin, LocalRay.Direction, V0, V1, V2, HitT))
        {
            if (HitT < ClosestT)
            {
                ClosestT = HitT;
                bHit = true;
                BestFaceIndex = static_cast<int32>(i / 3);

                const FVector Edge1 = V1 - V0;
                const FVector Edge2 = V2 - V0;
                BestLocalNormal = FVector::CrossProduct(Edge1, Edge2).GetSafeNormal();
            }
        }
    }

    if (!bHit)
    {
        return false;
    }

    const FVector LocalHitLocation = LocalRay.Origin + LocalRay.Direction * ClosestT;
    const FVector WorldHitLocation = GetWorldMatrix().TransformPosition(LocalHitLocation);

    FVector WorldNormal = GetWorldMatrix().TransformVector(BestLocalNormal);
    WorldNormal.NormalizeSafe();

    OutHitResult.bHit = true;
    OutHitResult.HitComponent = this;
    OutHitResult.Distance = (WorldHitLocation - Ray.Origin).Size();
    OutHitResult.Location = WorldHitLocation;
    OutHitResult.Normal = WorldNormal;
    OutHitResult.FaceIndex = BestFaceIndex;

    return true;
}

const FAABB& USkinnedMeshComponent::GetWorldAABB() const
{
    EnsureBoundsUpdated();
    return WorldAABB;
}

void USkinnedMeshComponent::InitializePoseFromReference()
{
    if (!HasValidMesh())
    {
        LocalBoneTransforms.clear();
        return;
    }

	const TArray<FSkeletalBone>& Bones = SkeletalMesh->GetBones();
    const int32 BoneCount = static_cast<int32>(Bones.size());

    LocalBoneTransforms.clear();
	LocalBoneTransforms.resize(BoneCount, FTransform::Identity);

    for (int32 i = 0; i < BoneCount; i++)
    {
        // ReferenceLocalTransform은 import 시점의 bind/reference pose이다.
        // 현재 animation system이 없을 때는 이 값을 그대로 현재 local pose로 사용한다.
		LocalBoneTransforms[i] = Bones[i].ReferenceLocalTransform;
    }

    MarkBoneTransformsDirty();
}

void USkinnedMeshComponent::ResetPose()
{
	InitializePoseFromReference();
}

FTransform USkinnedMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
    if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(LocalBoneTransforms.size()))
    {
		return LocalBoneTransforms[BoneIndex];
    }
	return FTransform::Identity;
}

void USkinnedMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& NewLocalTransform)
{
    if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(LocalBoneTransforms.size()))
    {
		LocalBoneTransforms[BoneIndex] = NewLocalTransform;
        // Local pose가 바뀌면 component-space pose, skinning matrix,
        // skinned vertex, bounds, render buffer가 모두 연쇄적으로 무효화된다.
        MarkBoneTransformsDirty();
	}
}

FTransform USkinnedMeshComponent::GetBoneComponentTransform(int32 BoneIndex) const
{
    if (bBoneTransformsDirty)
    {
        const_cast<USkinnedMeshComponent*>(this)->RefreshBoneTransforms();
    }

    if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(ComponentSpaceBoneTransforms.size()))
    {
		return ComponentSpaceBoneTransforms[BoneIndex];
    }
    return FTransform::Identity;
}

FTransform USkinnedMeshComponent::GetBoneWorldTransform(int32 BoneIndex) const
{
	FTransform ComponentSpaceTransform = GetBoneComponentTransform(BoneIndex);
	return ComponentSpaceTransform * GetWorldTransform();
}

void USkinnedMeshComponent::SetBoneWorldTransform(int32 BoneIndex, const FTransform& NewWorldTransform)
{
    if (!HasValidMesh() || LocalBoneTransforms.empty()) return;

    const TArray<FSkeletalBone>& Bones = SkeletalMesh->GetBones();
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size())) return;

    FTransform InvComponentTransform = GetWorldTransform().Inverse();
    FTransform TargetComponentSpace = NewWorldTransform * InvComponentTransform;

    const int32 ParentIndex = Bones[BoneIndex].ParentIndex;
    FTransform NewLocalTransform = TargetComponentSpace;

    if (ParentIndex >= 0 && ParentIndex < BoneIndex)
    {
        // 사용자는 gizmo로 world transform을 조작하지만 저장해야 하는 값은 parent 기준 local transform이다.
        // 그래서 target component-space transform에서 parent component-space transform을 제거한다.
        FTransform InvParentComponentSpace = GetBoneComponentTransform(ParentIndex).Inverse();
        NewLocalTransform = TargetComponentSpace * InvParentComponentSpace;
    }

    SetBoneLocalTransform(BoneIndex, NewLocalTransform);
}

void USkinnedMeshComponent::MarkBoneTransformsDirty()
{
    bBoneTransformsDirty = true;
    MarkSkinningDirty();
}

void USkinnedMeshComponent::MarkSkinningDirty()
{
    bSkinningDirty = true;
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

void USkinnedMeshComponent::MarkBoundsDirty()
{
    bBoundsDirty = true;
}

void USkinnedMeshComponent::MarkRenderStateDirty()
{
    bRenderStateDirty = true;
}

void USkinnedMeshComponent::EnsureBoundsUpdated() const
{
    if (!bBoundsDirty && !bTransformDirty)
    {
        return;
    }

    if (bTransformDirty)
    {
        (void)GetWorldMatrix();
        return;
    }

    const_cast<USkinnedMeshComponent*>(this)->UpdateWorldAABB();
}
