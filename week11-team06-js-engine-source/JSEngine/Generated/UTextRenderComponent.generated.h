#pragma once

class UTextRenderComponent;

template<>
struct TIsUClassReflected<UTextRenderComponent>
{
    static constexpr bool Value = true;
};
