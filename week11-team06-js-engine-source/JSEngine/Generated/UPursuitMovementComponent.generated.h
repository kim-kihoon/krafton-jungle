#pragma once

class UPursuitMovementComponent;

template<>
struct TIsUClassReflected<UPursuitMovementComponent>
{
    static constexpr bool Value = true;
};
