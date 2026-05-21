#pragma once

class ATextRenderActor;

template<>
struct TIsUClassReflected<ATextRenderActor>
{
    static constexpr bool Value = true;
};
