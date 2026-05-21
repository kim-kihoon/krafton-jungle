#pragma once

class AFireballActor;

template<>
struct TIsUClassReflected<AFireballActor>
{
    static constexpr bool Value = true;
};
