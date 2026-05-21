#pragma once

class AFogActor;

template<>
struct TIsUClassReflected<AFogActor>
{
    static constexpr bool Value = true;
};
