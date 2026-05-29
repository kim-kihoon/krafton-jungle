#include "Archive.h"
#include "Object/Object.h"

FArchive& FArchive::operator<<(UObject*& Obj)
{
	assert(false);
	return *this;
}