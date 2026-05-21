#pragma once

class ACubeActor;

template<>
struct TIsUClassReflected<ACubeActor>
{
    static constexpr bool Value = true;
};
