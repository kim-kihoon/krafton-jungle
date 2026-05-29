#include "UField.h"
#include "UClass.h"

UClass UField::StaticClassInstance(
	"UField",
	UObject::StaticClass(),
	sizeof(UField),
	CF_None,
	CASTCLASS_UField
);