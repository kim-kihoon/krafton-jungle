#pragma once

class ASceneActor;

template<>
struct TIsUClassReflected<ASceneActor>
{
    static constexpr bool Value = true;
};
