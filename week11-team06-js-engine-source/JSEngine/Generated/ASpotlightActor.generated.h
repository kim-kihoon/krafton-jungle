#pragma once

class ASpotlightActor;

template<>
struct TIsUClassReflected<ASpotlightActor>
{
    static constexpr bool Value = true;
};
