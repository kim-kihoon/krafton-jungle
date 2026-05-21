#pragma once

class UAnimationSequenceBase;

template<>
struct TIsUClassReflected<UAnimationSequenceBase>
{
    static constexpr bool Value = true;
};
