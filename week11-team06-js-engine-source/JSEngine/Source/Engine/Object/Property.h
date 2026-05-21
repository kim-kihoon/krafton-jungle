#pragma once

#include "Object/FName.h"
#include "Object/ObjectMacros.h"
#include "Serialization/Archive.h"

#include <memory>
#include <type_traits>

class UObject;
class UClass;

struct FArrayPropertyOps
{
    size_t (*Num)(void* Array);
    void (*Resize)(void* Array, size_t NewSize);
    void (*RemoveAt)(void* Array, size_t Index);
    void* (*GetElementPtr)(void* Array, size_t Index);
    const void* (*GetElementPtrConst)(const void* Array, size_t Index);
};

template <typename T>
static const FArrayPropertyOps* GetArrayPropertyOps()
{
    static const FArrayPropertyOps Ops = {
        [](void* Array) -> size_t
        {
            return static_cast<TArray<T>*>(Array)->size();
        },
        [](void* Array, size_t NewSize)
        {
            static_cast<TArray<T>*>(Array)->resize(NewSize);
        },
        [](void* Array, size_t Index)
        {
            auto* TypedArray = static_cast<TArray<T>*>(Array);
            if (Index < TypedArray->size())
            {
                TypedArray->erase(TypedArray->begin() + Index);
            }
        },
        [](void* Array, size_t Index) -> void*
        {
            auto* TypedArray = static_cast<TArray<T>*>(Array);
            return Index < TypedArray->size() ? &(*TypedArray)[Index] : nullptr;
        },
        [](const void* Array, size_t Index) -> const void*
        {
            const auto* TypedArray = static_cast<const TArray<T>*>(Array);
            return Index < TypedArray->size() ? &(*TypedArray)[Index] : nullptr;
        }
    };

    return &Ops;
}

struct FAssetReference
{
    FString Path;

    FAssetReference() = default;
    explicit FAssetReference(const FString& InPath)
        : Path(InPath)
    {
    }

    bool IsEmpty() const { return Path.empty(); }
    const FString& ToString() const { return Path; }
};

struct FStaticMeshAssetRef : FAssetReference { using FAssetReference::FAssetReference; };
struct FSkeletalMeshAssetRef : FAssetReference { using FAssetReference::FAssetReference; };
struct FTextureAssetRef : FAssetReference { using FAssetReference::FAssetReference; };
struct FMaterialAssetRef : FAssetReference { using FAssetReference::FAssetReference; };
struct FAnimationSequenceAssetRef : FAssetReference { using FAssetReference::FAssetReference; };
struct FAnimStateMachineAssetRef : FAssetReference { using FAssetReference::FAssetReference; };
struct FCurveAssetRef : FAssetReference { using FAssetReference::FAssetReference; };
struct FSceneAssetRef : FAssetReference { using FAssetReference::FAssetReference; };
struct FSoundAssetRef : FAssetReference { using FAssetReference::FAssetReference; };

enum class EPropertyFlags : uint32
{
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Edit = 1 << 2,
    Transient = 1 << 3,
    LuaRead = 1 << 4,
    LuaWrite = 1 << 5,
};

inline EPropertyFlags operator|(EPropertyFlags Lhs, EPropertyFlags Rhs)
{
    return static_cast<EPropertyFlags>(
        static_cast<uint32>(Lhs) | static_cast<uint32>(Rhs));
}

inline bool HasPropertyFlag(EPropertyFlags Value, EPropertyFlags Flag)
{
    return (static_cast<uint32>(Value) & static_cast<uint32>(Flag)) != 0;
}

struct FProperty
{
    FProperty() = default;
    FProperty(const char* InName, size_t InOffset, size_t InSize, EPropertyFlags InFlags)
        : Name(InName)
        , Offset(InOffset)
        , Size(InSize)
        , Flags(InFlags)
    {
    }

    virtual ~FProperty() = default;

    const char* Name = nullptr;
    size_t Offset = 0;
    size_t Size = 0;
    EPropertyFlags Flags = EPropertyFlags::None;

    virtual size_t GetValueSize() const = 0;
    virtual void SerializeValue(FArchive& Ar, void* ValuePtr) const = 0;

    void SerializeItem(FArchive& Ar, UObject* Container) const;

    void* ContainerPtrToRawValuePtr(void* Container) const
    {
        return Container ? reinterpret_cast<uint8*>(Container) + Offset : nullptr;
    }

    const void* ContainerPtrToRawValuePtr(const void* Container) const
    {
        return Container ? reinterpret_cast<const uint8*>(Container) + Offset : nullptr;
    }

    template <typename T>
    bool IsCompatibleValueType() const;

    template <typename T>
    T* ContainerPtrToValuePtr(UObject* Container) const;

    template <typename T>
    const T* ContainerPtrToValuePtr(const UObject* Container) const;

    template <typename T>
    bool GetPropertyValue_InContainer(const UObject* Container, T& OutValue) const;

    template <typename T>
    bool SetPropertyValue_InContainer(UObject* Container, const T& Value) const;
};

