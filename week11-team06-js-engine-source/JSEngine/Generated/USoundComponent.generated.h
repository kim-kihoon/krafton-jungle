#pragma once

class USoundComponent;

template<>
struct TIsUClassReflected<USoundComponent>
{
    static constexpr bool Value = true;
};
