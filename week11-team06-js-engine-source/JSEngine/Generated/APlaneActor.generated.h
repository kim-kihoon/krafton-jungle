#pragma once

class APlaneActor;

template<>
struct TIsUClassReflected<APlaneActor>
{
    static constexpr bool Value = true;
};
