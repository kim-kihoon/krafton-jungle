#pragma once

class AGameModeBase;

template<>
struct TIsUClassReflected<AGameModeBase>
{
    static constexpr bool Value = true;
};
