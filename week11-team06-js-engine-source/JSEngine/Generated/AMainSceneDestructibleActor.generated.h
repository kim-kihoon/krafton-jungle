#pragma once

class AMainSceneDestructibleActor;

template<>
struct TIsUClassReflected<AMainSceneDestructibleActor>
{
    static constexpr bool Value = true;
};
