#pragma once
#include "Archive.h"

class UObject;

class FLinker : public FArchive
{
public:
	explicit FLinker(FArchive* InInner) : Inner(InInner) {}

protected:
	FArchive* Inner;
	TArray<UObject*> ObjectTable;    // 1-based; index 0 reserved for null
};

class FLinkerSave final : public FLinker
{
public:
	// Caller supplies the table (typically from FPackageHarvester::TakeTable()).
	// The save linker is then a pure write-through — no buffer, no Finalize.
	FLinkerSave(FArchive* InInner, TArray<UObject*> PrebuiltTable);

	using FArchive::operator<<;                          // template re-export

	// Writes [Count][Path×Count] to Inner. Call once before serializing properties.
	void WriteHeader();

	void      Serialize(void* Data, size_t Num) override;
	FArchive& operator<<(UObject*& Obj) override;

private:
	// Lookup-only. Asserts on miss: every object referenced in Pass 2 should
	// have been seen in Pass 1's harvester.
	int32 IndexForObject(UObject* Obj) const;

	TMap<UObject*, int32> ObjectToIndex;
};

class FLinkerLoad final : public FLinker
{
public:
	explicit FLinkerLoad(FArchive* InInner) : FLinker(InInner) { bIsLoading = true; }

	using FArchive::operator<<;

	void     ReadTable();
	UObject* ObjectForIndex(int32 Index) const;

	void      Serialize(void* Data, size_t Num) override;
	FArchive& operator<<(UObject*& Obj) override;
};