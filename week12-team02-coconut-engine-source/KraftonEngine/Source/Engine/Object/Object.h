#pragma once

#include "Profiling/MemoryStats.h"
#include "Object/FName.h"
#include "Object/ObjectMacros.h"
#include "Core/Property/PropertyTypes.h"
#include "Object/FUObjectArray.h"

class FArchive;

// ---------------------------------------------------------------------------
// RTTI Macros
// ---------------------------------------------------------------------------

#define DECLARE_CLASS(ClassName, ParentClass)                               \
    using Super = ParentClass;                                             \
    static UClass StaticClassInstance;                                      \
    static UClass* StaticClass() { return &StaticClassInstance; }           \
    UClass* GetClass() const override { return StaticClass(); }				\
	friend struct ClassName##_PropertyRegistrar;

#define DEFINE_CLASS_WITH_CAST_FLAGS(ClassName, ParentClass, FlagsValue, CastFlagsValue) \
    UClass ClassName::StaticClassInstance(                                  \
        #ClassName,                                                        \
        &ParentClass::StaticClassInstance,                                  \
        sizeof(ClassName),                                                 \
        FlagsValue,                                                        \
        CastFlagsValue                                                     \
    );

#define DEFINE_CLASS_WITH_FLAGS(ClassName, ParentClass, FlagsValue)         \
    DEFINE_CLASS_WITH_CAST_FLAGS(ClassName, ParentClass, FlagsValue, CASTCLASS_None)

#define DEFINE_CLASS(ClassName, ParentClass)                                \
    DEFINE_CLASS_WITH_FLAGS(ClassName, ParentClass, CF_None)

// ---------------------------------------------------------------------------
// Manual property registration helpers.
//
// Normal reflected properties are authored with UPROPERTY(...) and emitted
// directly by Scripts/GenerateCode.py — the per-member registration code is
// inlined into the generated .gen.cpp, no macro indirection. The BEGIN/END
// scaffold and the *_OFFSET helpers below remain for the case the generator
// cannot express: properties whose address can't be reached with
// offsetof(ThisClass, X) (e.g. nested members).
// ---------------------------------------------------------------------------

#define BEGIN_CLASS_PROPERTIES(ClassName)                                   \
	struct ClassName##_PropertyRegistrar {                                  \
		ClassName##_PropertyRegistrar() {                                   \
			using ThisClass = ClassName;                                    \
			UClass* Cls = ClassName::StaticClass();                         \
			(void)Cls;

#define END_CLASS_PROPERTIES(ClassName)                                     \
		}                                                                   \
	};                                                                      \
	static ClassName##_PropertyRegistrar s_##ClassName##_PropertyReg;

#define KE_REGISTER_PROPERTY_OFFSET_IMPL(InName, InType, InCategory, InOffset, InSize, InFlags, ExtraSetup) \
	{                                                                                                 \
		FProperty* P = new FProperty();                                                               \
		P->Name            = (InName);                                                                \
		P->Type            = (InType);                                                                \
		P->Category        = (InCategory);                                                            \
		P->PropertyFlag    = (InFlags);                                                               \
		P->Offset_Internal = static_cast<uint32>(InOffset);                                           \
		P->ElementSize     = static_cast<uint32>(InSize);                                             \
		ExtraSetup;                                                                                   \
		Cls->AddProperty(P);                                                                          \
	}

// 명시적 offset 버전. 중첩 멤버처럼 offsetof(ThisClass, MemberName) 으로 표현할 수 없는
// 필드를 등록할 때 사용한다.
#define REGISTER_PROPERTY_OFFSET(InName, InType, InCategory, InOffset, InSize, InFlags) \
	KE_REGISTER_PROPERTY_OFFSET_IMPL(InName, InType, InCategory, InOffset, InSize, InFlags, (void)0)

#define PROPERTY_FLOAT_OFFSET(InName, InCategory, InOffset, InMin, InMax, InSpeed, InFlags) \
	KE_REGISTER_PROPERTY_OFFSET_IMPL(InName, EPropertyType::Float, InCategory, InOffset, sizeof(float), InFlags, \
		(P->Min = (InMin), P->Max = (InMax), P->Speed = (InSpeed)))

#define PROPERTY_BOOL_OFFSET(InName, InCategory, InOffset, InFlags) \
	KE_REGISTER_PROPERTY_OFFSET_IMPL(InName, EPropertyType::Bool, InCategory, InOffset, sizeof(bool), InFlags, (void)0)

#define PROPERTY_STRUCT_OFFSET(InName, InCategory, InOffset, InSize, ScriptStructPtr, InFlags) \
	KE_REGISTER_PROPERTY_OFFSET_IMPL(InName, EPropertyType::Struct, InCategory, InOffset, InSize, InFlags, \
		(void)(ScriptStructPtr))

