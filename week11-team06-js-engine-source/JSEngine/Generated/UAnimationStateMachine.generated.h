#pragma once

class UAnimationStateMachine;

template<>
struct TIsUClassReflected<UAnimationStateMachine>
{
    static constexpr bool Value = true;
};
