#include "Asset/AnimationSequence.h"

#include "Asset/SkeletalMesh.h"

#include <algorithm>
#include <cmath>

namespace
{
struct FKeyFrameRange
{
    int32 PrevIndex = 0;
    int32 NextIndex = 0;
    float Alpha = 0.0f;
};

float ClampSequenceTime(float Time, const FAnimationSequence& SequenceData)
{
    if (Time < 0.0f)
    {
        return 0.0f;
    }

    if (SequenceData.DurationSeconds > 0.0f && Time > SequenceData.DurationSeconds)
    {
        return SequenceData.DurationSeconds;
    }

    return Time;
}

FKeyFrameRange GetKeyFrameRange(float Time, const FAnimationSequence& SequenceData, int32 KeyCount)
{
    FKeyFrameRange Range;
    if (KeyCount <= 1)
    {
        return Range;
    }

    const float ClampedTime = ClampSequenceTime(Time, SequenceData);
    float KeyPosition = 0.0f;

    if (SequenceData.SourceFrameRate > 0.0f)
    {
        KeyPosition = ClampedTime * SequenceData.SourceFrameRate;
    }
    else if (SequenceData.DurationSeconds > 0.0f)
    {
        const float NormalizedTime = ClampedTime / SequenceData.DurationSeconds;
        KeyPosition = NormalizedTime * static_cast<float>(KeyCount - 1);
    }

    const int32 PrevIndex = static_cast<int32>(std::floor(KeyPosition));
    const int32 NextIndex = PrevIndex + 1;

    Range.PrevIndex = std::clamp(PrevIndex, 0, KeyCount - 1);
    Range.NextIndex = std::clamp(NextIndex, 0, KeyCount - 1);
    Range.Alpha = KeyPosition - static_cast<float>(Range.PrevIndex);
    Range.Alpha = std::clamp(Range.Alpha, 0.0f, 1.0f);

    return Range;
}

int32 FindBoneTrackIndexByName(const FAnimationSequence& SequenceData, const FString& BoneName)
{
    for (int32 TrackIndex = 0; TrackIndex < static_cast<int32>(SequenceData.BoneTracks.size()); ++TrackIndex)
    {
        const FBoneAnimationTrack& BoneTrack = SequenceData.BoneTracks[TrackIndex];
        if (BoneTrack.BoneName == BoneName)
        {
            return TrackIndex;
        }
    }

    return -1;
}

FVector SampleTranslation(const FAnimationSequence& SequenceData, const FBoneAnimationTrack& BoneTrack, float Time)
{
    const TArray<FVector>& PosKeys = BoneTrack.InternalTrack.PosKeys;
    if (PosKeys.empty())
    {
        return BoneTrack.DefaultTranslation;
    }

    const FKeyFrameRange Range = GetKeyFrameRange(Time, SequenceData, static_cast<int32>(PosKeys.size()));
    return FVector::Lerp(PosKeys[Range.PrevIndex], PosKeys[Range.NextIndex], Range.Alpha);
}

FQuat SampleRotation(const FAnimationSequence& SequenceData, const FBoneAnimationTrack& BoneTrack, float Time)
{
    const TArray<FQuat>& RotKeys = BoneTrack.InternalTrack.RotKeys;
    if (RotKeys.empty())
    {
        return FQuat::MakeFromEuler(BoneTrack.DefaultRotationEuler);
    }

    const FKeyFrameRange Range = GetKeyFrameRange(Time, SequenceData, static_cast<int32>(RotKeys.size()));
    return FQuat::Slerp(RotKeys[Range.PrevIndex], RotKeys[Range.NextIndex], Range.Alpha);
}

FVector SampleScale(const FAnimationSequence& SequenceData, const FBoneAnimationTrack& BoneTrack, float Time)
{
    const TArray<FVector>& ScaleKeys = BoneTrack.InternalTrack.ScaleKeys;
    if (ScaleKeys.empty())
    {
        return BoneTrack.DefaultScale;
    }

    const FKeyFrameRange Range = GetKeyFrameRange(Time, SequenceData, static_cast<int32>(ScaleKeys.size()));
    return FVector::Lerp(ScaleKeys[Range.PrevIndex], ScaleKeys[Range.NextIndex], Range.Alpha);
}

bool HasPoseKeys(const FBoneAnimationTrack& BoneTrack)
{
    return !BoneTrack.InternalTrack.PosKeys.empty() ||
        !BoneTrack.InternalTrack.RotKeys.empty() ||
        !BoneTrack.InternalTrack.ScaleKeys.empty();
}

FMatrix BuildTrackTransform(const FVector& Translation, const FQuat& Rotation, const FVector& Scale)
{
    return FMatrix::MakeTRS(Translation, Rotation.ToMatrix(), Scale);
}

FMatrix BuildSampledKeyTransform(const FAnimationSequence& SequenceData, const FBoneAnimationTrack& BoneTrack, float Time)
{
    return BuildTrackTransform(
        SampleTranslation(SequenceData, BoneTrack, Time),
        SampleRotation(SequenceData, BoneTrack, Time),
        SampleScale(SequenceData, BoneTrack, Time));
}
}

