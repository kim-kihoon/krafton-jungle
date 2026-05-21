#pragma once

class UMeshComponent;

template<>
struct TIsUClassReflected<UMeshComponent>
{
    static constexpr bool Value = true;
};
