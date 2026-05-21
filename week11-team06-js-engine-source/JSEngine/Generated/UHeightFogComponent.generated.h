#pragma once

class UHeightFogComponent;

template<>
struct TIsUClassReflected<UHeightFogComponent>
{
    static constexpr bool Value = true;
};
