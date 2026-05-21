#pragma once

class ADirectionalLightActor;

template<>
struct TIsUClassReflected<ADirectionalLightActor>
{
    static constexpr bool Value = true;
};
