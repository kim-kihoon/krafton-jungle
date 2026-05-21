#pragma once

class ABullet;

template<>
struct TIsUClassReflected<ABullet>
{
    static constexpr bool Value = true;
};
