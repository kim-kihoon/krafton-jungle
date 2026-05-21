#pragma once

class ASphereActor;

template<>
struct TIsUClassReflected<ASphereActor>
{
    static constexpr bool Value = true;
};
