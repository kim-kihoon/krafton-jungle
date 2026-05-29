#include "FWeakObjectPtr.h"
#include "Object/Object.h"

FWeakObjectPtr::FWeakObjectPtr(const FWeakObjectPtr& Other)
{
	ObjectIndex			= Other.ObjectIndex;
	ObjectSerialNumber	= Other.ObjectSerialNumber;
}

FWeakObjectPtr::FWeakObjectPtr(int Index)
{
	if (Index >= GUObjectArray.Num()) return; 
	ObjectIndex			= Index;
	ObjectSerialNumber	= GUObjectArray.GetSerialNumber(Index);
}

FWeakObjectPtr::FWeakObjectPtr(const UObject* InObject)
{
	if (!InObject) return;
	ObjectIndex = InObject->GetInternalIndex();
	ObjectSerialNumber = GUObjectArray.GetSerialNumber(ObjectIndex);
}

UObject* FWeakObjectPtr::Get() const 
{ 
	return IsValid() ? GUObjectArray.IndexToObject(ObjectIndex) : nullptr;
}

bool FWeakObjectPtr::IsValid() const
{
	if (ObjectIndex < 0 || ObjectIndex >= GUObjectArray.Num()) return false;
	return GUObjectArray.IndexToObject(ObjectIndex)
		&& GUObjectArray.IsValidIndex(ObjectIndex, ObjectSerialNumber);
}

bool FWeakObjectPtr::operator== (const FWeakObjectPtr& Other) const
{
	return (ObjectIndex == Other.ObjectIndex) && (ObjectSerialNumber == Other.ObjectSerialNumber);
}

bool FWeakObjectPtr::operator!= (const FWeakObjectPtr& Other) const
{
	return (ObjectIndex != Other.ObjectIndex) || (ObjectSerialNumber != Other.ObjectSerialNumber);
}

void FWeakObjectPtr::operator= (const UObject* InObject)
{
	if (!InObject) { 
		Reset();
		return;
	}

	ObjectIndex = InObject->GetInternalIndex();
	ObjectSerialNumber = GUObjectArray.GetSerialNumber(ObjectIndex);
}

FWeakObjectPtr& FWeakObjectPtr::operator= (const FWeakObjectPtr& Other)
{
	ObjectIndex			= Other.ObjectIndex;
	ObjectSerialNumber	= Other.ObjectSerialNumber;
	return *this;
}
