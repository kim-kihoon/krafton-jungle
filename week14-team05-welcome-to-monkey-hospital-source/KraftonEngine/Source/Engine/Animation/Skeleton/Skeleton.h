#pragma once

#include "Object/Object.h"
#include "Animation/Skeleton/SkeletonTypes.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Animation/Skeleton/Skeleton.generated.h"

class UPhysicsAsset;

UCLASS()
class USkeleton : public UObject
{
public:
	GENERATED_BODY()
    USkeleton()           = default;
    ~USkeleton() override = default;

    void Serialize(FArchive& Ar) override;
    void SerializeLegacyPayload(FArchive& Ar);	// Socket 개념 추가 이전의 에셋 데이터 호환
    void SerializeCurrentPayload(FArchive& Ar);	// Socket 개념 추가된 최신 버전 에셋 직렬화
    // 수동 바이너리 포맷 — 반사 직렬화 비활성.
    bool ShouldReflectProperties() const override { return false; }

    const FString& GetAssetPathFileName() const
    {
        return AssetPathFileName;
    }

    void SetAssetPathFileName(const FString& InPath)
    {
        AssetPathFileName = InPath;
    }

    const FString& GetSkeletonAssetGuid() const
    {
        return SkeletonAssetGuid;
    }

    void SetSkeletonAssetGuid(const FString& InGuid)
    {
        SkeletonAssetGuid = InGuid;
    }

    const FString& GetCompatibilitySignature() const
    {
        return CompatibilitySignature;
    }

    void SetCompatibilitySignature(const FString& InSignature)
    {
        CompatibilitySignature = InSignature;
    }

    FSkeletonBinding GetSkeletonBinding() const
    {
        FSkeletonBinding Binding;
        Binding.SkeletonPath = AssetPathFileName;
        Binding.SkeletonAssetGuid = SkeletonAssetGuid;
        Binding.CompatibilitySignature = CompatibilitySignature;
        return Binding;
    }

    const FReferenceSkeleton& GetReferenceSkeleton() const
    {
        return ReferenceSkeleton;
    }

    FReferenceSkeleton& GetMutableReferenceSkeleton()
    {
        return ReferenceSkeleton;
    }

    const TArray<FSkeletalMeshSocket>& GetSockets() const
    {
        return Sockets;
    }

    TArray<FSkeletalMeshSocket>& GetMutableSockets()
    {
        return Sockets;
    }

    const FSkeletalMeshSocket* FindSocket(const FName& Name) const;
    bool HasSocket(const FName& Name) const;
    int32 FindSocketIndex(const FName& Name) const;
    bool ResolveSocketBoneIndex(const FSkeletalMeshSocket& Socket, int32& OutBoneIndex) const;

    void RebuildBoneNameCache();
    void RebuildReferenceSkeletonDerivedData();

    int32 FindBoneIndex(const FString& BoneName) const;

    // Skeleton-level fallback keeps shared ragdoll defaults close to the binding source.
    void SetDefaultPhysicsAsset(UPhysicsAsset* InPhysicsAsset);
    UPhysicsAsset* GetDefaultPhysicsAsset() const;
    void SetDefaultPhysicsAssetPath(const FString& InPath);
    const FString& GetDefaultPhysicsAssetPath() const { return DefaultPhysicsAssetPath; }
    bool ResolveDefaultPhysicsAsset();
    void ClearDefaultPhysicsAsset();

private:
    FString            AssetPathFileName = "None";
    FString            SkeletonAssetGuid;
    FString            CompatibilitySignature;
    FString            DefaultPhysicsAssetPath = "None";
    FReferenceSkeleton ReferenceSkeleton;
    TArray<FSkeletalMeshSocket> Sockets;
    TMap<FString, int32> BoneNameToIndex;
    mutable TWeakObjectPtr<UPhysicsAsset> DefaultPhysicsAsset;
};
