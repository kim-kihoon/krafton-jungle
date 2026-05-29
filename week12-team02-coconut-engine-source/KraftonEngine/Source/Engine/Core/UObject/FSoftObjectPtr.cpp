#include "FSoftObjectPtr.h"
#include "Serialization/Archive.h"


FArchive& operator<<(FArchive& Ar, FSoftObjectPtr& Ptr)
{
	FSoftObjectPath& Path = Ptr.GetMutablePath();
	Ar << Path;
	if (Ar.IsLoading())
	{
		// Path changed; force cache refresh on next Get().
		Ptr.SetCache(nullptr);
	}
	return Ar;
}