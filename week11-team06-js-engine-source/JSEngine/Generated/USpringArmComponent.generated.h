#pragma once

class USpringArmComponent;

template<>
struct TIsUClassReflected<USpringArmComponent>
{
    static constexpr bool Value = true;
};
