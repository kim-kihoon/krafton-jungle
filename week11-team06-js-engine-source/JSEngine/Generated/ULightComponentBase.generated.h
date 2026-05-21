#pragma once

class ULightComponentBase;

template<>
struct TIsUClassReflected<ULightComponentBase>
{
    static constexpr bool Value = true;
};
