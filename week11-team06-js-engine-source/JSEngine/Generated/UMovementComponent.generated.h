#pragma once

class UMovementComponent;

template<>
struct TIsUClassReflected<UMovementComponent>
{
    static constexpr bool Value = true;
};
