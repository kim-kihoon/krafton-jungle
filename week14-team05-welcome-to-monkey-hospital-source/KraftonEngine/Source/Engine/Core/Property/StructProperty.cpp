#include "StructProperty.h"

#include "Serialization/Archive.h"
#include "Object/Reflection/UStruct.h"

bool FStructProperty::ContainsObjectReference() const
{
	return StructType && StructType->HasObjectReferences();
}

void FStructProperty::AddReferencedObjects(void* ValuePtr, FReferenceCollector& Collector) const
{
	if (!ValuePtr || !StructType)
	{
		return;
	}

	FScopedReferenceName Scope(Collector, Name);
	const TArray<FGCReferenceToken>& Tokens = StructType->GetReferenceTokenStream();
	for (const FGCReferenceToken& Token : Tokens)
	{
		const FProperty* Child = Token.Property;
		if (!Child)
		{
			continue;
		}

		FScopedReferenceName ChildReferenceScope(Collector, Child->Name);
		Child->AddReferencedObjects(Child->GetValuePtrFor(ValuePtr), Collector);
	}
}

namespace
{
	void SerializeStructChildren(
		void* ValuePtr,
		FArchive& Ar,
		UStruct* StructType,
		uint32 RequiredFlags,
		const FPropertySerializeContext* Context)
	{
		if (!ValuePtr || !StructType)
		{
			return;
		}

		Ar.BeginObject();

		TArray<const FProperty*> Children;
		StructType->GetPropertyRefs(Children);
		for (const FProperty* Child : Children)
		{
			if (!Child || !Child->Name)
			{
				continue;
			}

			if (RequiredFlags != 0 && (Child->Flags & RequiredFlags) != RequiredFlags)
			{
				continue;
			}

			if (!Ar.HasProperty(Child->Name))
			{
				continue;
			}

			Ar.BeginProperty(Child->Name);
			if (Context)
			{
				// Versioned tagged object payloads already established property
				// presence at the outer object scope. Reusing the same context here
				// keeps nested reflected USTRUCT fields on the same compatibility
				// contract: missing fields stay default, unknown saved fields are
				// skipped, and semantic fixups belong to the owner's OnPostLoad seam.
				Child->SerializeValue(Child->GetValuePtrFor(ValuePtr), Ar, *Context);
			}
			else
			{
				Child->SerializeValue(Child->GetValuePtrFor(ValuePtr), Ar);
			}
			Ar.EndProperty();
		}

		Ar.EndObject();
	}
}

void FStructProperty::SerializeValue(void* ValuePtr, FArchive& Ar, const FPropertySerializeContext& Context) const
{
	SerializeStructChildren(ValuePtr, Ar, StructType, Context.RequiredFlags, &Context);
}

void FStructProperty::SerializeValue(void* ValuePtr, FArchive& Ar) const
{
	SerializeStructChildren(ValuePtr, Ar, StructType, 0, nullptr);
}
