#include "ScriptStruct.h"
#include "UClass.h"

UClass UScriptStruct::StaticClassInstance(
	"UScriptStruct",
	UStruct::StaticClass(),
	sizeof(UScriptStruct),
	CF_None,
	CASTCLASS_UScriptStruct
);

UScriptStruct::UScriptStruct(
	const char* InName,
	UScriptStruct* InSuperStruct,
	size_t InSize,
	size_t InAlignment,
	const ICppStructOps* InCppStructOps)
	: UStruct(InName, InSuperStruct, InSize)
	, Alignment(InAlignment)
	, CppStructOps(InCppStructOps)
{
	// FName + DeferStaticObject are handled by UField's ctor.
}

UScriptStruct::~UScriptStruct() = default;
