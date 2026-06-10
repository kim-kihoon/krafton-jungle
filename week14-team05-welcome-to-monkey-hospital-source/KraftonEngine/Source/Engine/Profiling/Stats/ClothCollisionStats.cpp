#include "Profiling/Stats/ClothCollisionStats.h"

uint32 FClothCollisionStats::TickAttempts = 0;
uint32 FClothCollisionStats::SkippedNoAsset = 0;
uint32 FClothCollisionStats::SkippedNoClothPayload = 0;
uint32 FClothCollisionStats::SkippedNonCPUSkinning = 0;
uint32 FClothCollisionStats::SkippedNoSkinnedVertices = 0;
uint32 FClothCollisionStats::ComponentCount = 0;
uint32 FClothCollisionStats::EnabledClothSections = 0;
uint32 FClothCollisionStats::CollisionEligibleSections = 0;
uint32 FClothCollisionStats::WorldStaticConfiguredSections = 0;
uint32 FClothCollisionStats::WorldDynamicConfiguredSections = 0;
uint32 FClothCollisionStats::InvalidSectionBounds = 0;
uint32 FClothCollisionStats::MissingPhysicsRuntimeSections = 0;
uint32 FClothCollisionStats::SectionCount = 0;
uint32 FClothCollisionStats::WorldStaticEnabledSections = 0;
uint32 FClothCollisionStats::WorldDynamicEnabledSections = 0;
uint32 FClothCollisionStats::WorldStaticCandidateCount = 0;
uint32 FClothCollisionStats::WorldDynamicCandidateCount = 0;
uint32 FClothCollisionStats::WorldStaticGatheredSpheres = 0;
uint32 FClothCollisionStats::WorldStaticGatheredCapsules = 0;
uint32 FClothCollisionStats::WorldStaticGatheredBoxes = 0;
uint32 FClothCollisionStats::WorldStaticSelectedSpheres = 0;
uint32 FClothCollisionStats::WorldStaticSelectedCapsules = 0;
uint32 FClothCollisionStats::WorldStaticSelectedBoxes = 0;
uint32 FClothCollisionStats::WorldDynamicGatheredSpheres = 0;
uint32 FClothCollisionStats::WorldDynamicGatheredCapsules = 0;
uint32 FClothCollisionStats::WorldDynamicGatheredBoxes = 0;
uint32 FClothCollisionStats::WorldDynamicSelectedSpheres = 0;
uint32 FClothCollisionStats::WorldDynamicSelectedCapsules = 0;
uint32 FClothCollisionStats::WorldDynamicSelectedBoxes = 0;
uint32 FClothCollisionStats::WorldStaticRejectedBySectionBounds = 0;
uint32 FClothCollisionStats::WorldDynamicRejectedBySectionBounds = 0;
uint32 FClothCollisionStats::WorldStaticSkippedFilter = 0;
uint32 FClothCollisionStats::WorldDynamicSkippedFilter = 0;
FClothCollisionDebugStats FClothCollisionStats::DebugStats;
TArray<FClothCollisionStatCandidate> FClothCollisionStats::RecentWorldStaticCandidates;
TArray<FClothCollisionStatCandidate> FClothCollisionStats::RecentWorldDynamicCandidates;

namespace
{
    constexpr uint32 MaxRecentWorldStaticCandidates = 8;
    constexpr uint32 MaxRecentWorldDynamicCandidates = 8;

    void AddPrimitiveCounter(
        EClothCollisionPrimitiveType Type,
        uint32& Spheres,
        uint32& Capsules,
        uint32& Boxes)
    {
        switch (Type)
        {
        case EClothCollisionPrimitiveType::Sphere:
            ++Spheres;
            break;
        case EClothCollisionPrimitiveType::Capsule:
            ++Capsules;
            break;
        case EClothCollisionPrimitiveType::Box:
            ++Boxes;
            break;
        }
    }

