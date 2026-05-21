#pragma once

class UAmbientLightComponent;

template<>
struct TIsUClassReflected<UAmbientLightComponent>
{
    static constexpr bool Value = true;
};
