#pragma once

#include "FWeakObjectPtr.h"
#include "FSoftObjectPath.h"

class UObject;
class FArchive;

class FSoftObjectPtr
{
public:
	FSoftObjectPtr() = default;
	explicit FSoftObjectPtr(const FSoftObjectPath& InPath) : Path(InPath) {}

	bool IsNull() const { return Path.IsNull(); }
	void Reset() { Path.Reset(); CachedObject = nullptr; }

	const FSoftObjectPath& GetPath() const { return Path; }
	FSoftObjectPath& GetMutablePath() { return Path; }

	void SetPath(const FSoftObjectPath& InPath)
	{
		Path = InPath;
		CachedObject = nullptr;       // path change invalidates cache
	}
	void SetPath(const FString& InPath) { SetPath(FSoftObjectPath(InPath)); }

	// Lazy-resolves on first call after a path change. Generic, no T involved.
	UObject* Get() const
	{
		if (!CachedObject.IsValid() && !Path.IsNull())
		{
			CachedObject = Path.ResolveObject();
		}
		return CachedObject.Get();
	}

	// Hint: caller that already has the resolved object can prime the cache,
	// skipping the next resolve. Does NOT update Path - use SetPath for that.
	void SetCache(UObject* InObject) const { CachedObject = InObject; }

protected:
	FSoftObjectPath  Path;

	// TDODO: Change this to TWeakObjectPtr
	mutable FWeakObjectPtr CachedObject;
};

FArchive& operator<<(FArchive& Ar, FSoftObjectPtr& Ptr);