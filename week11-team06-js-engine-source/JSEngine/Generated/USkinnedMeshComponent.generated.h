#pragma once

class USkinnedMeshComponent;

template<>
struct TIsUClassReflected<USkinnedMeshComponent>
{
    static constexpr bool Value = true;
};
