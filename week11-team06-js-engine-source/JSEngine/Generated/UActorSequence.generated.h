#pragma once

class UActorSequence;

template<>
struct TIsUClassReflected<UActorSequence>
{
    static constexpr bool Value = true;
};
