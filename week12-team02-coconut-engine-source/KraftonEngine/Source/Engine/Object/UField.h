#pragma once
#include "Object/Object.h"

class UClass;

class UField : public UObject
{
public:
	UField() = default;
	UField(const char* InName)
		: Name(InName ? InName : "")
	{
		if (Name[0] != '\0')
		{
			SetFName(FName(Name));
		}
		// Reflected-type instances (UClass / UScriptStruct / UEnum) all derive
		// from UField, so a single deferred-registration call here covers every
		// leaf. EngineLoop drains the queue after FNamePool/GUObjectArray are
		// guaranteed live.
		FUObjectArray::DeferStaticObject(this);
	}
	virtual ~UField() = default;

	const char* GetName() const { return Name; }

	static UClass* StaticClass() { return &StaticClassInstance; }
	UClass* GetClass() const override { return StaticClass(); }

public:
	// TODO
	// UField* Next = nullptr;

	static UClass StaticClassInstance;

protected:
	const char* Name = nullptr;
};
