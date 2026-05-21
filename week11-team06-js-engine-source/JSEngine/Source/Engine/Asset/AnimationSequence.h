#pragma once

#include "Asset/AnimationTypes.h"
#include "Animation/AnimationSequenceBase.h"

#include "UAnimationSequence.generated.h"

// FBX 등에서 import한 실제 animation sequence asset이다.
// FAnimationSequence 하나를 소유하고, 재생 계층에는 UAnimationSequenceBase API로 노출한다.
UCLASS()
class UAnimationSequence : public UAnimationSequenceBase
{
public:
    GENERATED_BODY(UAnimationSequence, UAnimationSequenceBase)

    UAnimationSequence() = default;
    ~UAnimationSequence() override;

    void SetSequenceData(FAnimationSequence* InSequenceData);

    FAnimationSequence* GetSequenceData();
    const FAnimationSequence* GetSequenceData() const;

    const FString& GetResolvedPath() const;

    void SetAssetPath(const FString& InAssetPath) {AssetPath = InAssetPath;}
    void SetSourceImportPath(const FString& InSourceImportPath) {SourceImportPath = InSourceImportPath;}

	const FString& GetAssetPath() const { return AssetPath;}
    const FString& GetSourceImportPath() const { return SourceImportPath;}

    bool HasValidSequenceData() const;
    float GetPlayLength() const override;
    bool SamplePose(const USkeletalMesh* TargetMesh, float Time, TArray<FMatrix>& OutLocalPose) const override;
private:
    void RebuildTrackCache(const USkeletalMesh* TargetMesh) const;

private:
    FAnimationSequence* SequenceData = nullptr;
    mutable const USkeletalMesh* CachedTargetMesh = nullptr;
    mutable TArray<int32> CachedTrackIndexByBoneIndex;

    FString AssetPath;			// anim 에셋의 경로
    FString SourceImportPath;	// 원본의 경로
};

