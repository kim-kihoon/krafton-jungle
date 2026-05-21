#pragma once

class ASubUVActor;

template<>
struct TIsUClassReflected<ASubUVActor>
{
    static constexpr bool Value = true;
};
