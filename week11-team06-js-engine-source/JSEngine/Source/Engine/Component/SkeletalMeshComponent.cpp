#include "SkeletalMeshComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimStateMachineAssetInstance.h"
#include "Animation/AnimStateMachineAsset.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Core/Logging/Log.h"
#include "Core/ResourceManager.h"
#include "GameFramework/AActor.h"
#include "Object/Class.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Object/ReflectionRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstring>

DEFINE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
REGISTER_FACTORY(USkeletalMeshComponent)

namespace
{
    const char* GAnimationModeNames[] = {
        "Single Animation",
        "AnimInstance"
    };

    const char* GAnimInstanceSourceNames[] = {
        "Empty State Machine",
        "Custom C++",
        "StateMachine Asset"
    };

    FString NormalizeAnimationModeName(FString Value)
    {
        Value.erase(
            std::remove_if(
                Value.begin(),
                Value.end(),
                [](unsigned char Ch)
                {
                    return Ch == ' ' || Ch == '_' || Ch == '-';
                }),
            Value.end());

        std::transform(
            Value.begin(),
            Value.end(),
            Value.begin(),
            [](unsigned char Ch)
            {
                return static_cast<char>(std::tolower(Ch));
            });
        return Value;
    }
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    DestroyOwnedAnimInstance(SingleNodeAnimInstance);
    DestroyOwnedAnimInstance(StateMachineAssetInstance);
    DestroyOwnedAnimInstance(CustomAnimInstance);
    DestroyOwnedAnimInstance(DefaultAnimInstance);
    AnimInstance = nullptr;
}

void USkeletalMeshComponent::PostDuplicate(UObject* Original)
{
    USkinnedMeshComponent::PostDuplicate(Original);

    const USkeletalMeshComponent* SourceComponent = Cast<USkeletalMeshComponent>(Original);
    if (!SourceComponent)
    {
        return;
    }

    AnimationMode = SourceComponent->AnimationMode;
    AnimInstanceSource = SourceComponent->AnimInstanceSource;
    AnimationSequence = SourceComponent->AnimationSequence;
    bSingleAnimationLoop = SourceComponent->bSingleAnimationLoop;
    SingleAnimationPlayRate = SourceComponent->SingleAnimationPlayRate;
    CustomAnimInstanceClassName = SourceComponent->CustomAnimInstanceClassName;
    StateMachineAsset = SourceComponent->StateMachineAsset;

    RefreshAnimInstanceFromOptions(true);
}

