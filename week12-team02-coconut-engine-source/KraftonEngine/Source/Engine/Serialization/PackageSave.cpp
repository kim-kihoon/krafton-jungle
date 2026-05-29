#include "PackageSave.h"
#include "FLinker.h"
#include "FPackageHarvester.h"
#include "Object/Object.h"

void SavePackage(UObject* Root, FArchive& Out)
{
	// Pass 1: discover references
	FPackageHarvester Harvester;
	Root->Serialize(Harvester);

	// Pass 2: write everything in linear order — header, then properties.
	FLinkerSave Lk(&Out, Harvester.TakeTable());
	Lk.WriteHeader();
	Root->Serialize(Lk);
}

void LoadPackage(UObject* Root, FArchive& In)
{
	FLinkerLoad Lk(&In);
	Lk.ReadTable();
	Root->Serialize(Lk);
}