template <typename ValueType>
class TPlainProperty : public FProperty
{
public:
    using FProperty::FProperty;

    static constexpr size_t StaticValueSize = sizeof(ValueType);

    size_t GetValueSize() const override
    {
        return sizeof(ValueType);
    }

    void SerializeValue(FArchive& Ar, void* ValuePtr) const override
    {
        if (ValuePtr)
        {
            Ar << *static_cast<ValueType*>(ValuePtr);
        }
    }
};

template <typename AssetRefType>
class TAssetPathProperty : public FProperty
{
public:
    using FProperty::FProperty;

    static constexpr size_t StaticValueSize = sizeof(AssetRefType);

    size_t GetValueSize() const override
    {
        return sizeof(AssetRefType);
    }

    void SerializeValue(FArchive& Ar, void* ValuePtr) const override
    {
        if (ValuePtr)
        {
            Ar << static_cast<AssetRefType*>(ValuePtr)->Path;
        }
    }
};

using FBoolProperty = TPlainProperty<bool>;
using FInt32Property = TPlainProperty<int32>;
using FFloatProperty = TPlainProperty<float>;
using FNameProperty = TPlainProperty<FName>;
using FStringProperty = TPlainProperty<FString>;

using FStaticMeshAssetProperty = TAssetPathProperty<FStaticMeshAssetRef>;
using FSkeletalMeshAssetProperty = TAssetPathProperty<FSkeletalMeshAssetRef>;
using FTextureAssetProperty = TAssetPathProperty<FTextureAssetRef>;
using FMaterialAssetProperty = TAssetPathProperty<FMaterialAssetRef>;
using FAnimationSequenceAssetProperty = TAssetPathProperty<FAnimationSequenceAssetRef>;
using FAnimStateMachineAssetProperty = TAssetPathProperty<FAnimStateMachineAssetRef>;
using FCurveAssetProperty = TAssetPathProperty<FCurveAssetRef>;
using FSceneAssetProperty = TAssetPathProperty<FSceneAssetRef>;
using FSoundAssetProperty = TAssetPathProperty<FSoundAssetRef>;

class FObjectProperty : public FProperty
{
public:
    static constexpr size_t StaticValueSize = sizeof(UObject*);

    FObjectProperty() = default;
    FObjectProperty(const char* InName, size_t InOffset, size_t InSize, EPropertyFlags InFlags, UClass* InObjectClass)
        : FProperty(InName, InOffset, InSize, InFlags)
        , ObjectClass(InObjectClass)
    {
    }

    size_t GetValueSize() const override
    {
        return sizeof(UObject*);
    }

    UClass* GetObjectClass() const
    {
        return ObjectClass;
    }

    void SerializeValue(FArchive& Ar, void* ValuePtr) const override;

private:
    UClass* ObjectClass = nullptr;
};

class FArrayProperty : public FProperty
{
public:
    FArrayProperty() = default;
    FArrayProperty(
        const char* InName,
        size_t InOffset,
        size_t InSize,
        EPropertyFlags InFlags,
        std::unique_ptr<FProperty> InInnerProperty,
        const FArrayPropertyOps* InArrayOps)
        : FProperty(InName, InOffset, InSize, InFlags)
        , InnerProperty(std::move(InInnerProperty))
        , ArrayOps(InArrayOps)
    {
    }

    size_t GetValueSize() const override
    {
        return Size;
    }

    const FProperty* GetInnerProperty() const
    {
        return InnerProperty.get();
    }

    size_t GetArrayNum(UObject* Container) const;
    bool Resize(UObject* Container, size_t NewSize) const;
    bool RemoveArrayElement(UObject* Container, size_t Index) const;

    template <typename T>
    T* GetArrayElementPtr(UObject* Container, size_t Index) const;

    template <typename T>
    const T* GetArrayElementPtr(const UObject* Container, size_t Index) const;

    void SerializeValue(FArchive& Ar, void* ValuePtr) const override;

private:
    std::unique_ptr<FProperty> InnerProperty;
    const FArrayPropertyOps* ArrayOps = nullptr;
};

template <typename PropertyType>
std::unique_ptr<PropertyType> MakeProperty(
    const char* Name,
    size_t Offset,
    size_t Size,
    EPropertyFlags Flags)
{
    return std::make_unique<PropertyType>(Name, Offset, Size, Flags);
}

std::unique_ptr<FObjectProperty> MakeObjectProperty(
    const char* Name,
    size_t Offset,
    size_t Size,
    EPropertyFlags Flags,
    UClass* ObjectClass);

