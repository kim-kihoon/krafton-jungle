#pragma once

class UDecalComponent;

template<>
struct TIsUClassReflected<UDecalComponent>
{
    static constexpr bool Value = true;
};
