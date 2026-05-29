#pragma once
#include "FFieldVariant.h"

/*========================================================================================================================
	FField is the variable instance itself (e.g., "The health property named Health").

	FFieldVariant defines who owns it (e.g., "This property belongs to AMyCharacter OR it is nested inside a TArray").

	FFieldClass defines what type of variable it is (e.g., "This is an FIntProperty type").
========================================================================================================================*/

// TODO
// FField is the base class for all reflection fields (Properties, Functions) 
// that do NOT need to be UObjects.
//class FField
//{
//public:
//	// Every FField has a 'Class' that defines its type behavior, 
//	// similar to how UObjects have UClass.
//	FFieldClass* ClassPrivate;
//
//	// FFields are stored in a linked list within their owner (UStruct/UClass).
//	// This allows for very fast iteration over properties of a class.
//	FField* NextField;
//
//	// The 'Outer' is replaced by a manual 'Owner' pointer.
//	// This points to the UStruct or UClass that contains this field.
//	FFieldVariant Owner;
//
//
//private:
//	// The name of the property/field (e.g., "Health")
//	FName NamePrivate;
//
//public:
//	FField(FFieldVariant InOwner, const FName& InName)
//		: Owner(InOwner)
//		, NamePrivate(InName)
//		, NextField(nullptr)
//	{
//	}
//
//	// Since it's not a UObject, we must manually handle serialization
//	virtual void Serialize(FArchive& Ar);
//
//	// Name retrieval
//	virtual FName GetFName() const { return NamePrivate; }
//
//	// Casting logic is handled via FFieldClass instead of the UObject reflection system
//	template<typename T>
//	bool IsA() const { return ClassPrivate->IsChildOf(T::StaticClass()); }
//};