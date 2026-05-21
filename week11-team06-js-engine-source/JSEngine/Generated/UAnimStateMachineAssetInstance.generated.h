#pragma once

class UAnimStateMachineAssetInstance;

template<>
struct TIsUClassReflected<UAnimStateMachineAssetInstance>
{
    static constexpr bool Value = true;
};
