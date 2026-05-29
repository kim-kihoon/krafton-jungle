#include "FUObjectArray.h"
#include "Object/Object.h"
#include "Object/UClass.h"

FUObjectArray GUObjectArray;

FUObjectArray::~FUObjectArray()
{
	if (!bShutdown) Shutdown();
}

const char* FUObjectArray::GetObjectClassName(const UClass* Class)
{
	return Class ? Class->GetName() : "UObject";
}

void FUObjectArray::Shutdown()
{
	for (auto& Item : Items)
	{
		if (!Item.Object) continue;

		if (Item.bIsStatic)
		{
			Item.Object->SetInternalIndex(-1);
			Item.Object = nullptr;
			continue;
		}

		delete Item.Object;
		Item.Object = nullptr;
	}
	Items.clear();
	FreeIndices.clear();
	LiveSet.clear();

	bShutdown = true;
}

int32 FUObjectArray::AddObject(UObject* Object)
{
	if (!Object) return -1;

	LiveSet.insert(Object);
	if (FreeIndices.empty()) {
		FUObjectItem Item;
		Item.Object = Object;
		Item.SerialNumber = GenerateSerialNumber();
		Items.push_back(Item);
		uint32 Index = static_cast<uint32>(Items.size() - 1);
		Object->SetInternalIndex(Index);
		return Index;
	}

	uint32 Index = FreeIndices.back();
	Items[Index].Object = Object;
	Object->SetInternalIndex(Index);
	FreeIndices.pop_back();
	return Index;
}

static TArray<UObject*>& GetDeferredStaticQueue()
{
	static TArray<UObject*> Queue;
	return Queue;
}

void FUObjectArray::DeferStaticObject(UObject* Object)
{
	if (!Object) return;
	GetDeferredStaticQueue().push_back(Object);
}

void FUObjectArray::FlushDeferredStatics()
{
	TArray<UObject*>& Queue = GetDeferredStaticQueue();
	for (UObject* Object : Queue)
	{
		AddStaticObject(Object);
	}
	Queue.clear();
}

int32 FUObjectArray::AddStaticObject(UObject* Object)
{
	if (!Object) return -1;

	const int32 ExistingIndex = Object->GetInternalIndex();
	if (IndexToObject(ExistingIndex) == Object)
	{
		Items[ExistingIndex].bIsStatic = true;
		LiveSet.insert(Object);
		return ExistingIndex;
	}

	const int32 NewIndex = AddObject(Object);
	if (NewIndex >= 0)
	{
		Items[NewIndex].bIsStatic = true;
	}
	return NewIndex;
}

void FUObjectArray::RemoveObject(UObject* Object)
{
	if (bShutdown) return;
	if (!Object || IsStaticObject(Object)) return;
	LiveSet.erase(Object);

	int32 Index = Object->GetInternalIndex();

	// Index check
	if (!IndexToObject(Index) || IndexToObject(Index) != Object) return;
	FUObjectItem& Item = Items[Index];

	Items[Index].Object = nullptr;
	Items[Index].bIsStatic = false;

	// Serial number bump
	Item.SerialNumber = GenerateSerialNumber();

	// Add to free indices
	FreeIndices.push_back(Index);
	Object->SetInternalIndex(-1);
}

void FUObjectArray::DestroyObject(UObject* Object)
{
	if (!Object || IsStaticObject(Object)) return;
	delete Object;
}

// TODO: Extend this function for GC
//void FUObjectArray::FreeUObjectIndex(UObject* Object)
//{
//	if (!Object) return;
//	LiveSet.erase(Object);
//
//	int32 Index = Object->GetInternalIndex();
//
//	// Index check
//	if (!IndexToObject(Index) || IndexToObject(Index) != Object) return;
//	FUObjectItem& Item = Items[Index];
//
//	// Remove Object. Mind that the Object is not actually being deleted here.
//	Object->SetInternalIndex(-1);
//
//	// Serial number bump
//	Item.SerialNumber = GenerateSerialNumber();
//
//	// Add to free indices
//	FreeIndices.push_back(Index);
//}

bool FUObjectArray::IsValidIndex(int32 Index, int32 SerialNumber) const
{
	if (Index < 0 || Index >= Items.size() || SerialNumber < 0) return false;
	return Items[Index].SerialNumber == SerialNumber;
}

int32 FUObjectArray::GetSerialNumber(int32 Index) const
{
	if (Index < 0 || Index >= Items.size()) return -1;
	return Items[Index].SerialNumber;
}

UObject* FUObjectArray::IndexToObject(int32 Index) const
{
	if (Index < 0 || Index >= Items.size()) return nullptr;
	return Items[Index].Object;
}

UObject* FUObjectArray::FindByUUID(uint32 InUUID) const {
	for (const auto& Item : Items) {
		UObject* Obj = Item.Object;
		if (Obj && Obj->GetUUID() == InUUID)
			return Obj;
	}
	return nullptr;
}

bool FUObjectArray::IsAlive(const UObject* Object) const  
{  
   if (!Object) return false;  
   return LiveSet.find(const_cast<UObject*>(Object)) != LiveSet.end();  
}

bool FUObjectArray::IsStaticObject(const UObject* Object) const
{
	if (!Object) return false;
	const int32 Index = Object->GetInternalIndex();
	if (Index < 0 || Index >= static_cast<int32>(Items.size())) return false;
	return Items[Index].Object == Object && Items[Index].bIsStatic;
}

int32 FUObjectArray::GenerateSerialNumber() 
{
	return NextSerial++;
}
