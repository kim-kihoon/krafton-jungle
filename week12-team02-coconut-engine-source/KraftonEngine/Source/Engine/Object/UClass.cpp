#include "UClass.h"
#include "Serialization/Archive.h"

UClass UClass::StaticClassInstance(
	"UClass",
	UStruct::StaticClass(),
	sizeof(UClass),
	CF_None,
	CASTCLASS_UClass
);

UClass::UClass(const char* InName, UClass* InSuperClass, size_t InSize,
	uint32 InFlags, uint32 InCastFlags)
	: UStruct(InName, InSuperClass, InSize)
	, ClassFlags(InFlags)
	, OwnClassCastFlags(InCastFlags)
	, ClassCastFlags(InCastFlags)
{
	// FName + DeferStaticObject are handled by UField's ctor.
	GetAllClasses().push_back(this);
}

void UClass::Bind()
{
	if (bBound) return;

	ClassCastFlags = OwnClassCastFlags;
	if (UClass* Super = GetSuperClass())
	{
		Super->Bind();
		ClassCastFlags |= Super->ClassCastFlags;
	}
	bBound = true;
}

bool UClass::IsChildOf(const UClass* Other)
{
	if (Other == nullptr) return false;
	if (this == Other)    return true;

	// Fast path is valid only for classes that own a unique cast bit.
	const uint32 OtherOwnCast = Other->OwnClassCastFlags;
	if (OtherOwnCast != CASTCLASS_None)
	{
		Bind();
		return (ClassCastFlags & OtherOwnCast) == OtherOwnCast;
	}

	// SLOW PATH: walk the SuperStruct chain by pointer-equality.
	return UStruct::IsChildOf(Other);
}

void UClass::SerializeBin(FArchive& Ar, void* Data)
{
	TArray<const FProperty*> Properties;
	GetAllProperties(Properties);

	const bool bDuplicating = Ar.IsDuplicating();
	const bool bPIE = Ar.IsPIE();

	for (const FProperty* Property : Properties)
	{
		if (!Property)
		{
			continue;
		}

		if (bDuplicating)
		{
			const uint32 Flags = Property->PropertyFlag;
			if (Flags & CPF_DuplicateTransient)
			{
				continue;
			}
			if (!bPIE && (Flags & CPF_NonPIEDuplicateTransient))
			{
				continue;
			}
		}

		Property->SerializeItem(Ar, Property->ContainerPtrToValuePtr(Data), nullptr);
	}
}
