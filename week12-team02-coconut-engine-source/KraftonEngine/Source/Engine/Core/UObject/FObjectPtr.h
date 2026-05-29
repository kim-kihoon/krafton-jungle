#pragma once
#include "Core/ClassTypes.h"

class UObject;
class UClass;

// Untyped strong reference to a UObject. Pointer-sized; designed as a future
// upgrade path to a handle-table-backed indirection (matches Unreal layout).
struct FObjectPtr
{
public:
	// ---- ctors ----
	constexpr FObjectPtr() = default;
	constexpr FObjectPtr(std::nullptr_t) {}
	explicit FObjectPtr(UObject* InObject);
	FObjectPtr(const FObjectPtr& Other) = default;
	FObjectPtr(FObjectPtr&& Other) noexcept = default;

	// Reserved for future handle-table-backed mode; today: resolves via global array.
	explicit FObjectPtr(int32 Index);

	// ---- assignment ----
	FObjectPtr& operator=(const FObjectPtr&) = default;
	FObjectPtr& operator=(FObjectPtr&&) noexcept = default;
	FObjectPtr& operator=(UObject* Other);
	FObjectPtr& operator=(std::nullptr_t);

	// ---- access ----
	UObject* Get() const;
	UObject* operator->() const { return Get(); }
	UObject& operator*()  const { return *Get(); }

	explicit operator bool() const { return Handle != nullptr; }
	bool operator!()         const { return Handle == nullptr; }

	void Reset() { Handle = nullptr; }

	// ---- state ----
	bool IsValid() const;						// not null AND not pending-kill (pendingkill is a TODO w/ GC)
	bool IsResolved() const { return true; }	// always true in design (A);
	// kept for API parity with Unreal

// ---- metadata (resolve, then forward) ----
	UClass*	   GetClass()    const;
	FName      GetFName()    const;
	FString    GetName()     const;
	FString    GetPathName() const;
	FObjectPtr GetOuter()    const;
	FObjectPtr GetPackage()  const;
	bool       IsIn(FObjectPtr SomeOuter) const;

	bool IsA(const UClass* SomeBase) const;
	template <typename T>
	bool IsA() const { return IsA(T::StaticClass()); }

	// ---- comparisons ----
	bool operator==(const FObjectPtr& Other) const { return Handle == Other.Handle; }
	bool operator!=(const FObjectPtr& Other) const { return Handle != Other.Handle; }
	bool operator==(const UObject* Raw)      const { return Handle == Raw; }
	bool operator!=(const UObject* Raw)      const { return Handle != Raw; }
	bool operator==(std::nullptr_t)          const { return Handle == nullptr; }
	bool operator!=(std::nullptr_t)          const { return Handle != nullptr; }

	// ---- hashing ----
	friend uint32 GetTypeHash(const FObjectPtr& P);

private:
	// Union mirrors Unreal: when you eventually add a handle table, swap UObject*
	// for an FObjectHandle here. DebugPtr keeps debugger watch windows happy.
	union
	{
		UObject* Handle = nullptr;
		UObject* DebugPtr;  // alias today; meaningful once Handle becomes opaque
	};
};