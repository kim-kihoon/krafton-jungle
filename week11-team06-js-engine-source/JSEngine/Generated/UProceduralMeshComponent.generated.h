#pragma once

class UProceduralMeshComponent;

template<>
struct TIsUClassReflected<UProceduralMeshComponent>
{
    static constexpr bool Value = true;
};
