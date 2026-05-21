#pragma once

class UActorSequencePlayer;

template<>
struct TIsUClassReflected<UActorSequencePlayer>
{
    static constexpr bool Value = true;
};
