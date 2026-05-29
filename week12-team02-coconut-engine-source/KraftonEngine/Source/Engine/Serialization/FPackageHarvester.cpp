#include "FPackageHarvester.h"

FArchive& FPackageHarvester::operator<<(UObject*& Obj)
{
	if (!Obj) return *this;

	// First sighting? Add to table. Repeat sightings deduped via Seen.
	if (Seen.insert(Obj).second)
	{
		ObjectTable.push_back(Obj);
	}
	return *this;
}