    void AccumulateDebugStats(FClothCollisionDebugStats& Target, const FClothCollisionDebugStats& Source)
    {
        Target.GatheredSpheres += Source.GatheredSpheres;
        Target.GatheredCapsules += Source.GatheredCapsules;
        Target.GatheredBoxes += Source.GatheredBoxes;
        Target.SelectedSpheres += Source.SelectedSpheres;
        Target.SelectedCapsules += Source.SelectedCapsules;
        Target.SelectedBoxes += Source.SelectedBoxes;
        Target.UploadedSpheres += Source.UploadedSpheres;
        Target.UploadedCapsules += Source.UploadedCapsules;
        Target.UploadedPlanes += Source.UploadedPlanes;
        Target.UploadedConvexes += Source.UploadedConvexes;
        Target.SkippedNonUniformScale += Source.SkippedNonUniformScale;
        Target.Rejected += Source.Rejected;
        Target.Truncated += Source.Truncated;
        Target.GatheredWorldStatic += Source.GatheredWorldStatic;
        Target.SelectedWorldStatic += Source.SelectedWorldStatic;
        Target.RejectedWorldStatic += Source.RejectedWorldStatic;
        Target.TruncatedWorldStatic += Source.TruncatedWorldStatic;
        Target.GatheredWorldDynamic += Source.GatheredWorldDynamic;
        Target.SelectedWorldDynamic += Source.SelectedWorldDynamic;
        Target.RejectedWorldDynamic += Source.RejectedWorldDynamic;
        Target.TruncatedWorldDynamic += Source.TruncatedWorldDynamic;
    }
}

void FClothCollisionStats::Reset()
{
    TickAttempts = 0;
    SkippedNoAsset = 0;
    SkippedNoClothPayload = 0;
    SkippedNonCPUSkinning = 0;
    SkippedNoSkinnedVertices = 0;
    ComponentCount = 0;
    EnabledClothSections = 0;
    CollisionEligibleSections = 0;
    WorldStaticConfiguredSections = 0;
    WorldDynamicConfiguredSections = 0;
    InvalidSectionBounds = 0;
    MissingPhysicsRuntimeSections = 0;
    SectionCount = 0;
    WorldStaticEnabledSections = 0;
    WorldDynamicEnabledSections = 0;
    WorldStaticCandidateCount = 0;
    WorldDynamicCandidateCount = 0;
    WorldStaticGatheredSpheres = 0;
    WorldStaticGatheredCapsules = 0;
    WorldStaticGatheredBoxes = 0;
    WorldStaticSelectedSpheres = 0;
    WorldStaticSelectedCapsules = 0;
    WorldStaticSelectedBoxes = 0;
    WorldDynamicGatheredSpheres = 0;
    WorldDynamicGatheredCapsules = 0;
    WorldDynamicGatheredBoxes = 0;
    WorldDynamicSelectedSpheres = 0;
    WorldDynamicSelectedCapsules = 0;
    WorldDynamicSelectedBoxes = 0;
    WorldStaticRejectedBySectionBounds = 0;
    WorldDynamicRejectedBySectionBounds = 0;
    WorldStaticSkippedFilter = 0;
    WorldDynamicSkippedFilter = 0;
    DebugStats.Reset();
    RecentWorldStaticCandidates.clear();
    RecentWorldDynamicCandidates.clear();
}

void FClothCollisionStats::AddTickAttempt()
{
    ++TickAttempts;
}

void FClothCollisionStats::AddSkippedNoAsset()
{
    ++SkippedNoAsset;
}

void FClothCollisionStats::AddSkippedNoClothPayload()
{
    ++SkippedNoClothPayload;
}

void FClothCollisionStats::AddSkippedNonCPUSkinning()
{
    ++SkippedNonCPUSkinning;
}

void FClothCollisionStats::AddSkippedNoSkinnedVertices()
{
    ++SkippedNoSkinnedVertices;
}

void FClothCollisionStats::AddEnabledSection()
{
    ++EnabledClothSections;
}

void FClothCollisionStats::AddCollisionEligibleSection(
    bool bWorldStaticConfigured,
    bool bWorldDynamicConfigured,
    bool bHasValidBounds,
    bool bHasPhysicsRuntime)
{
    ++CollisionEligibleSections;
    if (bWorldStaticConfigured)
    {
        ++WorldStaticConfiguredSections;
    }
    if (bWorldDynamicConfigured)
    {
        ++WorldDynamicConfiguredSections;
    }
    if ((bWorldStaticConfigured || bWorldDynamicConfigured) && !bHasPhysicsRuntime)
    {
        ++MissingPhysicsRuntimeSections;
    }

    if (!bHasValidBounds)
    {
        ++InvalidSectionBounds;
    }
}

