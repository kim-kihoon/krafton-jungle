#include "FLinker.h"
#include "Object/Object.h"
#include <cassert>

//=======================================================
// FLinkerSave
//=======================================================

FLinkerSave::FLinkerSave(FArchive* InInner, TArray<UObject*> PrebuiltTable)
	: FLinker(InInner)
{
	bIsSaving = true;
	ObjectTable = std::move(PrebuiltTable);

	// Build the inverse map for O(1) IndexForObject lookups.
	for (int32 i = 0; i < static_cast<int32>(ObjectTable.size()); ++i)
	{
		ObjectToIndex.insert({ ObjectTable[i], i + 1 });   // 1-based
	}
}

void FLinkerSave::WriteHeader()
{
	int32 Count = static_cast<int32>(ObjectTable.size());
	*Inner << Count;

	for (UObject* Obj : ObjectTable)
	{
		FString Path = Obj ? Obj->GetPathName() : FString("None");
		*Inner << Path;
	}
}

void FLinkerSave::Serialize(void* Data, size_t Num)
{
	// Pass 2 writes flow straight to disk; no buffer, no flush.
	Inner->Serialize(Data, Num);
}

FArchive& FLinkerSave::operator<<(UObject*& Obj)
{
	int32 Idx = IndexForObject(Obj);
	*this << Idx;
	return *this;
}

int32 FLinkerSave::IndexForObject(UObject* Obj) const
{
	if (!Obj) return 0;

	auto It = ObjectToIndex.find(Obj);
	// Contract: harvester pass should have seen every object that pass 2 references.
	// A miss means the two passes traversed different references — programming error.
	assert(It != ObjectToIndex.end() && "Object referenced in Pass 2 missing from harvested table");

	return It->second;
}


//=======================================================
// FLinkerLoad  (unchanged)
//=======================================================

void FLinkerLoad::ReadTable()
{
	int32 Count = 0;
	*Inner << Count;

	ObjectTable.clear();
	ObjectTable.reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		FString Path;
		*Inner << Path;
		ObjectTable.push_back(FindObjectByPath(Path));
	}
}

UObject* FLinkerLoad::ObjectForIndex(int32 Index) const
{
	if (Index <= 0) return nullptr;
	if (Index > static_cast<int32>(ObjectTable.size())) return nullptr;
	return ObjectTable[Index - 1];
}

void FLinkerLoad::Serialize(void* Data, size_t Num)
{
	Inner->Serialize(Data, Num);
}

FArchive& FLinkerLoad::operator<<(UObject*& Obj)
{
	int32 Idx = 0;
	*this << Idx;
	Obj = ObjectForIndex(Idx);
	return *this;
}