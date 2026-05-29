#pragma once
#include "FSoftObjectPtr.h"
#include "Object/Object.h"

template <typename T>
class TSoftObjectPtr : public FSoftObjectPtr
{
	static_assert(std::is_base_of_v<UObject, T>, "TSoftObjectPtr<T>: T must derive from UObject");

public:
	TSoftObjectPtr() = default;
	explicit TSoftObjectPtr(const FSoftObjectPath& InPath) : FSoftObjectPtr(InPath) {}

	TSoftObjectPtr(T* InObject)
	{
		AssignObject(InObject);
	}

	// Typed accessor: narrows the base's UObject* through an IsA check.
	// Returns nullptr (not a bad pointer) if the resolved object isn't a T.
	T* Get() const
	{
		UObject* Resolved = FSoftObjectPtr::Get();
		return (Resolved && Resolved->IsA<T>()) ? static_cast<T*>(Resolved) : nullptr;
	}

	TSoftObjectPtr& operator=(T* InObject)
	{
		AssignObject(InObject);
		return *this;
	}

	explicit operator bool() const { return Get() != nullptr; }
	T* operator->() const { return Get(); }
	operator T* () const { return Get(); }

private:
	void AssignObject(T* InObject)
	{
		if (!InObject) { Reset(); return; }
		SetPath(InObject->GetAssetPathFileName());
		SetCache(InObject);
	}
};

// Zero-data guarantee. If this fires, someone added a field to TSoftObjectPtr<T>
// and broke the FSoftObjectProperty type-erasure contract.
static_assert(sizeof(TSoftObjectPtr<UObject>) == sizeof(FSoftObjectPtr),
	"TSoftObjectPtr<T> must add no data members beyond FSoftObjectPtr");

// Serialization forwards to the base
template <typename T>
FArchive& operator<<(FArchive& Ar, TSoftObjectPtr<T>& Ptr)
{
	return Ar << static_cast<FSoftObjectPtr&>(Ptr);
}