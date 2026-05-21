#pragma once

class UMainSceneDestructibleComponent;

template<>
struct TIsUClassReflected<UMainSceneDestructibleComponent>
{
    static constexpr bool Value = true;
};