void FClothCollisionStats::AddComponentResult()
{
    ++ComponentCount;
}

void FClothCollisionStats::AddSectionResult(
    const FClothCollisionGatherResult& Result,
    bool bWorldStaticEnabled,
    bool bWorldDynamicEnabled)
{
    ++SectionCount;
    if (bWorldStaticEnabled)
    {
        ++WorldStaticEnabledSections;
    }
    if (bWorldDynamicEnabled)
    {
        ++WorldDynamicEnabledSections;
    }

    AccumulateDebugStats(DebugStats, Result.Stats);

    for (const FClothCollisionCandidate& Candidate : Result.Candidates)
    {
        if (Candidate.SourceId.Source != EClothCollisionSource::WorldStatic &&
            Candidate.SourceId.Source != EClothCollisionSource::WorldDynamic)
        {
            continue;
        }

        const bool bDynamic = Candidate.SourceId.Source == EClothCollisionSource::WorldDynamic;
        uint32& CandidateCount = bDynamic ? WorldDynamicCandidateCount : WorldStaticCandidateCount;
        uint32& GatheredSpheres = bDynamic ? WorldDynamicGatheredSpheres : WorldStaticGatheredSpheres;
        uint32& GatheredCapsules = bDynamic ? WorldDynamicGatheredCapsules : WorldStaticGatheredCapsules;
        uint32& GatheredBoxes = bDynamic ? WorldDynamicGatheredBoxes : WorldStaticGatheredBoxes;
        uint32& SelectedSpheres = bDynamic ? WorldDynamicSelectedSpheres : WorldStaticSelectedSpheres;
        uint32& SelectedCapsules = bDynamic ? WorldDynamicSelectedCapsules : WorldStaticSelectedCapsules;
        uint32& SelectedBoxes = bDynamic ? WorldDynamicSelectedBoxes : WorldStaticSelectedBoxes;
        uint32& RejectedBySectionBounds =
            bDynamic ? WorldDynamicRejectedBySectionBounds : WorldStaticRejectedBySectionBounds;
        uint32& SkippedFilter = bDynamic ? WorldDynamicSkippedFilter : WorldStaticSkippedFilter;
        TArray<FClothCollisionStatCandidate>& RecentCandidates =
            bDynamic ? RecentWorldDynamicCandidates : RecentWorldStaticCandidates;
        const uint32 MaxRecentCandidates =
            bDynamic ? MaxRecentWorldDynamicCandidates : MaxRecentWorldStaticCandidates;

        ++CandidateCount;
        AddPrimitiveCounter(Candidate.Type, GatheredSpheres, GatheredCapsules, GatheredBoxes);

        if (Candidate.State == EClothCollisionSelectState::Selected)
        {
            AddPrimitiveCounter(Candidate.Type, SelectedSpheres, SelectedCapsules, SelectedBoxes);
        }
        else if (Candidate.State == EClothCollisionSelectState::RejectedBySectionBounds)
        {
            ++RejectedBySectionBounds;
        }
        else if (Candidate.State == EClothCollisionSelectState::SkippedFilter)
        {
            ++SkippedFilter;
        }

        if (RecentCandidates.size() < MaxRecentCandidates)
        {
            FClothCollisionStatCandidate Entry;
            Entry.OwnerActorId = Candidate.SourceId.OwnerActorId;
            Entry.OwnerComponentId = Candidate.SourceId.OwnerComponentId;
            Entry.BodyIndex = Candidate.SourceId.BodyIndex;
            Entry.ShapeIndex = Candidate.SourceId.ShapeIndex;
            Entry.Type = Candidate.Type;
            Entry.State = Candidate.State;
            Entry.BoundsCenter = Candidate.WorldBounds.GetCenter();
            Entry.BoundsExtent = Candidate.WorldBounds.GetExtent();
            RecentCandidates.push_back(Entry);
        }
    }
}

