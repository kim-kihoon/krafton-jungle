#pragma once

#include "Animation/AnimInstance.h"
#include "Object/Property.h"

#include "UAnimStateMachineAssetInstance.generated.h"

UCLASS()
class UAnimStateMachineAssetInstance : public UAnimInstance
{
public:
    GENERATED_BODY(UAnimStateMachineAssetInstance, UAnimInstance)

    void NativeInitializeAnimation() override;

    void SetStateMachineAssetRef(const FAnimStateMachineAssetRef& InAssetRef);
    const FAnimStateMachineAssetRef& GetStateMachineAssetRef() const { return StateMachineAsset; }
    bool ReloadStateMachineAsset();

public:
    UPROPERTY(Edit, Read, Write, LuaRead, LuaWrite)
    FAnimStateMachineAssetRef StateMachineAsset;
};
