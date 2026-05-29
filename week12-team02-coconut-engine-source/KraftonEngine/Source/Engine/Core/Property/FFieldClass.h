#pragma once
#include "Object/FName.h"

/*=============================================================================================================================
FFieldClass is the lightweight, non-UObject equivalent of UClass. It describes what a property is.

Purpose: It stores the static metadata for a specific field type.
Contents: It contains the Bitmask ID (used for the O(1) type checking), the type name (e.g., "FloatProperty"), and class flags.
/*=============================================================================================================================*/

class FField;
class FFieldVariant;

// TODO
// non-UObject equivalent of UClass
//class FFieldClass
//{
//public:
//	/** The unique name of this field class (e.g., "IntProperty", "FloatProperty") */
//	FName Name;
//
//	/** A unique 64-bit ID used for high-performance casting (CastField) */
//	uint64 Id;
//
//	/** Pointer to the parent FFieldClass (the Super/Base class) */
//	FFieldClass* SuperClass;
//
//	/** Configuration flags for the class (e.g., bitfield for capability checks) */
//	EFieldClassFlags ClassFlags;
//
//	/** Pointer to the native constructor function for this field type. */
//	FField* (*ConstructFn)(const FFieldVariant&, const FName&, EObjectFlags);
//
//	/**
//	 * Verifies if this class is a child of the specified class.
//	 * Used internally by CastField.
//	 */
//	bool IsChildOf(const FFieldClass* InClass) const
//	{
//		for (const FFieldClass* Temp = this; Temp; Temp = Temp->SuperClass)
//		{
//			if (Temp == InClass) return true;
//		}
//		return false;
//	}
//
//	// ... registration and metadata logic
//};