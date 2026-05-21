#include "Animation/AnimStateMachineAssetInstance.h"

#include "Animation/AnimStateMachineAsset.h"
#include "Core/Logging/Log.h"
#include "Core/ResourceManager.h"

void UAnimStateMachineAssetInstance::NativeInitializeAnimation()
{
    UAnimInstance::NativeInitializeAnimation();
    ReloadStateMachineAsset();
}

void UAnimStateMachineAssetInstance::SetStateMachineAssetRef(const FAnimStateMachineAssetRef& InAssetRef)
{
    StateMachineAsset = InAssetRef;
    ReloadStateMachineAsset();
}

bool UAnimStateMachineAssetInstance::ReloadStateMachineAsset()
{
    if (StateMachineAsset.Path.empty())
    {
        SetStateMachineAsset(nullptr);
        return true;
    }

    UAnimStateMachineAsset* LoadedAsset = FResourceManager::Get().LoadAnimStateMachineAsset(StateMachineAsset.Path);
    if (!LoadedAsset)
    {
        UE_LOG_WARNING("[AnimSM] Failed to load state machine asset instance path: %s", StateMachineAsset.Path.c_str());
        SetStateMachineAsset(nullptr);
        return false;
    }

    for (const FAnimStateDesc& State : LoadedAsset->GetStates())
    {
        if (State.AnimationPath.empty())
        {
            continue;
        }

        const FName AnimationKey(State.AnimationPath);
        if (!RegisterAnimationPath(AnimationKey, State.AnimationPath))
        {
            UE_LOG_WARNING(
                "[AnimSM] Failed to register state animation path: state=%s animation=%s path=%s",
                State.StateName.ToString().c_str(),
                AnimationKey.ToString().c_str(),
                State.AnimationPath.c_str());
        }
    }

    SetStateMachineAsset(LoadedAsset);
    return GetStateMachineAsset() == LoadedAsset;
}
