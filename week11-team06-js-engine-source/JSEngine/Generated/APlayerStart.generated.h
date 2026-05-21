#pragma once

class APlayerStart;

template<>
struct TIsUClassReflected<APlayerStart>
{
    static constexpr bool Value = true;
};