UAnimationSequence::~UAnimationSequence()
{
    delete SequenceData;
    SequenceData = nullptr;
}

void UAnimationSequence::SetSequenceData(FAnimationSequence* InSequenceData)
{
    if (SequenceData == InSequenceData)
    {
        return;
    }

    delete SequenceData;
    SequenceData = InSequenceData;

    if (SequenceData && SourceImportPath.empty())
    {
        SourceImportPath = SequenceData->SourcePath;
    }

    CachedTargetMesh = nullptr;
    CachedTrackIndexByBoneIndex.clear();
}

FAnimationSequence* UAnimationSequence::GetSequenceData()
{
    return SequenceData;
}

const FAnimationSequence* UAnimationSequence::GetSequenceData() const
{
    return SequenceData;
}

const FString& UAnimationSequence::GetResolvedPath() const
{
    return !AssetPath.empty() ? AssetPath : SourceImportPath;
}

bool UAnimationSequence::HasValidSequenceData() const
{
    return SequenceData != nullptr && (!SequenceData->BoneTracks.empty() || !SequenceData->ShapeKeyTracks.empty());
}

float UAnimationSequence::GetPlayLength() const
{
    return SequenceData ? SequenceData->DurationSeconds : 0.0f;
}

bool UAnimationSequence::SamplePose(const USkeletalMesh* TargetMesh, float Time, TArray<FMatrix>& OutLocalPose) const
{
    if (!SequenceData || !TargetMesh)
    {
        OutLocalPose.clear();
        return false;
    }

    const TArray<FBoneInfo>& TargetBones = TargetMesh->GetBones();
    if (TargetBones.empty())
    {
        OutLocalPose.clear();
        return false;
    }

    if (CachedTargetMesh != TargetMesh || CachedTrackIndexByBoneIndex.size() != TargetBones.size())
    {
        RebuildTrackCache(TargetMesh);
    }

    OutLocalPose.resize(TargetBones.size());

    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(TargetBones.size()); ++BoneIndex)
    {
        const FBoneInfo& TargetBone = TargetBones[BoneIndex];
        OutLocalPose[BoneIndex] = TargetBone.LocalBindTransform;

        const int32 TrackIndex = CachedTrackIndexByBoneIndex[BoneIndex];
        if (TrackIndex < 0)
        {
            continue;
        }

        const FBoneAnimationTrack& BoneTrack = SequenceData->BoneTracks[TrackIndex];

        if (!HasPoseKeys(BoneTrack))
        {
            continue;
        }

        const FMatrix CurrentKeyTransform = BuildSampledKeyTransform(*SequenceData, BoneTrack, Time);

        OutLocalPose[BoneIndex] = CurrentKeyTransform;
    }

    return true;
}

void UAnimationSequence::RebuildTrackCache(const USkeletalMesh* TargetMesh) const
{
    CachedTargetMesh = TargetMesh;
    CachedTrackIndexByBoneIndex.clear();

    if (!SequenceData || !TargetMesh)
    {
        return;
    }

    const TArray<FBoneInfo>& TargetBones = TargetMesh->GetBones();
    CachedTrackIndexByBoneIndex.resize(TargetBones.size());

    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(TargetBones.size()); ++BoneIndex)
    {
        CachedTrackIndexByBoneIndex[BoneIndex] = FindBoneTrackIndexByName(*SequenceData, TargetBones[BoneIndex].Name);
    }
}

