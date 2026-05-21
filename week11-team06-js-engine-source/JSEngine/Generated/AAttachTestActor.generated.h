#pragma once

class AAttachTestActor;

template<>
struct TIsUClassReflected<AAttachTestActor>
{
    static constexpr bool Value = true;
};
