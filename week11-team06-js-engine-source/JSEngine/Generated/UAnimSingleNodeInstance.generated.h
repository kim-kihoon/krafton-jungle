#pragma once

class UAnimSingleNodeInstance;

template<>
struct TIsUClassReflected<UAnimSingleNodeInstance>
{
    static constexpr bool Value = true;
};
