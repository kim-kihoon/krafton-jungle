#pragma once
#include "FWeakObjectPtr.h"

// TWeakObjectPtr.h - zero-data typed wrapper
template <typename T>
class TWeakObjectPtr : public FWeakObjectPtr
{
	static_assert(std::is_base_of_v<UObject, T>);
public:
	using FWeakObjectPtr::FWeakObjectPtr;
	T* Get() const
	{
		UObject* O = FWeakObjectPtr::Get();
		return (O && O->IsA<T>()) ? static_cast<T*>(O) : nullptr;
	}
	explicit operator bool() const { return Get() != nullptr; }
	T* operator->() const { return Get(); }
	operator T* () const { return Get(); }
};

static_assert(sizeof(TWeakObjectPtr<UObject>) == sizeof(FWeakObjectPtr));