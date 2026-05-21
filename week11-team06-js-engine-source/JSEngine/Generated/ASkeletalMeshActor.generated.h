#pragma once

class ASkeletalMeshActor;

template<>
struct TIsUClassReflected<ASkeletalMeshActor>
{
    static constexpr bool Value = true;
};