void USkeletalMeshComponent::BeginPlay()
{
    USkinnedMeshComponent::BeginPlay();

    if (AnimationMode == ESkeletalMeshAnimationMode::SingleAnimation && !AnimationSequence.Path.empty())
    {
        PlaySingleAnimation();
    }
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    USkinnedMeshComponent::TickComponent(DeltaTime);

    if (AnimInstance)
    {
        AnimInstance->TickAnimation(DeltaTime);
    }

	// Pose가 바뀐 경우에만 실제 CPU skinning이 수행(dirty flag 이용)
    EnsureSkinningUpdated();
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InAnimInstance)
{
    if (AnimInstance == InAnimInstance)
    {
        return;
    }

    AnimInstance = InAnimInstance;
    if (AnimInstance)
    {
        AnimInstance->Initialize(this);
    }
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    if (Ar.IsLoading())
    {
        int32 AnimationModeValue = static_cast<int32>(AnimationMode);
        int32 AnimInstanceSourceValue = static_cast<int32>(AnimInstanceSource);

        if (Ar.HasKey("AnimationMode"))
        {
            Ar << "AnimationMode" << AnimationModeValue;
        }
        if (Ar.HasKey("AnimInstanceSource"))
        {
            Ar << "AnimInstanceSource" << AnimInstanceSourceValue;
        }
        if (Ar.HasKey("AnimationSequence"))
        {
            Ar << "AnimationSequence" << AnimationSequence.Path;
        }
        if (Ar.HasKey("Loop"))
        {
            Ar << "Loop" << bSingleAnimationLoop;
        }
        if (Ar.HasKey("PlayRate"))
        {
            Ar << "PlayRate" << SingleAnimationPlayRate;
        }
        if (Ar.HasKey("AnimInstanceClassName"))
        {
            Ar << "AnimInstanceClassName" << CustomAnimInstanceClassName;
        }
        if (Ar.HasKey("StateMachineAsset"))
        {
            Ar << "StateMachineAsset" << StateMachineAsset.Path;
        }

        AnimationMode = static_cast<ESkeletalMeshAnimationMode>(AnimationModeValue);
        AnimInstanceSource = static_cast<EAnimInstanceSource>(AnimInstanceSourceValue);

        USkinnedMeshComponent::Serialize(Ar);
        RefreshAnimInstanceFromOptions(true);
        return;
    }

    USkinnedMeshComponent::Serialize(Ar);

    int32 AnimationModeValue = static_cast<int32>(AnimationMode);
    int32 AnimInstanceSourceValue = static_cast<int32>(AnimInstanceSource);

    Ar << "AnimationMode" << AnimationModeValue;
    Ar << "AnimInstanceSource" << AnimInstanceSourceValue;
    Ar << "AnimationSequence" << AnimationSequence.Path;
    Ar << "Loop" << bSingleAnimationLoop;
    Ar << "PlayRate" << SingleAnimationPlayRate;
    Ar << "AnimInstanceClassName" << CustomAnimInstanceClassName;
    Ar << "StateMachineAsset" << StateMachineAsset.Path;
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    USkinnedMeshComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "Animation Mode", EPropertyType::Enum, &AnimationMode, 0.0f, 0.0f, 0.0f, GAnimationModeNames, 2 });
    OutProps.push_back({ "AnimInstance Source", EPropertyType::Enum, &AnimInstanceSource, 0.0f, 0.0f, 0.0f, GAnimInstanceSourceNames, 3 });
    OutProps.push_back({ "Animation Sequence", EPropertyType::String, &AnimationSequence.Path });
    OutProps.push_back({ "Loop", EPropertyType::Bool, &bSingleAnimationLoop });
    OutProps.push_back({ "Play Rate", EPropertyType::Float, &SingleAnimationPlayRate, 0.01f, 10.0f, 0.05f });
    OutProps.push_back({ "AnimInstance Class Name", EPropertyType::String, &CustomAnimInstanceClassName });
    OutProps.push_back({ "StateMachine Asset", EPropertyType::String, &StateMachineAsset.Path });
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    USkinnedMeshComponent::PostEditProperty(PropertyName);

    if (!PropertyName)
    {
        return;
    }

    if (std::strcmp(PropertyName, "Animation Mode") == 0
        || std::strcmp(PropertyName, "AnimInstance Source") == 0
        || std::strcmp(PropertyName, "Animation Sequence") == 0
        || std::strcmp(PropertyName, "Loop") == 0
        || std::strcmp(PropertyName, "Play Rate") == 0
        || std::strcmp(PropertyName, "AnimInstance Class Name") == 0
        || std::strcmp(PropertyName, "StateMachine Asset") == 0)
    {
        RefreshAnimInstanceFromOptions(true);
    }
}

bool USkeletalMeshComponent::SetAnimationMode(ESkeletalMeshAnimationMode InAnimationMode)
{
    if (AnimationMode == InAnimationMode)
    {
        return RefreshAnimInstanceFromOptions(true);
    }

    AnimationMode = InAnimationMode;
    return RefreshAnimInstanceFromOptions(true);
}

bool USkeletalMeshComponent::SetAnimationModeByName(const FString& InAnimationMode)
{
    const FString Mode = NormalizeAnimationModeName(InAnimationMode);
    if (Mode == "singleanimation" || Mode == "single")
    {
        return SetAnimationMode(ESkeletalMeshAnimationMode::SingleAnimation);
    }
    if (Mode == "animinstance" || Mode == "instance" || Mode == "statemachine")
    {
        return SetAnimationMode(ESkeletalMeshAnimationMode::AnimInstance);
    }

    UE_LOG_WARNING("[SkeletalMeshComponent] Unsupported animation mode: %s", InAnimationMode.c_str());
    return false;
}

bool USkeletalMeshComponent::SetAnimInstanceSource(EAnimInstanceSource InAnimInstanceSource)
{
    if (AnimInstanceSource == InAnimInstanceSource)
    {
        return RefreshAnimInstanceFromOptions(true);
    }

    AnimInstanceSource = InAnimInstanceSource;
    AnimationMode = ESkeletalMeshAnimationMode::AnimInstance;
    return RefreshAnimInstanceFromOptions(true);
}

bool USkeletalMeshComponent::SetAnimationSequence(const FString& AnimationPath)
{
    AnimationSequence = FAnimationSequenceAssetRef(AnimationPath);
    AnimationMode = ESkeletalMeshAnimationMode::SingleAnimation;
    return RefreshAnimInstanceFromOptions(true);
}

UAnimationSequenceBase* USkeletalMeshComponent::GetAnimationSequence() const
{
    return SingleNodeAnimInstance ? SingleNodeAnimInstance->GetSequence() : nullptr;
}

