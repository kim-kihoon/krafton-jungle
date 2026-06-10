#include "AnimMontage.h"

#include "Animation/Sequence/AnimSequence.h"
#include "Animation/AnimExtractContext.h"
#include "Animation/AnimationManager.h"
#include "Animation/PoseContext.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"

#include <algorithm>
#include <cstdio>

FArchive& operator<<(FArchive& Ar, FCompositeSection& S)
{
    Ar << S.SectionName;
    Ar << S.StartTime;
    Ar << S.LinkTime;
    Ar << S.NextSectionName;
    return Ar;
}

void UAnimMontage::Serialize(FArchive& Ar)
{
    UObject::Serialize(Ar);

    Ar << AssetPathFileName;
    Ar << BlendInTime;
    Ar << BlendOutTime;

    // SourceSequence 는 path 로만 저장. Load 시 LoadAnimation 으로 resolve.
    if (Ar.IsSaving())
    {
        SourceSequencePath = IsValid(SourceSequence) ? SourceSequence->GetAssetPathFileName() : FString("None");
    }
    Ar << SourceSequencePath;

    // Sections — TArray serialize 직접 (FCompositeSection 의 operator<< 사용).
    {
        int32 Count = static_cast<int32>(Sections.size());
        Ar << Count;
        if (Ar.IsLoading())
        {
            Sections.clear();
            Sections.resize(Count);
        }
        for (int32 i = 0; i < Count; ++i)
        {
            Ar << Sections[i];
        }
    }

    // Notify 는 base 의 TArray<FAnimNotifyEvent> 사용 — montage v1 에서는 notify 직접 트래킹 안 함.
    // (SourceSequence 의 notify 가 그대로 dispatch 됨, MontageInstance 의 AddAnimNotifies 호출로.)

    // Load 후 SourceSequence resolve — caller 측 (FAnimationManager::LoadMontage) 에서 처리.
    // Serialize 안에서 LoadAnimation 호출하면 montage cache miss 시 무한루프 위험.
}

void UAnimMontage::SetSourceSequence(UAnimSequence* InSeq)
{
    SourceSequence = IsValid(InSeq) ? InSeq : nullptr;
    if (SourceSequence)
    {
        PlayLength         = SourceSequence->GetPlayLength();
        FrameRate          = SourceSequence->GetFrameRate();
        SourceSequencePath = SourceSequence->GetAssetPathFileName();
        EnsureDefaultSection();
        NormalizeSections();
    }
    else
    {
        PlayLength = 0.0f;
        SourceSequencePath = "None";
        NormalizeSections();
    }
}

void UAnimMontage::EnsureDefaultSection()
{
    if (!Sections.empty()) return;
    FCompositeSection Default;
    Default.SectionName     = FName("Default");
    Default.StartTime       = 0.0f;
    Default.LinkTime        = IsValid(SourceSequence) ? SourceSequence->GetPlayLength() : 0.0f;
    Default.NextSectionName = FName::None;
    Sections.push_back(Default);
}

void UAnimMontage::NormalizeSections()
{
    const float MaxTime = IsValid(SourceSequence) ? std::max(SourceSequence->GetPlayLength(), 0.0f) : 0.0f;

    auto NameExistsBefore = [this](FName Name, int32 BeforeIndex) -> bool
    {
        if (Name == FName::None || Name.ToString().empty()) return false;
        for (int32 i = 0; i < BeforeIndex && i < static_cast<int32>(Sections.size()); ++i)
        {
            if (Sections[i].SectionName == Name) return true;
        }
        return false;
    };

    auto MakeUniqueName = [this](const char* Prefix) -> FName
    {
        for (int32 Candidate = 1; Candidate < 10000; ++Candidate)
        {
            char Buf[64];
            std::snprintf(Buf, sizeof(Buf), "%s%d", Prefix, Candidate);
            if (!FindSection(FName(Buf))) return FName(Buf);
        }
        return FName("Section");
    };

    for (int32 Index = 0; Index < static_cast<int32>(Sections.size()); ++Index)
    {
        FCompositeSection& S = Sections[Index];
        S.StartTime = std::clamp(S.StartTime, 0.0f, MaxTime);
        S.LinkTime  = std::clamp(S.LinkTime,  S.StartTime, MaxTime);

        // 이름이 비어 있거나 앞 section 과 중복되면 runtime lookup 이 모호해지므로 안전한 이름으로 복구.
        if (S.SectionName == FName::None || S.SectionName.ToString().empty() || NameExistsBefore(S.SectionName, Index))
        {
            S.SectionName = MakeUniqueName("Section_");
        }
    }

    // 깨진 NextSectionName 은 종료로 정규화. 자기 자신은 의도적인 loop 로 허용.
    for (FCompositeSection& S : Sections)
    {
        if (S.NextSectionName == FName::None) continue;
        if (!FindSection(S.NextSectionName))
        {
            S.NextSectionName = FName::None;
        }
    }
}

const FCompositeSection* UAnimMontage::FindSection(FName Name) const
{
    for (const FCompositeSection& S : Sections)
    {
        if (S.SectionName == Name) return &S;
    }
    return nullptr;
}

int32 UAnimMontage::GetSectionIndex(FName Name) const
{
    for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
    {
        if (Sections[i].SectionName == Name) return i;
    }
    return -1;
}

void UAnimMontage::GetBonePose(FPoseContext& Output, const FAnimExtractContext& Ctx) const
{
    if (IsValid(SourceSequence))
    {
        // SourceSequence 가 위임 — Ctx.CurrentTime 은 sequence 시간 그대로.
        // Montage v1 에서는 source 의 ForceRootLock/RootMotion 옵션이 그대로 적용됨.
        SourceSequence->GetBonePose(Output, Ctx);
    }
    else
    {
        Output.ResetToRefPose();
    }
}


UAnimSequence* UAnimMontage::GetSourceSequence() const
{
    return IsValid(SourceSequence) ? SourceSequence : nullptr;
}

void UAnimMontage::AddReferencedObjects(FReferenceCollector& Collector)
{
    UAnimSequenceBase::AddReferencedObjects(Collector);
    Collector.AddReferencedObject(SourceSequence, "AnimMontage.SourceSequence");
}
