#pragma once

class UAnimInstance;

template<>
struct TIsUClassReflected<UAnimInstance>
{
    static constexpr bool Value = true;
};
