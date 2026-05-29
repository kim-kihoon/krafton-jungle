#pragma once

#include "Animation/AnimTypes.h"
#include "Math/Transform.h"
#include "Object/Object.h"

#include <utility>

/**
 * FBX에서 import된 source/raw animation data model입니다.
 * 특정 track을 시간 기준으로 sampling하는 책임을 가집니다.
 */
class UAnimDataModel : public UObject
{
public:
	DECLARE_CLASS(UAnimDataModel, UObject)

	UAnimDataModel() = default;
	~UAnimDataModel() override = default;

	void Serialize(FArchive& Ar) override;

	const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const { return BoneAnimationTracks; }
	void SetBoneAnimationTracks(TArray<FBoneAnimationTrack>&& InTracks) { BoneAnimationTracks = std::move(InTracks); }

	int32 GetNumBoneTracks() const { return static_cast<int32>(BoneAnimationTracks.size()); }
	const FBoneAnimationTrack* GetBoneTrackByIndex(int32 TrackIndex) const;
	const FBoneAnimationTrack* FindBoneTrackByName(const FName& BoneName) const;
	const FBoneAnimationTrack* FindBoneTrackByIndex(int32 BoneTreeIndex) const;

	float GetPlayLength() const { return PlayLength; }
	void SetPlayLength(float InPlayLength) { PlayLength = InPlayLength; }

	float GetFrameRate() const { return FrameRate; }
	void SetFrameRate(float InFrameRate) { FrameRate = InFrameRate; }

	int32 GetNumberOfFrames() const { return NumberOfFrames; }
	void SetNumberOfFrames(int32 InNumberOfFrames) { NumberOfFrames = InNumberOfFrames; }

	int32 GetNumberOfKeys() const { return NumberOfKeys; }
	void SetNumberOfKeys(int32 InNumberOfKeys) { NumberOfKeys = InNumberOfKeys; }

	bool EvaluateBoneTrackTransform(const FBoneAnimationTrack& Track, float Time, FTransform& OutTransform, const FTransform& DefaultTransform) const;

	const TArray<FAnimNotifyEvent>& GetNotifies() const { return Notifies; }
	TArray<FAnimNotifyEvent>& GetNotifiesMutable() { return Notifies; }
	void SetNotifies(TArray<FAnimNotifyEvent>&& InNotifies) { Notifies = std::move(InNotifies); }

private:
	TArray<FBoneAnimationTrack> BoneAnimationTracks;
	TArray<FAnimNotifyEvent>   Notifies;
	float PlayLength = 0.0f;
	float FrameRate = 30.0f;
	int32 NumberOfFrames = 0;
	int32 NumberOfKeys = 0;
};
