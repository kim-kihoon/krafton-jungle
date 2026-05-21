#pragma once

class UPointLightComponent;

template<>
struct TIsUClassReflected<UPointLightComponent>
{
    static constexpr bool Value = true;
};
