#pragma once

#include "Asset/IAssetLoader.h"
#include "Asset/AnimationTypes.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Asset/StaticMeshTypes.h"
#include "Core/ResourceTypes.h"
#include <fbxsdk.h>

enum class ESkeletalMeshImportPass
{
    SkinnedMeshes,
    RigidAttachedMeshes
};

struct FFbxMeshContentInfo
{
    bool bHasStaticMesh = false;
    bool bHasSkeletalMesh = false;
};

struct FFbxSkeletonImportDesc
{
    FString RootNodeName;
    TArray<FString> BoneNodeNames;
};

struct FFbxSkeletalMeshImportDesc
{
    FString MeshNodeName;
    FString SkeletonRootNodeName;
    int32 MaterialSlotCount = 0;
    bool bSkinned = false;
    bool bRigidAttached = false;
};

struct FFbxAnimationImportDesc
{
    FString SequenceName;
    FString TargetSkeletonRootNodeName;
    int32 StackIndex = -1;
};

struct FFbxImportedAssetSet
{
    FString SourcePath;
    FFbxMeshContentInfo MeshContentInfo;
    TArray<FFbxSkeletonImportDesc> Skeletons;
    TArray<FFbxSkeletalMeshImportDesc> SkeletalMeshes;
    TArray<FFbxAnimationImportDesc> Animations;
};

struct FAnimationImportOptions
{
    FString SkeletonSourcePath;
    bool bImportAllStacks = true;
    bool bImportBoneTransforms = true;
    bool bImportShapeKeys = true;
};

class FFbxImporter : public IAssetLoader
{
public:
	FFbxImporter() = default;
	~FFbxImporter() override = default;

	FStaticMesh* Load(const FString& Path, const FStaticMeshLoadOptions& LoadOptions);

	bool SupportsExtension(const FString& Extension) const override;
	FString GetLoaderName() const override;

	FSkeletalMesh* LoadSkeletalMesh(const FString& Path, const FStaticMeshLoadOptions& LoadOptions);
	TArray<FSkeleton*> LoadSkeletons(const FString& Path);

    TArray<FAnimationSequence*> LoadAnimationSequences(const FString& Path, const FAnimationImportOptions& ImportOptions);

	FFbxImportedAssetSet AnalyzeImportedAssets(const FString& Path);
	FFbxMeshContentInfo InspectMeshContent(const FString& Path);

private:
	bool ImportScene(const FString& Path, FbxManager* Manager, FbxScene* Scene);

	// Scene -> StaticMesh (mesh node를 재귀로 순회)
	void CollectMeshes(FbxNode* Node, FStaticMesh* InStaticMesh);
	void ProcessMesh(FbxMesh* Mesh, FStaticMesh* InStaticMesh);

	int32 GetOrAddMaterialSlot(FStaticMesh* InStaticMesh, const FString& MaterialName);
	FAABB BuildLocalBounds(FStaticMesh* InStaticMesh) const;

	void NormalizePositionsToUnitCube(FStaticMesh* InStaticMesh);
	void ComputeTangents(FStaticMesh* InStaticMesh);

    void CollectSkeletalMeshes(
        FbxNode* Node,
        FSkeletalMesh* InSkeletalMesh,
        ESkeletalMeshImportPass Pass,
        TMap<FbxNode*, int32>& BoneNodeToIndex,
        bool& bHasImportedSkinnedMesh);

    void ProcessSkeletalMesh(
        FbxMesh* Mesh,
        FSkeletalMesh* InSkeletalMesh,
        ESkeletalMeshImportPass Pass,
        TMap<FbxNode*, int32>& BoneNodeToIndex,
        bool& bHasImportedSkinnedMesh);

    void ProcessRigidAttachedMesh(
        FbxMesh* Mesh,
        FSkeletalMesh* InSkeletalMesh,
        TMap<FbxNode*, int32>& BoneNodeToIndex,
        bool bHasImportedSkinnedMesh);

    int32 GetOrAddMaterialSlot(FSkeletalMesh* InSkeletalMesh, const FString& MaterialName);
    FAABB BuildLocalBounds(FSkeletalMesh* InSkeletalMesh) const;
    void ComputeTangents(FSkeletalMesh* InSkeletalMesh);

    void CollectSkeletonNodes(FbxNode* Node, TArray<FbxNode*>& OutNodes) const;
    bool ExtractAnimationStack(
        FbxScene* Scene,
        FbxAnimStack* AnimStack,
        const TArray<FbxNode*>& BoneNodes,
        const FAnimationImportOptions& ImportOptions,
        FAnimationSequence& OutSequence) const;
    void ExtractBoneAnimationTracks(
        FbxAnimLayer* AnimLayer,
        const TArray<FbxNode*>& BoneNodes,
        FAnimationSequence& OutSequence) const;
    void ExtractShapeKeyTracks(FbxAnimLayer* AnimLayer, FbxScene* Scene, FAnimationSequence& OutSequence) const;
};
