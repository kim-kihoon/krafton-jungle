#pragma once

class ABladeSlash;

template<>
struct TIsUClassReflected<ABladeSlash>
{
    static constexpr bool Value = true;
};