void USkeletalMeshComponent::SetSingleAnimationLooping(bool bInLooping)
{
    bSingleAnimationLoop = bInLooping;
    if (SingleNodeAnimInstance)
    {
        SingleNodeAnimInstance->SetLooping(bSingleAnimationLoop);
    }
}

void USkeletalMeshComponent::SetSingleAnimationPlayRate(float InPlayRate)
{
    SingleAnimationPlayRate = InPlayRate;
    if (SingleAnimationPlayRate <= 0.0f)
    {
        SingleAnimationPlayRate = 1.0f;
    }

    if (SingleNodeAnimInstance)
    {
        SingleNodeAnimInstance->SetPlayRate(SingleAnimationPlayRate);
    }
}

bool USkeletalMeshComponent::PlaySingleAnimation()
{
    AnimationMode = ESkeletalMeshAnimationMode::SingleAnimation;
    if (!ActivateSingleAnimationInstance() || !SingleNodeAnimInstance)
    {
        return false;
    }

    SingleNodeAnimInstance->Play();
    return true;
}

void USkeletalMeshComponent::PauseSingleAnimation()
{
    if (SingleNodeAnimInstance)
    {
        SingleNodeAnimInstance->Pause();
    }
}

void USkeletalMeshComponent::StopSingleAnimation()
{
    if (SingleNodeAnimInstance)
    {
        SingleNodeAnimInstance->Stop();
    }
}

bool USkeletalMeshComponent::UseDefaultAnimInstance()
{
    AnimationMode = ESkeletalMeshAnimationMode::AnimInstance;
    AnimInstanceSource = EAnimInstanceSource::EmptyStateMachine;
    return RefreshAnimInstanceFromOptions(true);
}

bool USkeletalMeshComponent::UseStateMachine(UAnimStateMachineAsset* StateMachineAsset)
{
    if (!StateMachineAsset)
    {
        return false;
    }

    AnimationMode = ESkeletalMeshAnimationMode::AnimInstance;
    AnimInstanceSource = EAnimInstanceSource::EmptyStateMachine;
    UAnimInstance* StateAnimInstance = GetOrCreateDefaultAnimInstance();
    if (!StateAnimInstance)
    {
        return false;
    }

    SetAnimInstance(StateAnimInstance);
    StateAnimInstance->SetStateMachineAsset(StateMachineAsset);
    return StateAnimInstance->GetStateMachineAsset() == StateMachineAsset;
}

bool USkeletalMeshComponent::LoadStateMachineAsset(const FString& AssetPath)
{
    if (AssetPath.empty())
    {
        return false;
    }

    AnimationMode = ESkeletalMeshAnimationMode::AnimInstance;
    AnimInstanceSource = EAnimInstanceSource::StateMachineAsset;
    StateMachineAsset = FAnimStateMachineAssetRef(AssetPath);
    return RefreshAnimInstanceFromOptions(true);
}

void USkeletalMeshComponent::HandleAnimNotify(const FAnimNotifyEvent& Notify)
{
    AActor* OwnerActor = GetOwner();
    if (OwnerActor)
    {
        OwnerActor->HandleAnimNotify(Notify);
    }
}

UAnimInstance* USkeletalMeshComponent::GetOrCreateDefaultAnimInstance()
{
    if (!DefaultAnimInstance)
    {
        DefaultAnimInstance = UObjectManager::Get().CreateObject<UAnimInstance>();
        DefaultAnimInstance->SetLooping(true);
        DefaultAnimInstance->Initialize(this);
    }

    return DefaultAnimInstance;
}

