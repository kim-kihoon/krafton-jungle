#pragma once

class UShapeComponent;

template<>
struct TIsUClassReflected<UShapeComponent>
{
    static constexpr bool Value = true;
};
