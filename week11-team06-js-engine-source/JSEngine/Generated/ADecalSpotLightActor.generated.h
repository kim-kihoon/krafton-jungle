#pragma once

class ADecalSpotLightActor;

template<>
struct TIsUClassReflected<ADecalSpotLightActor>
{
    static constexpr bool Value = true;
};
