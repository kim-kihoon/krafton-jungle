#pragma once

class UFireballComponent;

template<>
struct TIsUClassReflected<UFireballComponent>
{
    static constexpr bool Value = true;
};
