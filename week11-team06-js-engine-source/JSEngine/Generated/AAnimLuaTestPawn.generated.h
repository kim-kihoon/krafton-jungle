#pragma once

class AAnimLuaTestPawn;

template<>
struct TIsUClassReflected<AAnimLuaTestPawn>
{
    static constexpr bool Value = true;
};
