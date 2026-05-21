#pragma once

class UInterpToMovementComponent;

template<>
struct TIsUClassReflected<UInterpToMovementComponent>
{
    static constexpr bool Value = true;
};
