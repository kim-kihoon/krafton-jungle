#pragma once

#include "Profiling/Stats/MemoryStats.h"
#include "Object/FName.h"
#include "Core/Singleton.h"
#include "Core/Types/PropertyTypes.h"
#include "Object/Reflection/UClass.h"
#include "Object/Reflection/ObjectMacros.h"

#include "Source/Engine/Object/Object.generated.h"

class FArchive;
class FDuplicateArchiveContext;

// Forward — IsValid 의 실제 정의는 GUObjectSet 선언 뒤. UObject::GetTypedOuter 가
// non-dependent name lookup 으로 IsValid 를 찾을 수 있게 미리 알려둠.
class UObject;
inline bool IsValid(const UObject* Object);
inline bool IsAliveObject(const UObject* Object);
inline UObject* GetAliveObjectFromAddress(const void* ObjectAddress);

template<typename T>
T* Cast(UObject* Obj);

template<typename T>
const T* Cast(const UObject* Obj);

enum EObjectFlags : uint32
{
	RF_None          = 0,
	RF_RootSet       = 1 << 0,
	RF_PendingKill   = 1 << 1,
	RF_Unreachable   = 1 << 2,
	RF_Marked        = 1 << 3,
	RF_BeginDestroy  = 1 << 4,
	RF_FinishDestroy = 1 << 5,
	RF_Garbage       = 1 << 6,
};

class FReferenceCollector;

UCLASS()
class UObject
{
public:
	GENERATED_BODY()

	UObject();
	virtual ~UObject();

	uint32 GetUUID() const { return UUID; }
	uint32 GetInternalIndex() const { return InternalIndex; }
	void SetUUID(uint32 InUUID) { UUID = InUUID; }
	void SetInternalIndex(uint32 InIndex) { InternalIndex = InIndex; }

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
		// Some legacy call sites may still invoke GetWorld()/GetTypedOuter() through
		// a null or already-unregistered object during destruction. Guard self before
		// touching Outer so teardown becomes a safe nullptr result instead of a crash.
		if (!IsAliveObject(this))
		{
			return nullptr;
		}

		for (UObject* O = Outer; IsValid(O); O = O->Outer)
		{
			if (T* Hit = Cast<T>(O))
			{
				return Hit;
			}
		}
		return nullptr;
	}

	// 정리/파괴 라우팅 전용. 일반 게임 로직은 GetTypedOuter()를 사용해야 한다.
	template<typename T>
	T* GetTypedOuterEvenIfPendingKill() const
	{
		if (!IsAliveObject(this))
		{
			return nullptr;
		}

		for (UObject* O = Outer; IsAliveObject(O); O = O->Outer)
		{
			if (O->IsA<T>())
			{
				return static_cast<T*>(O);
			}
		}
		return nullptr;
	}

	virtual UObject* Duplicate(UObject* NewOuter = nullptr) const;
	UObject* DuplicateWithArchiveContext(UObject* NewOuter, FDuplicateArchiveContext& DuplicateContext) const;
	// Template Method 진입점. 고정 순서로 직렬화 단계를 오케스트레이션한다.
	// (단계: SerializeIdentity → OnPreSave[저장] → SerializeProperties[ShouldReflectProperties()==true]
	//        → SerializeExtra → OnPostLoad[로드])
	// 이 경로는 obj->Serialize() — 즉 에셋(.uasset)/복제(Duplicate) 에서만 사용된다.
	// 씬 저장은 SceneSaveManager 가 SerializeProperties 를 직접 호출하므로 아래 훅은 돌지 않는다.
	virtual void Serialize(FArchive& Ar);
	void SerializeProperties(FArchive& Ar, uint32 RequiredFlags);
	// SceneSaveManager / instanced-subobject loaders intentionally serialize reflected
	// properties directly, but still need the same semantic save/load seams as
	// UObject::Serialize(). Keep OnPreSave/OnPostLoad protected and expose only
	// these explicit archive hooks.
	void PreSaveForArchive(FArchive& Ar) { OnPreSave(Ar); }
	void PostLoadFromArchive(FArchive& Ar) { OnPostLoad(Ar); }

protected:
	// ── 직렬화 훅 (서브클래스가 필요한 것만 오버라이드) ──
	virtual void SerializeIdentity(FArchive& Ar);                  // 기본: ObjectName 직렬화
	virtual bool ShouldReflectProperties() const { return true; }  // UPROPERTY(Save) 자동 반사. 수동 포맷 클래스만 false 로 opt-out
	virtual void OnPreSave(FArchive& /*Ar*/) {}                    // 반사 전(저장) — 스냅샷 등
	virtual void SerializeExtra(FArchive& /*Ar*/) {}               // 반사로 못 담는 수동 필드
	// Versioned tagged loads use OnPostLoad as the semantic compatibility/default-fill seam
	// after raw reflected serialization has already handled missing/unknown properties safely.
	virtual void OnPostLoad(FArchive& /*Ar*/) {}                   // 반사 후(로드) — 파생 상태 재구성 / compatibility fixups

