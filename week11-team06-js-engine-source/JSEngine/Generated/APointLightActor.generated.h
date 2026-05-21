#pragma once

class APointLightActor;

template<>
struct TIsUClassReflected<APointLightActor>
{
    static constexpr bool Value = true;
};
