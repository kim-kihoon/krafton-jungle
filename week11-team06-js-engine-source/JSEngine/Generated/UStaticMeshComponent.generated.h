#pragma once

class UStaticMeshComponent;

template<>
struct TIsUClassReflected<UStaticMeshComponent>
{
    static constexpr bool Value = true;
};
