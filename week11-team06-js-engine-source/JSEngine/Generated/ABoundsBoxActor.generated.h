#pragma once

class ABoundsBoxActor;

template<>
struct TIsUClassReflected<ABoundsBoxActor>
{
    static constexpr bool Value = true;
};
