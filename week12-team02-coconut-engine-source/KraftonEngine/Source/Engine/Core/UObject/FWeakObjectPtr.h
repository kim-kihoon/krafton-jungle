#pragma once
#include "Core/ClassTypes.h"

class UObject;

struct FWeakObjectPtr
{
public:
	FWeakObjectPtr() = default;
	FWeakObjectPtr(const FWeakObjectPtr& Other);
	FWeakObjectPtr(int Index);
	FWeakObjectPtr(const UObject* InObject);

	void Reset() { ObjectIndex = -1; ObjectSerialNumber = -1; }

	bool IsValid() const;

	UObject* Get() const;


public:
	bool operator== (const FWeakObjectPtr& Other) const;
	bool operator!= (const FWeakObjectPtr& Other) const;
	void operator= (const UObject* Object);
	FWeakObjectPtr& operator= (const FWeakObjectPtr& Other);


protected:
	int32	ObjectIndex			= -1;	// The index of the object within Unreal's global object table
	int32   ObjectSerialNumber	= -1;
};