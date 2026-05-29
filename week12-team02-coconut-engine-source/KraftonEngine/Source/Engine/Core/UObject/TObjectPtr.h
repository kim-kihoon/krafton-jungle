#pragma once
#include "FObjectPtr.h"
#include <type_traits>

template <typename T, typename = void>
struct TIsComplete : std::false_type {};

template <typename T>
struct TIsComplete<T, std::void_t<decltype(sizeof(T))>> : std::true_type {};

template <typename T>
struct TObjectPtr
{
	// T must be incomplete (forward-declared) OR derived from UObject.
	// The incomplete-type branch lets headers forward-declare UFoo and still
	// declare TObjectPtr<UFoo> members; the assert re-fires in TUs that see
	// the full T definition.
	static_assert(!TIsComplete<T>::value || std::is_base_of_v<UObject, T>,
		"TObjectPtr<T> requires T to derive from UObject");

public:
	using ElementType = T;

	// ---- ctors ----
	constexpr TObjectPtr() = default;
	constexpr TObjectPtr(std::nullptr_t) : ObjectPtr(nullptr) {}
	TObjectPtr(T* InObject) : ObjectPtr(InObject) {}      // implicit on purpose
	explicit TObjectPtr(FObjectPtr InObjectPtr) : ObjectPtr(InObjectPtr) {}

	TObjectPtr(const TObjectPtr&) = default;
	TObjectPtr(TObjectPtr&&) noexcept = default;

	// Covariant: TObjectPtr<Derived> -> TObjectPtr<Base>
	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TObjectPtr(const TObjectPtr<U>& Other) : ObjectPtr(Other.ObjectPtr) {}

	// ---- assignment ----
	TObjectPtr& operator=(const TObjectPtr&) = default;
	TObjectPtr& operator=(TObjectPtr&&) noexcept = default;
	TObjectPtr& operator=(T* Other) { ObjectPtr = Other;   return *this; }
	TObjectPtr& operator=(std::nullptr_t) { ObjectPtr = nullptr; return *this; }

	template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TObjectPtr& operator=(const TObjectPtr<U>& Other)
	{
		ObjectPtr = Other.ObjectPtr;
		return *this;
	}

	// ---- access ----
	T* Get() const { return static_cast<T*>(ObjectPtr.Get()); }
	T* operator->() const { return Get(); }
	T& operator*()  const { return *Get(); }
	operator T* ()   const { return Get(); }      // drop-in replacement

	explicit operator bool() const { return (bool)ObjectPtr; }
	bool operator!()         const { return !ObjectPtr; }

	void Reset() { ObjectPtr.Reset(); }

	// ---- state / metadata (forward to FObjectPtr) ----
	bool       IsValid()    const { return ObjectPtr.IsValid(); }
	bool       IsResolved() const { return ObjectPtr.IsResolved(); }
	UClass*    GetClass()   const { return ObjectPtr.GetClass(); }
	FName      GetFName()   const { return ObjectPtr.GetFName(); }
	FString    GetName()    const { return ObjectPtr.GetName(); }
	FString    GetPathName()const { return ObjectPtr.GetPathName(); }

	TObjectPtr<UObject> GetOuter()   const { return TObjectPtr<UObject>(ObjectPtr.GetOuter()); }
	TObjectPtr<UObject> GetPackage() const { return TObjectPtr<UObject>(ObjectPtr.GetPackage()); }

	bool IsIn(FObjectPtr SomeOuter) const { return ObjectPtr.IsIn(SomeOuter); }
	template <typename U>
	bool IsIn(TObjectPtr<U> SomeOuter) const { return ObjectPtr.IsIn(SomeOuter.ObjectPtr); }

	bool IsA(const UClass* SomeBase) const { return ObjectPtr.IsA(SomeBase); }
	template <typename U>
	bool IsA() const { return ObjectPtr.template IsA<U>(); }

	// ---- comparisons ----
	template <typename U>
	bool operator==(const TObjectPtr<U>& Other) const { return ObjectPtr == Other.ObjectPtr; }
	template <typename U>
	bool operator!=(const TObjectPtr<U>& Other) const { return ObjectPtr != Other.ObjectPtr; }
	bool operator==(const T* Raw) const { return ObjectPtr == Raw; }
	bool operator!=(const T* Raw) const { return ObjectPtr != Raw; }
	bool operator==(std::nullptr_t) const { return !ObjectPtr; }
	bool operator!=(std::nullptr_t) const { return (bool)ObjectPtr; }

	// ---- hashing ----
	friend uint32 GetTypeHash(const TObjectPtr& P) { return GetTypeHash(P.ObjectPtr); }

	// Friend so covariant ctor/assignment can read ObjectPtr from a different T.
	template <typename U> friend struct TObjectPtr;

private:
	union
	{
		FObjectPtr ObjectPtr{};   // value-init so default-constructed TObjectPtr is valid
		T* DebugPtr;
	};
};

// ---- free helpers (drop-in compatibility) ----
template <typename T> T* ToRawPtr(const TObjectPtr<T>& P) { return P.Get(); }
template <typename T> T* ToRawPtr(T* P) { return P; }
template <typename T> TObjectPtr<T> ToObjectPtr(T* P) { return TObjectPtr<T>(P); }

template <typename T>
T* GetValid(const TObjectPtr<T>& P) { return P.IsValid() ? P.Get() : nullptr; }