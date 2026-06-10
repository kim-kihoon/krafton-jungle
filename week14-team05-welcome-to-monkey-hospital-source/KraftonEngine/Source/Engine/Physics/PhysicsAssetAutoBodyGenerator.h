#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Matrix.h"
#include "Object/FName.h"

class UPhysicsAsset;
class USkeletalMesh;

//자동 body 생성 관련 유틸을 모아둔 파일입니다. 해당 기능은
//physics editor 상단의 Regenerate Bodies 버튼과 연결되어 있습니다.
enum class EPhysicsAssetAutoBodyMethod : uint8
{
    BoneAxis,
    PCAAnalysis
};

enum class EPhysicsAssetAutoBodyPrimitiveType : uint8
{
    Capsule,
    Box,
    Sphere
};

struct FPhysicsAssetAutoBodyGeneratorOptions
{
    EPhysicsAssetAutoBodyMethod Method = EPhysicsAssetAutoBodyMethod::PCAAnalysis;
    EPhysicsAssetAutoBodyPrimitiveType PrimitiveType = EPhysicsAssetAutoBodyPrimitiveType::Capsule;
    bool bCreateConstraints = true;
    bool bDisableCollisionBetweenConstrainedBodies = true;
    bool bReplaceExisting = true;
    bool bSkipHelperBones = true;
    bool bAllowBoneAxisFallback = false;
    bool bMergeSmallBones = true;
    float MinInfluenceWeight = 0.15f;
    int32 MinWeightedVertices = 16;
    float MinBoneSize = 0.25f;
    float MinWeldSize = 1.0e-4f;
    float FitPadding = 1.0f;
};

struct FPhysicsAssetAutoBodyGeneratorResult
{
    int32 GeneratedBodyCount = 0;
    int32 GeneratedConstraintCount = 0;
    int32 SkippedBoneCount = 0;
    int32 FirstGeneratedBodyIndex = -1;
    FName FirstGeneratedBoneName = FName::None;
    bool bAssetChanged = false;
};

class FPhysicsAssetAutoBodyGenerator
{
public:
    // Rebuilds primitive body setups from the skeletal mesh reference pose.
    // The editor owns UI state and selection; this generator only mutates asset data.
    static bool Regenerate(
        UPhysicsAsset* PhysicsAsset,
        USkeletalMesh* SkeletalMesh,
        const FPhysicsAssetAutoBodyGeneratorOptions& Options,
        FPhysicsAssetAutoBodyGeneratorResult* OutResult = nullptr,
        const TArray<FMatrix>* OverrideBoneGlobalMatrices = nullptr);
};
