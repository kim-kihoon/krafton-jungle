#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "Object/Object.h"

// Serial-number based weak UObject pointer. It never owns the object and does
// not keep it alive for GC. Get() rejects PendingKill/Garbage objects; cleanup
// code may use GetEvenIfPendingKill() when it explicitly needs teardown access.
template<typename T>
class TWeakObjectPtr
{
public:
	TWeakObjectPtr() = default;

	TWeakObjectPtr(T* InObject)
	{
		Reset(InObject);
	}

	TWeakObjectPtr& operator=(T* InObject)
	{
		Reset(InObject);
		return *this;
	}

	void Reset(T* InObject = nullptr)
	{
		UObject* LiveObject = GetAliveObjectFromAddress(InObject);
		if (!LiveObject)
		{
			TypedObject = nullptr;
			Object = nullptr;
			SerialNumber = 0;
			return;
		}

		TypedObject = InObject;
		Object = LiveObject;
		SerialNumber = LiveObject->GetSerialNumber();
	}

	T* Get() const
	{
		UObject* LiveObject = GetAliveObjectFromAddress(Object);
		if (!LiveObject || LiveObject->GetSerialNumber() != SerialNumber)
		{
			return nullptr;
		}

		if (LiveObject->HasAnyFlags(RF_PendingKill | RF_Garbage))
		{
			return nullptr;
		}

		return TypedObject;
	}

	T* GetEvenIfPendingKill() const
	{
		UObject* LiveObject = GetAliveObjectFromAddress(Object);
		if (!LiveObject || LiveObject->GetSerialNumber() != SerialNumber)
		{
			return nullptr;
		}

		return TypedObject;
	}

	T* GetAlive() const { return GetEvenIfPendingKill(); }
	bool IsValid() const { return Get() != nullptr; }
	bool bValid() const { return IsValid(); }

	explicit operator bool() const { return IsValid(); }
	operator T*() const { return Get(); }
	T* operator->() const { return Get(); }

	bool Equals(const T* Other) const { return Get() == Other; }

	bool operator==(const TWeakObjectPtr& Other) const
	{
		return Object == Other.Object && SerialNumber == Other.SerialNumber;
	}

	bool operator!=(const TWeakObjectPtr& Other) const
	{
		return !(*this == Other);
	}

	bool operator==(const T* Other) const
	{
		return Get() == Other;
	}

	bool operator!=(const T* Other) const
	{
		return !(*this == Other);
	}

	uintptr_t GetWeakAddressKey() const { return reinterpret_cast<uintptr_t>(Object); }
	uint32 GetWeakSerialNumber() const { return SerialNumber; }
private:
	T* TypedObject = nullptr;
	UObject* Object = nullptr;
	uint32 SerialNumber = 0;
};

template<typename T>
inline bool operator==(const T* Object, const TWeakObjectPtr<T>& WeakObject)
{
	return WeakObject == Object;
}

template<typename T>
inline bool operator!=(const T* Object, const TWeakObjectPtr<T>& WeakObject)
{
	return !(WeakObject == Object);
}

namespace std
{
	template<typename T>
	struct hash<TWeakObjectPtr<T>>
	{
		size_t operator()(const TWeakObjectPtr<T>& Ptr) const noexcept
		{
			const size_t AddressHash = std::hash<uintptr_t>{}(Ptr.GetWeakAddressKey());
			const size_t SerialHash = std::hash<uint32>{}(Ptr.GetWeakSerialNumber());
			return AddressHash ^ (SerialHash + 0x9e3779b97f4a7c15ull + (AddressHash << 6) + (AddressHash >> 2));
		}
	};
}
