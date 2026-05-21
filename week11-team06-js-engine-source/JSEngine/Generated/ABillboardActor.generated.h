#pragma once

class ABillboardActor;

template<>
struct TIsUClassReflected<ABillboardActor>
{
    static constexpr bool Value = true;
};
