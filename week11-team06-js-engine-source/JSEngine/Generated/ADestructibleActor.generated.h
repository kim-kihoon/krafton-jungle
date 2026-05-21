#pragma once

class ADestructibleActor;

template<>
struct TIsUClassReflected<ADestructibleActor>
{
    static constexpr bool Value = true;
};
