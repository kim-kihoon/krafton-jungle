#pragma once

class USkeletalMeshComponent;

template<>
struct TIsUClassReflected<USkeletalMeshComponent>
{
    static constexpr bool Value = true;
};