template <typename InnerPropertyType>
std::unique_ptr<FArrayProperty> MakeArrayProperty(
    const char* Name,
    size_t Offset,
    size_t Size,
    EPropertyFlags Flags,
    const FArrayPropertyOps* ArrayOps,
    UClass* InnerObjectClass = nullptr)
{
    std::unique_ptr<FProperty> InnerProperty;
    if constexpr (std::is_same_v<InnerPropertyType, FObjectProperty>)
    {
        InnerProperty = std::make_unique<FObjectProperty>(
            "Element",
            0,
            FObjectProperty::StaticValueSize,
            Flags,
            InnerObjectClass);
    }
    else
    {
        InnerProperty = std::make_unique<InnerPropertyType>(
            "Element",
            0,
            InnerPropertyType::StaticValueSize,
            Flags);
    }

    return std::make_unique<FArrayProperty>(
        Name,
        Offset,
        Size,
        Flags,
        std::move(InnerProperty),
        ArrayOps);
}

template <typename T>
bool FProperty::IsCompatibleValueType() const
{
    using RawT = std::remove_cv_t<T>;

    if constexpr (std::is_pointer_v<RawT> && std::is_convertible_v<RawT, UObject*>)
    {
        return dynamic_cast<const FObjectProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else if constexpr (std::is_same_v<RawT, FStaticMeshAssetRef>)
    {
        return dynamic_cast<const FStaticMeshAssetProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else if constexpr (std::is_same_v<RawT, FSkeletalMeshAssetRef>)
    {
        return dynamic_cast<const FSkeletalMeshAssetProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else if constexpr (std::is_same_v<RawT, FTextureAssetRef>)
    {
        return dynamic_cast<const FTextureAssetProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else if constexpr (std::is_same_v<RawT, FMaterialAssetRef>)
    {
        return dynamic_cast<const FMaterialAssetProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else if constexpr (std::is_same_v<RawT, FAnimationSequenceAssetRef>)
    {
        return dynamic_cast<const FAnimationSequenceAssetProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else if constexpr (std::is_same_v<RawT, FAnimStateMachineAssetRef>)
    {
        return dynamic_cast<const FAnimStateMachineAssetProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else if constexpr (std::is_same_v<RawT, FCurveAssetRef>)
    {
        return dynamic_cast<const FCurveAssetProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else if constexpr (std::is_same_v<RawT, FSceneAssetRef>)
    {
        return dynamic_cast<const FSceneAssetProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else if constexpr (std::is_same_v<RawT, FSoundAssetRef>)
    {
        return dynamic_cast<const FSoundAssetProperty*>(this) != nullptr && Size == sizeof(RawT);
    }
    else
    {
        return dynamic_cast<const TPlainProperty<RawT>*>(this) != nullptr && Size == sizeof(RawT);
    }
}

template <typename T>
T* FProperty::ContainerPtrToValuePtr(UObject* Container) const
{
    if (!Container || !IsCompatibleValueType<T>())
    {
        return nullptr;
    }

    return static_cast<T*>(ContainerPtrToRawValuePtr(Container));
}

template <typename T>
const T* FProperty::ContainerPtrToValuePtr(const UObject* Container) const
{
    if (!Container || !IsCompatibleValueType<T>())
    {
        return nullptr;
    }

    return static_cast<const T*>(ContainerPtrToRawValuePtr(Container));
}

template <typename T>
bool FProperty::GetPropertyValue_InContainer(const UObject* Container, T& OutValue) const
{
    if (!HasPropertyFlag(Flags, EPropertyFlags::Read))
    {
        return false;
    }

    const T* ValuePtr = ContainerPtrToValuePtr<T>(Container);
    if (!ValuePtr)
    {
        return false;
    }

    OutValue = *ValuePtr;
    return true;
}

template <typename T>
bool FProperty::SetPropertyValue_InContainer(UObject* Container, const T& Value) const
{
    if (!HasPropertyFlag(Flags, EPropertyFlags::Write))
    {
        return false;
    }

    T* ValuePtr = ContainerPtrToValuePtr<T>(Container);
    if (!ValuePtr)
    {
        return false;
    }

    *ValuePtr = Value;
    return true;
}

template <typename T>
T* FArrayProperty::GetArrayElementPtr(UObject* Container, size_t Index) const
{
    if (!Container || !ArrayOps || !InnerProperty || !HasPropertyFlag(Flags, EPropertyFlags::Read))
    {
        return nullptr;
    }

    if (!InnerProperty->IsCompatibleValueType<T>())
    {
        return nullptr;
    }

    void* ArrayPtr = ContainerPtrToRawValuePtr(Container);
    return ArrayPtr ? static_cast<T*>(ArrayOps->GetElementPtr(ArrayPtr, Index)) : nullptr;
}

template <typename T>
const T* FArrayProperty::GetArrayElementPtr(const UObject* Container, size_t Index) const
{
    if (!Container || !ArrayOps || !InnerProperty || !HasPropertyFlag(Flags, EPropertyFlags::Read))
    {
        return nullptr;
    }

    if (!InnerProperty->IsCompatibleValueType<T>())
    {
        return nullptr;
    }

    const void* ArrayPtr = ContainerPtrToRawValuePtr(Container);
    return ArrayPtr ? static_cast<const T*>(ArrayOps->GetElementPtrConst(ArrayPtr, Index)) : nullptr;
}
