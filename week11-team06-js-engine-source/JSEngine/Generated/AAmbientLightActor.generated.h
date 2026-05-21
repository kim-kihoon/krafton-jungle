#pragma once

class AAmbientLightActor;

template<>
struct TIsUClassReflected<AAmbientLightActor>
{
    static constexpr bool Value = true;
};
