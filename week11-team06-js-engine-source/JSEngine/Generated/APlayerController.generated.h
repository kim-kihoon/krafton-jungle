#pragma once

class APlayerController;

template<>
struct TIsUClassReflected<APlayerController>
{
    static constexpr bool Value = true;
};
