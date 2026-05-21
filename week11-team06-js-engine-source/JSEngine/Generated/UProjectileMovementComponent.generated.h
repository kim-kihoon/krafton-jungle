#pragma once

class UProjectileMovementComponent;

template<>
struct TIsUClassReflected<UProjectileMovementComponent>
{
    static constexpr bool Value = true;
};
