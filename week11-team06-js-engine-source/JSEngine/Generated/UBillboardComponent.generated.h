#pragma once

class UBillboardComponent;

template<>
struct TIsUClassReflected<UBillboardComponent>
{
    static constexpr bool Value = true;
};
