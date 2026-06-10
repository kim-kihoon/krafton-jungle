#pragma once

#include "Object/Object.h"
#include "Animation/Notify/AnimNotifyEvent.h"

#include <algorithm>

struct FPoseContext;
struct FAnimExtractContext;
class FReferenceCollector;

// 시퀀스류 애셋의 공통 베이스. PlayLength/Notify 등 시간축이 있는 모든 자식의 공통 인터페이스.
// 실제 본 키프레임 샘플링은 UAnimSequence 가 구현한다.

#include "Source/Engine/Animation/Sequence/AnimSequenceBase.generated.h"

UCLASS()
class UAnimSequenceBase : public UObject
{
public:
	GENERATED_BODY()
    UAnimSequenceBase()           = default;
    ~UAnimSequenceBase() override = default;

    void Serialize(FArchive& Ar) override;
    void AddReferencedObjects(FReferenceCollector& Collector) override;
    // 수동 바이너리 포맷 — 반사 직렬화 비활성 (UAnimSequence/UAnimMontage 도 상속).
    bool ShouldReflectProperties() const override { return false; }

    // ── 시간/길이 ──
    virtual float GetPlayLength() const
    {
        return PlayLength;
    }

    virtual float GetFrameRate() const
    {
        return FrameRate;
    }

    // ── 포즈 추출 ──
    // Output.SkeletalMesh, Output.Pose.size() 는 호출자가 이미 세팅했다고 가정.
    // 자식 구현은 본별 키프레임을 Ctx.CurrentTime 으로 샘플링해서 Output.Pose 를 채운다.
    virtual void GetBonePose(FPoseContext& Output, const FAnimExtractContext& Ctx) const
    {
        (void)Output;
        (void)Ctx;
    }

    // ── Notify ──
    const TArray<FAnimNotifyEvent>& GetNotifies() const
    {
        return Notifies;
    }

    TArray<FAnimNotifyEvent>& GetMutableNotifies()
    {
        return Notifies;
    }

    float GetNotifyTrackVolume(int32 TrackIndex) const
    {
        if (TrackIndex < 0 || TrackIndex >= static_cast<int32>(NotifyTrackVolumes.size()))
        {
            return 1.0f;
        }
        return std::clamp(NotifyTrackVolumes[TrackIndex], 0.0f, 1.0f);
    }

protected:
    float                    PlayLength = 0.0f;  // sec
    float                    FrameRate  = 30.0f; // fps
    int32                    NotifyTrackCount = 1;
    TArray<float>            NotifyTrackVolumes;
    TArray<FAnimNotifyEvent> Notifies; 
};
