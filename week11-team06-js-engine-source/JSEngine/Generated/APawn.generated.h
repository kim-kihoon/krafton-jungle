#pragma once

class APawn;

template<>
struct TIsUClassReflected<APawn>
{
    static constexpr bool Value = true;
};
