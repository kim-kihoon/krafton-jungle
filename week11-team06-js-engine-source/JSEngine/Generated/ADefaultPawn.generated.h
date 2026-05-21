#pragma once

class ADefaultPawn;

template<>
struct TIsUClassReflected<ADefaultPawn>
{
    static constexpr bool Value = true;
};
