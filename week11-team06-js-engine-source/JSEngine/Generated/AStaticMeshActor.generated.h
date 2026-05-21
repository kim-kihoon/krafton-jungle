#pragma once

class AStaticMeshActor;

template<>
struct TIsUClassReflected<AStaticMeshActor>
{
    static constexpr bool Value = true;
};
