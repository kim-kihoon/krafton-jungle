#pragma once

#include "Animation/CurvePlayback.h"
#include "Core/CoreMinimal.h"
#include "Core/Guid.h"
#include "Core/PropertyTypes.h"
#include "Object/Object.h"

class UActorComponent;
class UActorSequenceComponent;
class UCurveFloatAsset;

enum class ESequencePlayerContext
{
    Runtime,
    EditorPreview
};

enum class EActorSequenceTrackType
{
    Bool,
    Int,
    Float,
    Vec3,
    Vec4,
    Color,
    Transform
};

struct FSequenceObjectBinding
{
    FGuid BindingGuid;
    FGuid TargetObjectGuid;
    FString TargetObjectName;
};

struct FActorSequenceChannel
{
    FString ChannelName = "Value";
    FSequenceCurvePlaybackDesc Playback;
};

struct FActorSequenceSection
{
    FGuid SectionGuid;
    float StartTime = 0.0f;
    float Duration = 1.0f;
    float PlayRate = 1.0f;
    bool bLoop = false;
    TArray<FActorSequenceChannel> Channels;
};

struct FActorSequenceTrack
{
    FGuid TrackGuid;
    FString PropertyPath;
    EActorSequenceTrackType TrackType = EActorSequenceTrackType::Float;
    TArray<FActorSequenceSection> Sections;
};

struct FActorSequenceBinding
{
    FSequenceObjectBinding Binding;
    TArray<FActorSequenceTrack> Tracks;
};

struct FActorSequenceFloatTrackDesc
{
    FString TargetObjectName;
    FString TargetPropertyPath;
    FString CurveAssetPath;
    UCurveFloatAsset* Curve = nullptr;

    float StartTime = 0.0f;
    float Duration = 1.0f;
    float PlayRate = 1.0f;

    bool bLoop = false;

    ECurveApplyMode ApplyMode = ECurveApplyMode::Absolute;
    ECurveTimeMappingMode TimeMappingMode = ECurveTimeMappingMode::NormalizedTime;
};

struct FResolvedActorSequenceTrack
{
    FActorSequenceBinding* SourceBinding = nullptr;
    FActorSequenceTrack* SourceTrack = nullptr;
    FActorSequenceSection* SourceSection = nullptr;
    FActorSequenceChannel* SourceChannel = nullptr;
    UObject* ResolvedObject = nullptr;
    UCurveFloatAsset* ResolvedCurve = nullptr;
    FPropertyDescriptor ResolvedProperty;
    float BaseFloatValue = 0.0f;
    float LastValue = 0.0f;
    bool bHasBaseValue = false;
    bool bValid = false;
};

#include "UActorSequence.generated.h"
#include "UActorSequencePlayer.generated.h"

UCLASS()
class UActorSequence : public UObject
{
public:
    GENERATED_BODY(UActorSequence, UObject)

    UPROPERTY(Edit, LuaRead, LuaWrite)
    float StartTime = 0.0f;

    UPROPERTY(Edit, LuaRead, LuaWrite)
    float Duration = 1.0f;

    UPROPERTY(Edit, LuaRead, LuaWrite)
    bool bLoop = false;

    TArray<FActorSequenceBinding> Bindings;

    void Serialize(FArchive& Ar) override;
};

UCLASS()
class UActorSequencePlayer : public UObject
{
public:
    GENERATED_BODY(UActorSequencePlayer, UObject)

    void Initialize(
        UActorSequenceComponent* InOwner,
        UActorSequence* InSequence,
        ESequencePlayerContext InContext);

    void Play();
    void Pause();
    void Stop();
    void Tick(float DeltaTime);

    void SetCurrentTime(float InCurrentTime);
    float GetCurrentTime() const { return CurrentTime; }
    bool IsPlaying() const { return bPlaying; }

    void SetPlayRate(float InPlayRate) { PlayRate = InPlayRate; }
    float GetPlayRate() const { return PlayRate; }
    void SetPauseAtEnd(bool bInPauseAtEnd) { bPauseAtEnd = bInPauseAtEnd; }
    bool ShouldPauseAtEnd() const { return bPauseAtEnd; }
    void SetStartOffset(float InStartOffsetSeconds);
    float GetStartOffset() const { return StartOffsetSeconds; }

    void MarkResolveDirty();

private:
    void ResolveTracks();
    void Evaluate(float SequenceTime);
    void ClearAppliedValues();

    bool IsOwnerLive() const;
    bool IsResolvedTrackLive(const FResolvedActorSequenceTrack& Resolved) const;
    UObject* ResolveObject(const FSequenceObjectBinding& Binding) const;
    bool ResolveProperty(UObject* Object, const FActorSequenceTrack& Track, const FActorSequenceChannel& Channel, FPropertyDescriptor& OutProperty) const;
    bool CacheBaseValue(FResolvedActorSequenceTrack& Resolved) const;
    void ApplyFloat(FResolvedActorSequenceTrack& Resolved, float CurveValue);

private:
    UPROPERTY(Transient, Read)
    UActorSequenceComponent* OwnerComponent = nullptr;

    UPROPERTY(Transient, Read)
    UActorSequence* Sequence = nullptr;

    ESequencePlayerContext Context = ESequencePlayerContext::Runtime;

    UPROPERTY(Edit, LuaRead, LuaWrite)
    float CurrentTime = 0.0f;

    UPROPERTY(Edit, LuaRead, LuaWrite)
    float PlayRate = 1.0f;

    UPROPERTY(Edit, LuaRead, LuaWrite)
    float StartOffsetSeconds = 0.0f;

    UPROPERTY(Read, LuaRead)
    bool bPlaying = false;

    UPROPERTY(Read, LuaRead)
    bool bPaused = false;

    UPROPERTY(Edit, LuaRead, LuaWrite)
    bool bPauseAtEnd = false;

    UPROPERTY(Transient, Read)
    bool bResolveDirty = true;

    TArray<FResolvedActorSequenceTrack> ResolvedTracks;
};
