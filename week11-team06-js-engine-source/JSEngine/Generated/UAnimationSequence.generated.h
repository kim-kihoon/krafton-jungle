#pragma once

class UAnimationSequence;

template<>
struct TIsUClassReflected<UAnimationSequence>
{
    static constexpr bool Value = true;
};