bool USkeletalMeshComponent::RefreshAnimInstanceFromOptions(bool bKeepCurrentOnFailure)
{
    if (AnimationMode == ESkeletalMeshAnimationMode::SingleAnimation)
    {
        return ActivateSingleAnimationInstance();
    }

    switch (AnimInstanceSource)
    {
    case EAnimInstanceSource::EmptyStateMachine:
        return ActivateDefaultAnimInstance();
    case EAnimInstanceSource::CustomCpp:
        return ActivateCustomAnimInstance(bKeepCurrentOnFailure);
    case EAnimInstanceSource::StateMachineAsset:
        return ActivateStateMachineAssetInstance();
    default:
        return ActivateDefaultAnimInstance();
    }
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetOrCreateSingleNodeAnimInstance()
{
    if (!SingleNodeAnimInstance)
    {
        SingleNodeAnimInstance = UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>();
        SingleNodeAnimInstance->Initialize(this);
    }

    return SingleNodeAnimInstance;
}

bool USkeletalMeshComponent::ActivateSingleAnimationInstance()
{
    UAnimSingleNodeInstance* SingleInstance = GetOrCreateSingleNodeAnimInstance();
    if (!SingleInstance)
    {
        return false;
    }

    SingleInstance->SetSequence(LoadConfiguredAnimationSequence());
    SingleInstance->SetLooping(bSingleAnimationLoop);
    SingleInstance->SetPlayRate(SingleAnimationPlayRate > 0.0f ? SingleAnimationPlayRate : 1.0f);
    SetAnimInstance(SingleInstance);
    DestroyInactiveOwnedAnimInstances(SingleInstance);
    return true;
}

bool USkeletalMeshComponent::ActivateDefaultAnimInstance()
{
    UAnimInstance* StateAnimInstance = GetOrCreateDefaultAnimInstance();
    if (!StateAnimInstance)
    {
        return false;
    }

    SetAnimInstance(StateAnimInstance);
    DestroyInactiveOwnedAnimInstances(StateAnimInstance);
    return true;
}

bool USkeletalMeshComponent::ActivateCustomAnimInstance(bool bKeepCurrentOnFailure)
{
    if (CustomAnimInstanceClassName.empty())
    {
        return ActivateDefaultAnimInstance();
    }

    UClass* AnimClass = FReflectionRegistry::Get().FindClass(CustomAnimInstanceClassName);
    if (!AnimClass || !AnimClass->IsChildOf(UAnimInstance::StaticClass()))
    {
        UE_LOG_WARNING(
            "[SkeletalMeshComponent] Invalid AnimInstance class: %s",
            CustomAnimInstanceClassName.c_str());
        return bKeepCurrentOnFailure && AnimInstance;
    }

    if (CustomAnimInstance && CustomAnimInstance->GetClass() == AnimClass)
    {
        SetAnimInstance(CustomAnimInstance);
        DestroyInactiveOwnedAnimInstances(CustomAnimInstance);
        return true;
    }

    UObject* CreatedObject = AnimClass->CreateObject();
    UAnimInstance* CreatedAnimInstance = Cast<UAnimInstance>(CreatedObject);
    if (!CreatedAnimInstance)
    {
        if (CreatedObject)
        {
            UObjectManager::Get().DestroyObject(CreatedObject);
        }
        UE_LOG_WARNING(
            "[SkeletalMeshComponent] Failed to create AnimInstance class: %s",
            CustomAnimInstanceClassName.c_str());
        return bKeepCurrentOnFailure && AnimInstance;
    }

    DestroyOwnedAnimInstance(CustomAnimInstance);
    CustomAnimInstance = CreatedAnimInstance;
    SetAnimInstance(CustomAnimInstance);
    DestroyInactiveOwnedAnimInstances(CustomAnimInstance);
    return true;
}

bool USkeletalMeshComponent::ActivateStateMachineAssetInstance()
{
    UAnimStateMachineAssetInstance* StateAssetInstance = GetOrCreateStateMachineAssetInstance();
    if (!StateAssetInstance)
    {
        return false;
    }

    StateAssetInstance->SetStateMachineAssetRef(StateMachineAsset);
    SetAnimInstance(StateAssetInstance);
    DestroyInactiveOwnedAnimInstances(StateAssetInstance);
    return true;
}

UAnimStateMachineAssetInstance* USkeletalMeshComponent::GetOrCreateStateMachineAssetInstance()
{
    if (!StateMachineAssetInstance)
    {
        StateMachineAssetInstance = UObjectManager::Get().CreateObject<UAnimStateMachineAssetInstance>();
        StateMachineAssetInstance->Initialize(this);
    }

    return StateMachineAssetInstance;
}

bool USkeletalMeshComponent::IsComponentOwnedAnimInstance(const UAnimInstance* InAnimInstance) const
{
    return InAnimInstance
        && (InAnimInstance == DefaultAnimInstance
            || InAnimInstance == SingleNodeAnimInstance
            || InAnimInstance == CustomAnimInstance
            || InAnimInstance == StateMachineAssetInstance);
}

void USkeletalMeshComponent::DestroyOwnedAnimInstance(UAnimInstance*& InOutAnimInstance)
{
    if (!InOutAnimInstance)
    {
        return;
    }

    if (AnimInstance == InOutAnimInstance)
    {
        AnimInstance = nullptr;
    }

    UObjectManager::Get().DestroyObject(InOutAnimInstance);
    InOutAnimInstance = nullptr;
}

void USkeletalMeshComponent::DestroyOwnedAnimInstance(UAnimSingleNodeInstance*& InOutAnimInstance)
{
    if (!InOutAnimInstance)
    {
        return;
    }

    if (AnimInstance == InOutAnimInstance)
    {
        AnimInstance = nullptr;
    }

    UObjectManager::Get().DestroyObject(InOutAnimInstance);
    InOutAnimInstance = nullptr;
}

void USkeletalMeshComponent::DestroyOwnedAnimInstance(UAnimStateMachineAssetInstance*& InOutAnimInstance)
{
    if (!InOutAnimInstance)
    {
        return;
    }

    if (AnimInstance == InOutAnimInstance)
    {
        AnimInstance = nullptr;
    }

    UObjectManager::Get().DestroyObject(InOutAnimInstance);
    InOutAnimInstance = nullptr;
}

void USkeletalMeshComponent::DestroyInactiveOwnedAnimInstances(UAnimInstance* InstanceToKeep)
{
    if (DefaultAnimInstance != InstanceToKeep)
    {
        DestroyOwnedAnimInstance(DefaultAnimInstance);
    }
    if (SingleNodeAnimInstance != InstanceToKeep)
    {
        DestroyOwnedAnimInstance(SingleNodeAnimInstance);
    }
    if (CustomAnimInstance != InstanceToKeep)
    {
        DestroyOwnedAnimInstance(CustomAnimInstance);
    }
    if (StateMachineAssetInstance != InstanceToKeep)
    {
        DestroyOwnedAnimInstance(StateMachineAssetInstance);
    }
}

UAnimationSequenceBase* USkeletalMeshComponent::LoadConfiguredAnimationSequence() const
{
    if (AnimationSequence.Path.empty())
    {
        return nullptr;
    }

    return FResourceManager::Get().LoadAnimationSequence(AnimationSequence.Path);
}
void USkeletalMeshComponent::RegisterStateAnimationPathsFromAsset(const UAnimStateMachineAsset* StateMachineAsset)
{
    if (!StateMachineAsset)
    {
        return;
    }

    UAnimInstance* StateAnimInstance = GetOrCreateDefaultAnimInstance();
    if (!StateAnimInstance)
    {
        return;
    }

    for (const FAnimStateDesc& State : StateMachineAsset->GetStates())
    {
        if (!State.AnimationName.IsValid() || State.AnimationPath.empty())
        {
            continue;
        }

        if (!StateAnimInstance->RegisterAnimationPath(State.AnimationName, State.AnimationPath))
        {
            UE_LOG_WARNING(
                "[AnimSM] Failed to register state animation path: state=%s animation=%s path=%s",
                State.StateName.ToString().c_str(),
                State.AnimationName.ToString().c_str(),
                State.AnimationPath.c_str());
        }
    }
}

void USkeletalMeshComponent::ApplyLocalPose(const TArray<FMatrix>& InLocalPose)
{
    if (InLocalPose.size() != CurrentLocalPose.size())
    {
        return;
    }

    CurrentLocalPose = InLocalPose;
    UpdateCurrentGlobalPose();
    MarkSkinningDirty();
}

void USkeletalMeshComponent::ResetToBindPose()
{
    InitializePoseFromBindPose();
    MarkSkinningDirty();
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FMatrix& NewLocalTransform)
{
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CurrentLocalPose.size()))
    {
        return;
    }

    CurrentLocalPose[BoneIndex] = NewLocalTransform;
    UpdateCurrentGlobalPose();
    MarkSkinningDirty();
}

