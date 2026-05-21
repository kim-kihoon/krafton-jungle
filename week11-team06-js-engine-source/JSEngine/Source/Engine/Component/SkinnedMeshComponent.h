#pragma once

#include "Asset/SkeletalMesh.h"
#include "Component/MeshComponent.h"
#include "Render/Resource/VertexTypes.h"
#include "Render/Skinning/SkinningRuntimeSettings.h"

#include "USkinnedMeshComponent.generated.h"
UCLASS()
class USkinnedMeshComponent : public UMeshComponent
{
public:
    GENERATED_BODY(USkinnedMeshComponent, UMeshComponent)
    USkinnedMeshComponent() = default;
    ~USkinnedMeshComponent() override = default;

    void Serialize(FArchive& Ar) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

    void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
    USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
    bool HasValidMesh() const { return SkeletalMesh != nullptr && SkeletalMesh->HasValidMeshData(); }

    const TArray<FMatrix>& GetCurrentLocalPose() const { return CurrentLocalPose; }
    const TArray<FMatrix>& GetCurrentGlobalPose() const { return CurrentGlobalPose; }
    const TArray<FMatrix>& GetSkinningMatrices() const { return SkinningMatrices; }
    const TArray<FSkeletalMeshVertex>& GetSkinnedVertices() const { return SkinnedVertices; }

    // 본 i의 월드 변환 (component-space pose × actor world). 인덱스가 범위 밖이면 컴포넌트 월드 행렬을 반환.
    // 본 자세 최신화는 GetSocketTransform과 동일 컨벤션 — 호출 측이 사전에 EnsureSkinningUpdated를 보장.
    FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;

    void MarkSkinningDirty();

    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;

	virtual const FAABB& GetWorldAABB() const;

    bool ConsumeRenderStateDirty();
    bool ConsumeGPUBoneBufferDirty();

    void EnsureSkinningUpdated();
    void EnsurePoseUpdated();
    void EnsureSkinningMatricesUpdated();
    void EnsureCPUSkinnedVerticesUpdated();
    void EnsureGPUSkinningResourcesDirty();
    ESkinningMode GetEffectiveSkinningMode() const;
    bool ShouldUseSkinCache() const { return bUseSkinCache; }
    void SetUseSkinCache(bool bInUseSkinCache);
    uint64 GetDeformationRevision() const { return DeformationRevision; }

    // Socket API override — mesh asset의 Sockets 정의를 사용.
    bool       HasSocket(const FName& SocketName) const override;
    FTransform GetSocketTransform(const FName& SocketName) const override;

protected:
    void InitializePoseFromBindPose();
    void UpdateCurrentGlobalPose();
    void UpdateSkinningMatrices();

	/**
	 * @brief CPU skinning 핵심 함수
	 */
    void SkinVerticesCPU();

    void MarkBoundsDirty() { bBoundsDirty = true; }
    void MarkRenderStateDirty() { bRenderStateDirty = true; }
    void EnsureBoundsUpdated() const;
    void SyncSkinningModeState() const;
    void NotifyPoseDependentsDirty();
    void AdvanceDeformationRevision() { ++DeformationRevision; }

protected:
    USkeletalMesh* SkeletalMesh = nullptr;
    FString SkeletalMeshPath;

    TArray<FMatrix> CurrentLocalPose;
    TArray<FMatrix> CurrentGlobalPose;
    TArray<FMatrix> SkinningMatrices;

    TArray<FSkeletalMeshVertex> SkinnedVertices;

    bool bEnableCPUSkinning = true;
    bool bUseSkinCache = false;
    bool bPoseDirty = true;
    bool bSkinningMatricesDirty = true;
    bool bCPUSkinnedVerticesDirty = true;
    bool bGPUBoneBufferDirty = true;

    mutable bool bBoundsDirty = true;
    bool bRenderStateDirty = true;
    mutable ESkinningMode CachedSkinningMode = ESkinningMode::CPU;
    mutable uint64 CachedSkinningModeRevision = static_cast<uint64>(-1);
    uint64 DeformationRevision = 1;
};