public:
	virtual void PostDuplicate() {}

	virtual void GetEditableProperties(TArray<FPropertyValue>& OutProps);
	virtual void PreGetEditableProperties() {}
	virtual bool ShouldExposeProperty(const FProperty& Property) const;
	virtual void PostEditChangeProperty(const FPropertyChangedEvent& Event);
	virtual void PostEditProperty(const char* PropertyName);

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

	// RTTI
	virtual UClass* GetClass() const { return StaticClass(); }

	template<typename T>
	bool IsA() const { return GetClass()->IsA(T::StaticClass()); }

	static UClass StaticClassInstance;
	static UClass* StaticClass() { return &StaticClassInstance; }

	uint32 GetObjectFlags() const { return ObjectFlags; }
	bool HasAnyFlags(uint32 Flags) const { return (ObjectFlags & Flags) != 0; }
	bool HasAllFlags(uint32 Flags) const { return (ObjectFlags & Flags) == Flags; }
	void SetFlags(uint32 Flags) { ObjectFlags |= Flags; }
	void ClearFlags(uint32 Flags) { ObjectFlags &= ~Flags; }

	bool IsRooted() const { return HasAnyFlags(RF_RootSet); }
	bool IsPendingKill() const { return HasAnyFlags(RF_PendingKill); }
	bool IsGarbage() const { return HasAnyFlags(RF_Garbage); }

	void AddToRoot() { SetFlags(RF_RootSet); }
	void RemoveFromRoot() { ClearFlags(RF_RootSet); }
	void MarkPendingKill() { SetFlags(RF_PendingKill); }

	// 2단계 GC에서 property/reference token 기반 구현으로 확장한다.
	virtual void AddReferencedObjects(FReferenceCollector& Collector);

	virtual bool ProcessEvent(
		const FFunction* Function,
		void* ParametersStorage = nullptr,
		void* ReturnValueStorage = nullptr
		);
	virtual void BeginDestroy();
	virtual bool IsReadyForFinishDestroy() const { return true; }
	virtual void FinishDestroy();

	uint32 GetSerialNumber() const { return SerialNumber; }

protected:
	FName ObjectName;

private:
	uint32 UUID;
	uint32 InternalIndex;
	UObject* Outer = nullptr;
	uint32 ObjectFlags = RF_None;
	uint32 SerialNumber = 0;
};

extern TArray<UObject*> GUObjectArray;
// 살아있는 UObject 포인터를 O(1) 로 조회하기 위한 set. UObject ctor/dtor 가 자동 유지.
// dangling pointer 도 hash 만 계산하므로(deref 없음) 안전.
extern TSet<UObject*> GUObjectSet;

// 포인터가 현재 GUObjectSet에 등록된 UObject 주소인지 확인한다. dangling/freed
// 포인터가 들어와도 해시 테이블 조회 전에는 deref하지 않는다.
inline UObject* GetAliveObjectFromAddress(const void* ObjectAddress)
{
	if (!ObjectAddress)
	{
		return nullptr;
	}

	UObject* Candidate = reinterpret_cast<UObject*>(const_cast<void*>(ObjectAddress));
	return GUObjectSet.find(Candidate) != GUObjectSet.end() ? Candidate : nullptr;
}

inline bool IsAliveObject(const UObject* Object)
{
	return GetAliveObjectFromAddress(Object) != nullptr;
}

// 게임 로직에서 접근 가능한 UObject인지 확인한다. PendingKill/Garbage 객체는
// 아직 메모리에 남아 있어도 유효하지 않은 객체로 취급한다.
inline bool IsValid(const UObject* Object)
{
	UObject* LiveObject = GetAliveObjectFromAddress(Object);
	return LiveObject && !LiveObject->HasAnyFlags(RF_PendingKill | RF_Garbage);
}

class UObjectManager : public TSingleton<UObjectManager>
{
	friend class TSingleton<UObjectManager>;

public:
	template<typename T>
	T* CreateObject(UObject* InOuter = nullptr)
	{
		static_assert(std::is_base_of<UObject, T>::value, "T must derive from UObject");
		T* Obj = new T();
		Obj->SetOuter(InOuter);

		const char* ClassName = T::StaticClass()->GetName();
		uint32& Counter = NameCounters[ClassName];
		FString Name = FString(ClassName) + "_" + std::to_string(Counter++);
		Obj->SetFName(FName(Name));

		return Obj;
	}

	// 1단계부터 파괴 요청은 즉시 delete가 아니라 BeginDestroy + PendingKill로 통일한다.
	// BeginDestroy는 tick/render/physics/owner 같은 외부 연결만 끊고, 실제 FinishDestroy/delete는
	// 2단계 GC sweep에서만 수행한다.
	void DestroyObject(UObject* Obj)
	{
		if (!IsAliveObject(Obj) || Obj->IsRooted())
		{
			return;
		}

		if (Obj->HasAnyFlags(RF_BeginDestroy | RF_FinishDestroy | RF_Garbage))
		{
			return;
		}

		Obj->BeginDestroy();
		if (!Obj->HasAnyFlags(RF_PendingKill))
		{
			Obj->MarkPendingKill();
		}

		FGarbageCollector::Get().RequestGarbageCollection();
	}

	// 기존 호출부 호환용. 동기 메모리 해제 의미는 없고 DestroyObject와 동일하게
	// BeginDestroy까지만 보장한다. 메모리 회수는 GC sweep 책임이다.
	void DestroyObjectImmediate(UObject* Obj)
	{
		DestroyObject(Obj);
	}

private:
	TMap<FString, uint32> NameCounters;

public:
	UObject* FindByUUID(uint32 InUUID)
	{
		for (auto* Obj : GUObjectArray)
			if (Obj && Obj->GetUUID() == InUUID)
				return Obj;
		return nullptr;
	}

	UObject* FindByIndex(uint32 Index)
	{
		if (Index >= GUObjectArray.size()) return nullptr;
		return GUObjectArray[Index];
	}
};

template<typename T>
T* Cast(UObject* Obj)
{
	return (IsValid(Obj) && Obj->IsA<T>()) ? static_cast<T*>(Obj) : nullptr;
}

template<typename T>
const T* Cast(const UObject* Obj)
{
	return (IsValid(Obj) && Obj->IsA<T>()) ? static_cast<const T*>(Obj) : nullptr;
}
