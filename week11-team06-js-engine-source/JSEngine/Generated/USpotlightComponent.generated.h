#pragma once

class USpotlightComponent;

template<>
struct TIsUClassReflected<USpotlightComponent>
{
    static constexpr bool Value = true;
};
