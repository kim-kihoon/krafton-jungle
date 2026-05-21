#pragma once

class ULightComponent;

template<>
struct TIsUClassReflected<ULightComponent>
{
    static constexpr bool Value = true;
};
