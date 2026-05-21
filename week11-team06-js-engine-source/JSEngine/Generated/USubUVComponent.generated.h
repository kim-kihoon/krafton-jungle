#pragma once

class USubUVComponent;

template<>
struct TIsUClassReflected<USubUVComponent>
{
    static constexpr bool Value = true;
};
