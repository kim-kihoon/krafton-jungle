#pragma once

class UBoxComponent;

template<>
struct TIsUClassReflected<UBoxComponent>
{
    static constexpr bool Value = true;
};
