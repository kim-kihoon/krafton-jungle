#pragma once

class AReflectionTestActor;

template<>
struct TIsUClassReflected<AReflectionTestActor>
{
    static constexpr bool Value = true;
};
