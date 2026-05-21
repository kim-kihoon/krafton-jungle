#pragma once

class URotatingMovementComponent;

template<>
struct TIsUClassReflected<URotatingMovementComponent>
{
    static constexpr bool Value = true;
};
