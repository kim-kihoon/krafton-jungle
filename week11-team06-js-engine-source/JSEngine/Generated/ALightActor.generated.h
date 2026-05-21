#pragma once

class ALightActor;

template<>
struct TIsUClassReflected<ALightActor>
{
    static constexpr bool Value = true;
};
