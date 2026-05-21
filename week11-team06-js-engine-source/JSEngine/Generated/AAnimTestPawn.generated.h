#pragma once

class AAnimTestPawn;

template<>
struct TIsUClassReflected<AAnimTestPawn>
{
    static constexpr bool Value = true;
};
