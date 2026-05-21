#pragma once

class UAnimStateMachineAsset;

template<>
struct TIsUClassReflected<UAnimStateMachineAsset>
{
    static constexpr bool Value = true;
};
