#pragma once

class USphereComponent;

template<>
struct TIsUClassReflected<USphereComponent>
{
    static constexpr bool Value = true;
};