// ---------------------------------------------------------------------------

// Forward — IsValid 의 실제 정의는 FUObjectArray 선언 뒤. UObject::GetTypedOuter 가
// non-dependent name lookup 으로 IsValid 를 찾을 수 있게 미리 알려둠.
class UClass;
class UObject;
inline bool IsValid(const UObject* Object);

template<typename T>
T* Cast(UObject* Obj);

template<typename T>
const T* Cast(const UObject* Obj);

class UObject
{
public:
	UObject();
	virtual ~UObject();

	uint32 GetUUID() const { return UUID; }
	uint32 GetInternalIndex() const { return InternalIndex; }
	void SetUUID(uint32 InUUID) { UUID = InUUID; }
	void SetInternalIndex(int32 InIndex) { InternalIndex = InIndex; }

	// Outer — 객체의 논리적 스코프 (소유 의미 아님). 직렬화 제외.
	UObject* GetOuter() const { return Outer; }
	void SetOuter(UObject* InOuter) { Outer = InOuter; }

	// Outer 체인을 따라 첫 번째 T를 찾는다 (UE의 GetTypedOuter<T>와 동일 시맨틱).
	// PendingKill 처리 도중 World 가 actor 보다 먼저 delete 되면 component 의
	// DestroyRenderState 가 Owner->GetWorld → GetTypedOuter<UWorld> 경로를 타다가
	// freed Outer 를 deref 해 crash 났음. 매 iteration 에서 IsValid 로 살아있는 UObject
	// 만 따라가도록 가드.
	template<typename T>
	T* GetTypedOuter() const
	{
		for (UObject* O = Outer; IsValid(O); O = O->Outer)
		{
			if (T* Hit = Cast<T>(O))
			{
				return Hit;
			}
		}
		return nullptr;
	}

	virtual UObject* Duplicate(UObject* NewOuter = nullptr) const;
	virtual void Serialize(FArchive& Ar);
	virtual void PostDuplicate() {}

	virtual void GetAllProperties(TArray<const FProperty*>& OutProps);
	virtual void GetEditableProperties(TArray<const FProperty*>& OutProps);
	virtual void PostEditProperty(const char* PropertyName);
	virtual void GetNonTransientProperties(TArray<const FProperty*>& OutProps);

	virtual const FString& GetAssetPathFileName() const
	{
		static const FString Empty;
		return Empty;
	}

	static void* operator new(size_t Size)
	{
		void* Ptr = std::malloc(Size);
		if (Ptr)
		{
			MemoryStats::OnAllocated(static_cast<uint32>(Size));
		}
		return Ptr;
	}

	static void operator delete(void* Ptr, size_t Size)
	{
		if (Ptr)
		{
			MemoryStats::OnDeallocated(static_cast<uint32>(Size));
			std::free(Ptr);
		}
	}

	// FName
	FName GetFName() const { return ObjectName; }
	FString GetName() const { return ObjectName.ToString(); }
	void SetFName(const FName& InName) { ObjectName = InName; }
	FString GetPathName() const
	{
		FString Path = GetName();
		for (UObject* O = Outer; IsValid(O); O = O->Outer)
		{
			Path = O->GetName() + "." + Path;
		}
		return Path;
	}

	// RTTI
	virtual UClass* GetClass() const { return StaticClass(); }

	template<typename T>
	bool IsA() const { return IsA(T::StaticClass()); }
	bool IsA(const UClass* Other) const;

	static UClass StaticClassInstance;
	static UClass* StaticClass() { return &StaticClassInstance; }

protected:
	FName ObjectName;

private:
	uint32 UUID;
	int32 InternalIndex = -1;
	UObject* Outer = nullptr;
};

// 포인터가 현재 살아있는 UObject 를 가리키는지 확인. dangling/freed 포인터가 들어와도
// 해시 테이블 조회만 하므로 deref 안 함 — 안전.
inline bool IsValid(const UObject* Object)
{
	return GUObjectArray.IsAlive(Object);
}

inline UObject* FindObjectByPath(const FString& Path)
{
	if (Path.empty() || Path == FString("None")) return nullptr;
	for (const FUObjectItem& Item : GUObjectArray.GetItems())
	{
		UObject* Obj = Item.Object;
		if (!Obj) continue;
		if (Obj->GetPathName() == Path) return Obj;
	}
	return nullptr;
}

inline bool IsAliveObject(const UObject* Object)
{
	return IsValid(Object);
}

template<typename T>
T* Cast(UObject* Obj)
{
	return (Obj && Obj->IsA<T>()) ? static_cast<T*>(Obj) : nullptr;
}

template<typename T>
const T* Cast(const UObject* Obj)
{
	return (Obj && Obj->IsA<T>()) ? static_cast<const T*>(Obj) : nullptr;
}
