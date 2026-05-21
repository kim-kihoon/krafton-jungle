#pragma once

class UActorSequenceComponent;

template<>
struct TIsUClassReflected<UActorSequenceComponent>
{
    static constexpr bool Value = true;
};
