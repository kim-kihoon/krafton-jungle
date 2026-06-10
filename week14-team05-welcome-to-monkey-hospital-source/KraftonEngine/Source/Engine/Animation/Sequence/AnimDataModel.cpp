#include "AnimDataModel.h"
#include "Animation/Notify/AnimNotify.h"
#include "Animation/Notify/AnimNotifyState.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"

#include <algorithm>

namespace
{
    constexpr int32 NotifyBlockMagic = -1314146612;
    constexpr int32 NotifyBlockVersion = 2;

    void ClampNotifyTrackData(TArray<FAnimNotifyEvent>& Notifies, int32& NotifyTrackCount,
                              TArray<float>& NotifyTrackVolumes)
    {
        NotifyTrackCount = std::max(NotifyTrackCount, 1);
        if (NotifyTrackVolumes.size() < static_cast<size_t>(NotifyTrackCount))
        {
            NotifyTrackVolumes.resize(NotifyTrackCount, 1.0f);
        }
        else if (NotifyTrackVolumes.size() > static_cast<size_t>(NotifyTrackCount))
        {
            NotifyTrackVolumes.resize(NotifyTrackCount);
        }
        for (float& Volume : NotifyTrackVolumes)
        {
            Volume = std::clamp(Volume, 0.0f, 1.0f);
        }
        for (FAnimNotifyEvent& Notify : Notifies)
        {
            Notify.TrackIndex = std::clamp(Notify.TrackIndex, 0, NotifyTrackCount - 1);
        }
    }
}

void UAnimDataModel::Serialize(FArchive& Ar)
{
    UObject::Serialize(Ar);

    Ar << PlayLength;
    Ar << FrameRate;
    Ar << NumFrames;
    Ar << BoneAnimationTracks;

    // Morph curves are authored data owned by this animation. They are appended before notifies.
    Ar << MorphTargetCurves;

    // Notifies 는 Outer 인지 직렬화 — Notify/NotifyState 객체 클래스명 + UPROPERTY(Save) payload
    // 까지 round-trip. ObjectFactory::Create 가 Outer 를 받아야 라이프타임 체인 형성되므로
    // TArray operator<< (raw 만) 사용 못 함, 명시적 루프로 entry 별 Serialize(Ar, this) 호출.
    if (Ar.IsSaving())
    {
        ClampNotifyTrackData(Notifies, NotifyTrackCount, NotifyTrackVolumes);

        int32 Magic = NotifyBlockMagic;
        int32 Version = NotifyBlockVersion;
        int32 NotifyCount = static_cast<int32>(Notifies.size());
        Ar << Magic;
        Ar << Version;
        Ar << NotifyTrackCount;
        Ar << NotifyTrackVolumes;
        Ar << NotifyCount;
        for (int32 i = 0; i < NotifyCount; ++i)
        {
            Ar << Notifies[i].TrackIndex;
            Notifies[i].Serialize(Ar, this);
        }
        return;
    }

    int32 NotifyCountOrMagic = 0;
    Ar << NotifyCountOrMagic;
    if (NotifyCountOrMagic == NotifyBlockMagic)
    {
        int32 Version = 0;
        int32 NotifyCount = 0;
        Ar << Version;
        Ar << NotifyTrackCount;
        if (Version >= 2)
        {
            Ar << NotifyTrackVolumes;
        }
        else
        {
            NotifyTrackVolumes.clear();
        }
        Ar << NotifyCount;

        Notifies.clear();
        Notifies.resize(std::max(NotifyCount, 0));
        for (int32 i = 0; i < static_cast<int32>(Notifies.size()); ++i)
        {
            Ar << Notifies[i].TrackIndex;
            Notifies[i].Serialize(Ar, this);
        }
        ClampNotifyTrackData(Notifies, NotifyTrackCount, NotifyTrackVolumes);
        return;
    }

    const int32 LegacyNotifyCount = std::max(NotifyCountOrMagic, 0);
    NotifyTrackCount = 1;
    NotifyTrackVolumes.clear();
    NotifyTrackVolumes.push_back(1.0f);
    Notifies.clear();
    Notifies.resize(LegacyNotifyCount);
    for (int32 i = 0; i < LegacyNotifyCount; ++i)
    {
        Notifies[i].TrackIndex = 0;
        Notifies[i].Serialize(Ar, this);
    }
}


void UAnimDataModel::AddReferencedObjects(FReferenceCollector& Collector)
{
    UObject::AddReferencedObjects(Collector);
    for (FAnimNotifyEvent& NotifyEvent : Notifies)
    {
        if (NotifyEvent.Notify && !IsValid(NotifyEvent.Notify))
        {
            NotifyEvent.Notify = nullptr;
        }
        if (NotifyEvent.NotifyState && !IsValid(NotifyEvent.NotifyState))
        {
            NotifyEvent.NotifyState = nullptr;
        }
        Collector.AddReferencedObject(NotifyEvent.Notify, "AnimDataModel.Notify");
        Collector.AddReferencedObject(NotifyEvent.NotifyState, "AnimDataModel.NotifyState");
    }
}
