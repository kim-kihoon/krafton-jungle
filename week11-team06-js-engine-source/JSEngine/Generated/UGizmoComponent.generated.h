#pragma once

class UGizmoComponent;

template<>
struct TIsUClassReflected<UGizmoComponent>
{
    static constexpr bool Value = true;
};
