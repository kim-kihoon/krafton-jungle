#pragma once
#include "Object/FName.h"

/*===============================================================================================================================
FFieldVariant is a specialized type-safe union (container). It describes where a property lives.

The Problem: 

A property (FProperty) must always have an owner. However, in UE5, a property’s owner can be two different things:
A UObject (e.g., a UClass, UFunction, or UScriptStruct).
Another FField (e.g., an FIntProperty inside an FArrayProperty is owned by that ArrayProperty).
The Solution: Since UObject and FField do not share a common base class, you cannot have a single pointer that points to both.
FFieldVariant is a small wrapper that holds either a UObject* or an FField*.
/*===============================================================================================================================*/

class UObject;
class FField;

// TODO
// FFieldVariant is a low level “bridge” container introduced in Unreal Engine
// to handle the architectural split between UField(UObjects) and FField(lightweight C++ objects).
//struct FFieldVariant
//{
//private:
//	// This stores the raw pointer.
//	// Bit 0 == 0 -> The pointer is a UObject*
//	// Bit 0 == 1 -> The pointer is an FField*
//	mutable UPTRINT Container;
//
//	// Constants for bit manipulation
//	enum { IsFFieldBit = 0x1 };
//
//public:
//	FFieldVariant() : Container(0) {}
//
//	// Constructor for UObject (Bit 0 remains 0)
//	FFieldVariant(UObject* InObject)
//		: Container((UPTRINT)InObject)
//	{
//	}
//
//	// Constructor for FField (We set Bit 0 to 1)
//	FFieldVariant(FField* InField)
//		: Container((UPTRINT)InField | IsFFieldBit)
//	{
//	}
//
//	/** Returns true if the stored pointer is an FField */
//	bool IsFField() const
//	{
//		return (Container != 0) && ((Container & IsFFieldBit) != 0);
//	}
//
//	/** Returns true if the stored pointer is a UObject */
//	bool IsUObject() const
//	{
//		return (Container != 0) && ((Container & IsFFieldBit) == 0);
//	}
//
//	/** Retrieves the pointer as an FField* by masking out the tag bit */
//	FField* ToFField() const
//	{
//		return IsFField() ? (FField*)(Container & ~IsFFieldBit) : nullptr;
//	}
//
//	/** Retrieves the pointer as a UObject* */
//	UObject* ToUObject() const
//	{
//		return IsUObject() ? (UObject*)Container : nullptr;
//	}
//
//	/**
//	 * Helper to get the owner name regardless of type.
//	 * This is used heavily when the engine is printing property paths.
//	 */
//	FName GetFName() const;
//};