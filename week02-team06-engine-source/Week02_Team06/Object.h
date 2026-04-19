#pragma once

#include "Containers.h"
#include "EngineStatics.h"

class UObject
{
	friend class UObjectFactory;
	friend class UImGuiDrawer;

public:
	UObject() = default;
	virtual ~UObject() = default;

public:
	inline static UClass* StaticClass()
	{
		return GetPrivateStaticClass();
	}

	virtual UClass* GetClass() const
	{
		return StaticClass();
	}

	bool IsA(UClass* TargetClass) const
	{
		UClass* cur = GetClass();
		while (cur)
		{
			if (cur == TargetClass) return true;
			cur = cur->SuperClass;
		}
		return false;
	}

public:
	virtual void Release() {}

private:
	static UClass* GetPrivateStaticClass()
	{
		static UClass ClassInfo = {
			"UObject",
			sizeof(UObject),
			nullptr
		};
		return &ClassInfo;
	}

public:
	uint32 UUID = { 0 };
	uint32 SceneUUID = { 0 };
	uint64 internalIndex = { }; // Index of GUObjectArray
};

extern TArray<UObject*> GUObjectArray;


/* 언리얼 IsA 구현 API > API/Runtime > API/Runtime/CoreUObject > API/Runtime/CoreUObject/UObject > API/Runtime/CoreUObject/UObject/UObjectBaseUtility
* 	inline bool IsA( OtherClassType SomeBase ) const
	{
		// We have a cyclic dependency between UObjectBaseUtility and UClass,
		// so we use a template to allow inlining of something we haven't yet seen, because it delays compilation until the function is called.

		// 'static_assert' that this thing is actually a UClass pointer or convertible to it.
		const UClass* SomeBaseClass = SomeBase;
		(void)SomeBaseClass;
		checkfSlow(SomeBaseClass, TEXT("IsA(NULL) cannot yield meaningful results"));

		const UClass* ThisClass = GetClass();

		// Stop the compiler doing some unnecessary branching for nullptr checks
		UE_ASSUME(SomeBaseClass);
		UE_ASSUME(ThisClass);

		return IsChildOfWorkaround(ThisClass, SomeBaseClass);
	}

	/** Returns true if this object is of the template type. */
	// template<class T>
	// bool IsA() const
	// {
	//		return IsA(T::StaticClass());
	//	}
	//