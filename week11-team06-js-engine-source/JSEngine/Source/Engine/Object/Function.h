#pragma once
#include "Core/CoreMinimal.h"
#include <functional>
#include <memory>
#include <utility>

class UObject;

using FNativeFunction = std::function<void(UObject*)>;
enum class EFunctionFlags : uint32
{
    None = 0,
    Native = 1 << 0,      // C++ 구현 함수
    EditorCall = 1 << 1,  // 에디터 UI에서 호출 가능
    LuaCallable = 1 << 2, // Lua에서 호출 가능
};

inline EFunctionFlags operator|(EFunctionFlags Lhs, EFunctionFlags Rhs)
{
    return static_cast<EFunctionFlags>(
        static_cast<uint32>(Lhs) | static_cast<uint32>(Rhs));
}

inline bool HasFunctionFlag(EFunctionFlags Value, EFunctionFlags Flag)
{
    return (static_cast<uint32>(Value) & static_cast<uint32>(Flag)) != 0;
}


class UFunction
{
public:
    UFunction(
        const char* InName,
        EFunctionFlags InFlags,
        FNativeFunction InNativeFunction)
        : Name(InName), Flags(InFlags), NativeFunction(std::move(InNativeFunction))
    {
    }

    const char* GetName() const { return Name; }
    EFunctionFlags GetFlags() const { return Flags; }
    bool Invoke(UObject* Object) const;

private:
    const char* Name = nullptr;
    EFunctionFlags Flags = EFunctionFlags::None;
    FNativeFunction NativeFunction = nullptr;
};

inline std::unique_ptr<UFunction> MakeFunction(
    const char* Name,
    EFunctionFlags Flags,
    FNativeFunction NativeFunction)
{
    return std::make_unique<UFunction>(
        Name,
        Flags,
        std::move(NativeFunction));
}

inline std::unique_ptr<UFunction> MakeFunction(UFunction&& Function)
{
    return std::make_unique<UFunction>(std::move(Function));
}
