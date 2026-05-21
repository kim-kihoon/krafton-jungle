#pragma once

class ADecalActor;

template<>
struct TIsUClassReflected<ADecalActor>
{
    static constexpr bool Value = true;
};
