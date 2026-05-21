#pragma once

class UCapsuleComponent;

template<>
struct TIsUClassReflected<UCapsuleComponent>
{
    static constexpr bool Value = true;
};
