#pragma once
#include <type_traits>

#include "Core/CoreTypes.h"
#include "Object/FName.h"

class UClass;
class UObject;

struct FUObjectItem
{
	UObject* Object = nullptr;
	int32	 SerialNumber = -1;
	bool     bIsStatic = false;
};

// FUObjectArray is NOT a strict singleton in Unreal. It IS a functional singleton nontheless,
// which is achieved by linker level uniqueness. This is beyond the academical scope of this engine.
class FUObjectArray
{
public:
	FUObjectArray() = default;
	~FUObjectArray();
	void Shutdown();

	template<typename T>
	T* CreateObject(UObject* InOuter = nullptr)
	{
		static_assert(std::is_base_of<UObject, T>::value, "T must derive from UObject");
		T* Obj = new T();
		Obj->SetOuter(InOuter);

		const char* ClassName = GetObjectClassName(T::StaticClass());
		uint32& Counter = NameCounters[ClassName];
		FString Name = FString(ClassName) + "_" + std::to_string(Counter++);
		Obj->SetFName(FName(Name));
		AddObject(Obj);

		return Obj;
	}

	int32 AddObject(UObject* Object);
	int32 AddStaticObject(UObject* Object);

	// Reflected-type instances (UClass / UScriptStruct / UEnum) are constructed at
	// static-init time, before GUObjectArray and FNamePool are guaranteed to be
	// alive. They queue themselves here via DeferStaticObject and are flushed
	// from EngineLoop after both globals are ready.
	static void DeferStaticObject(UObject* Object);
	void FlushDeferredStatics();

	// Does not actually removes the object from memory. It simply detaches from the array as of now.
	void RemoveObject(UObject* Object);
	void DestroyObject(UObject* Object);

	// GC feature: Do this later
	//void  FreeUObjectIndex(UObject* Object);

	bool	 IsValidIndex(int32 Index, int32 SerialNumber) const;
	int32	 GetSerialNumber(int32 Index) const;
	UObject* IndexToObject(int32 Index) const;
	UObject* FindByUUID(uint32 InUUID) const;

	bool     IsAlive(const UObject* Object) const;
	bool     IsStaticObject(const UObject* Object) const;
	int32    Num() const { return static_cast<int32>(Items.size()); }	// Does not necessarily return the number of alive objects
	size_t   Capacity() const { return Items.capacity(); }
	const TArray<FUObjectItem>& GetItems() const { return Items; }

private:
	static const char* GetObjectClassName(const UClass* Class);
	int32 GenerateSerialNumber();

private:
	bool bShutdown = false;
	TArray<FUObjectItem> Items;
	TArray<int32>        FreeIndices;
	TSet<UObject*>       LiveSet;
	int32                NextSerial = 0;

	TMap<FString, uint32> NameCounters;
};

extern FUObjectArray GUObjectArray;
