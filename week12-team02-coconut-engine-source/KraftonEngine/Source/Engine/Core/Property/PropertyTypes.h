#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Core/CoreTypes.h"

namespace json { class JSON; }

struct FArrayAccessor
{
	uint32	(*Num)(const void* ArrayPtr);
	void*	(*GetAt)(void* ArrayPtr, uint32 Index);
	void	(*AddDefault)(void* ArrayPtr);
	void	(*RemoveAt)(void* ArrayPtr, uint32 Index);
	void	(*Clear)(void* ArrayPtr);
	void	(*Assign)(void* DstArr, const void* SrcArr);
};

template <typename T>
inline FArrayAccessor* GetTArrayAccessor()
{
	static FArrayAccessor s = {
		+[](const void* A) -> uint32 { return (uint32)static_cast<const TArray<T>*>(A)->size(); },
		+[](void* A, uint32 i) -> void* { return &(*static_cast<TArray<T>*>(A))[i]; },
		+[](void* A) { static_cast<TArray<T>*>(A)->emplace_back(); },
		+[](void* A, uint32 i) { auto& V = *static_cast<TArray<T>*>(A); V.erase(V.begin() + i); },
		+[](void* A) { static_cast<TArray<T>*>(A)->clear(); },
		+[](void* D, const void* S) { *static_cast<TArray<T>*>(D) = *static_cast<const TArray<T>*>(S); },
	};
	return &s;
}

// Property dispatch tag used by the editor and serializers.
enum class EPropertyType : uint8_t
{
	Bool,
	ByteBool,
	Int,
	Float,
	Vec3,
	Vec4,
	Rotator,
	String,
	Name,
	SceneComponentRef,
	Color4,
	MaterialSlot,
	Enum,
	Struct,
	Script,
	Array,
	SoftObject,
	Object,
};

enum EPropertyFlags : uint32 {
	CPF_None						= 0,
	CPF_Edit						= 1 << 1,
	CPF_FixedSize					= 1 << 2,
	CPF_Transient					= 1 << 3,
	CPF_DuplicateTransient			= 1 << 4,
	CPF_NonPIEDuplicateTransient	= 1 << 5,
	CPF_Config						= 1 << 6,
};

struct FMaterialSlot
{
	std::string SlotName;
	std::string Path;
};

class FArchive;
class FProperty;

// Should inherit from FField when ready
class FProperty
{
public:
	virtual ~FProperty() = default;
	FProperty(const FProperty&) = delete;
	FProperty& operator=(const FProperty&) = delete;

	FString Name;
	FString Category;
	uint32 PropertyFlag = EPropertyFlags::CPF_None;
	uint32 ElementSize = 0;
	uint32 Offset_Internal = 0;

	virtual EPropertyType GetType() const = 0;
	virtual json::JSON Serialize(const void* Instance) const = 0;
	virtual void Deserialize(void* Instance, const json::JSON& Value) const = 0;
	virtual void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const = 0;

	void* ContainerPtrToValuePtr(void* Container) const
	{
		return static_cast<char*>(Container) + Offset_Internal;
	}

	const void* ContainerPtrToValuePtr(const void* Container) const
	{
		return static_cast<const char*>(Container) + Offset_Internal;
	}

protected:
	FProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize)
		: Name(InName)
		, Category(InCategory)
		, PropertyFlag(InPropertyFlag)
		, ElementSize(InElementSize)
		, Offset_Internal(InOffset)
	{}
};

// FProperty is pure schema. Instance addresses are derived from the owning container.

class FBoolProperty final : public FProperty
{
public:
	FBoolProperty(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Bool; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FByteBoolProperty final : public FProperty
{
public:
	FByteBoolProperty(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::ByteBool; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FNumericProperty : public FProperty
{
public:
	float Min = 0.0f;
	float Max = 0.0f;
	float Speed = 0.1f;

protected:
	FNumericProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize,
		float InMin = 0.0f,
		float InMax = 0.0f,
		float InSpeed = 0.1f)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
		, Min(InMin)
		, Max(InMax)
		, Speed(InSpeed)
	{
	}
};


class FIntProperty final : public FNumericProperty
{
public:
	FIntProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize,
		float InMin = 0.0f,
		float InMax = 0.0f,
		float InSpeed = 0.1f)
		: FNumericProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize, InMin, InMax, InSpeed)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Int; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FFloatProperty final : public FNumericProperty
{
public:
	FFloatProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize,
		float InMin = 0.0f,
		float InMax = 0.0f,
		float InSpeed = 0.1f)
		: FNumericProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize, InMin, InMax, InSpeed)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Float; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FColor4Property final : public FProperty
{
public:
	FColor4Property(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Color4; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FMaterialSlotProperty final : public FProperty
{
public:
	FMaterialSlotProperty(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::MaterialSlot; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FNameProperty final : public FProperty
{
public:
	FNameProperty(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Name; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FRotatorProperty final : public FProperty
{
public:
	FRotatorProperty(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Rotator; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FSceneComponentRefProperty final : public FProperty
{
public:
	FSceneComponentRefProperty(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::SceneComponentRef; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FScriptProperty final : public FProperty
{
public:
	FScriptProperty(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Script; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};



class FStringProperty final : public FProperty
{
public:
	FStringProperty(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::String; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FVec3Property final : public FProperty
{
public:
	FVec3Property(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Vec3; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};


class FVec4Property final : public FProperty
{
public:
	FVec4Property(const FString& InName, const FString& InCategory, uint32 InPropertyFlag, uint32 InOffset, uint32 InElementSize)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
	{
	}

	EPropertyType GetType() const override { return EPropertyType::Vec4; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
	void SerializeItem(FArchive& Ar, void* Value, void const* Defaults) const override;
};
