#pragma once

class UCameraComponent;

template<>
struct TIsUClassReflected<UCameraComponent>
{
    static constexpr bool Value = true;
};
