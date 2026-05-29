#pragma once
#include "Archive.h"
#include "Core/CoreTypes.h"
#include <unordered_set>

class UObject;

// Pass-1 archive: walks the object graph as if it were saving, but discards
// all primitive bytes and only records UObject references into a table.
// After the pass, the populated table is handed to FLinkerSave so Pass 2 can
// resolve refs to indices in O(1) without any buffer-and-flush dance.
//
// Named after Unreal's FPackageHarvester even though we have no UPackage yet. 
// When UPackage lands, this class evolves into the per-package version
// rather than being renamed.
class FPackageHarvester final : public FArchive
{
public:
	FPackageHarvester() { bIsSaving = true; }   // makes property code take the save branch

	using FArchive::operator<<;                 // re-expose the trivially-copyable template

	// Discard all primitive bytes — the harvester doesn't care what they are.
	void Serialize(void* /*Data*/, size_t /*Num*/) override {}

	// The only override that does real work: record the reference.
	FArchive& operator<<(UObject*& Obj) override;

	// Hand off the populated table to FLinkerSave. Empties the internal state.
	TArray<UObject*> TakeTable() { return std::move(ObjectTable); }

private:
	TArray<UObject*>             ObjectTable;
	std::unordered_set<UObject*> Seen;
};