const FMatrix& USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	// fallback은 identity
    static const FMatrix Identity = FMatrix::Identity;

    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CurrentLocalPose.size()))
    {
        return Identity;
    }

    return CurrentLocalPose[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneGlobalTransform(int32 BoneIndex) const
{
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CurrentGlobalPose.size()))
    {
        return FMatrix::Identity;
    }

    return CurrentGlobalPose[BoneIndex] * GetWorldMatrix();
}

void USkeletalMeshComponent::SetBoneGlobalTransform(int32 BoneIndex, const FMatrix& NewGlobalTransform)
{
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CurrentLocalPose.size()) || !SkeletalMesh)
    {
        return;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMesh->GetBones();
    if (BoneIndex >= static_cast<int32>(Bones.size()))
    {
        return;
    }

    int32 ParentIndex = Bones[BoneIndex].ParentIndex;

    FMatrix ParentGlobalTransform;
    if (ParentIndex >= 0)
    {
        ParentGlobalTransform = CurrentGlobalPose[ParentIndex] * GetWorldMatrix();
    }
    else
    {
        ParentGlobalTransform = GetWorldMatrix();
    }

    // Local = Global * ParentGlobal.Inverse
    FMatrix NewLocalTransform = NewGlobalTransform * ParentGlobalTransform.GetInverse();
    SetBoneLocalTransform(BoneIndex, NewLocalTransform);
}
