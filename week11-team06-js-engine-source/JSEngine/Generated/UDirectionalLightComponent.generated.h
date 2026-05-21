#pragma once

class UDirectionalLightComponent;

template<>
struct TIsUClassReflected<UDirectionalLightComponent>
{
    static constexpr bool Value = true;
};
