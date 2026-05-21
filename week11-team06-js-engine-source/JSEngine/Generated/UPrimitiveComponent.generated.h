#pragma once

class UPrimitiveComponent;

template<>
struct TIsUClassReflected<UPrimitiveComponent>
{
    static constexpr bool Value = true;
};
