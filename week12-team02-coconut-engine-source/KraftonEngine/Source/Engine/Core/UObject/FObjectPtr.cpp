#include "FObjectPtr.h"

FObjectPtr::FObjectPtr(UObject* InObject)
{
	Handle = InObject;
}

FObjectPtr::FObjectPtr(int32 Index)
{
	const auto& Items = GUObjectArray.GetItems();
	Handle = (Index >= 0 && Index < (int32)Items.size()) ? Items[Index].Object : nullptr;
}

FObjectPtr& FObjectPtr::operator=(UObject* Other)
{
	Handle = Other;
	return *this;
}

FObjectPtr& FObjectPtr::operator=(std::nullptr_t)
{
	Handle   = nullptr;
	return *this;
}

UObject* FObjectPtr::Get() const
{
	return Handle;
}

bool FObjectPtr::IsValid() const 
{
	return ::IsValid(Handle);
}

UClass* FObjectPtr::GetClass() const 
{
	if (!Handle) return nullptr;
	return Handle->GetClass();
}

FName FObjectPtr::GetFName() const
{
	if (!Handle) return FName::None;
	return Handle->GetFName();
}

FString FObjectPtr::GetName() const 
{
	if (!Handle) return FString();
	return Handle->GetName();
}

FString FObjectPtr::GetPathName() const
{
	return Handle ? Handle->GetPathName() : FString("None");
}

FObjectPtr FObjectPtr::GetOuter() const
{
	return Handle ? FObjectPtr(Handle->GetOuter()) : FObjectPtr(nullptr);
}

FObjectPtr FObjectPtr::GetPackage() const
{
	// No UPackage in this codebase — "package" == topmost outer (the root).
	// If Handle has no outer, Handle itself is the root.
	if (!Handle) return FObjectPtr(nullptr);

	UObject* Root = Handle;
	while (UObject* Next = Root->GetOuter())
	{
		if (!::IsValid(Next)) break;
		Root = Next;
	}
	return FObjectPtr(Root);
}

bool FObjectPtr::IsIn(FObjectPtr SomeOuter) const
{
	UObject* Target = SomeOuter.Get();
	if (!Handle || !Target) return false;

	for (UObject* O = Handle->GetOuter(); ::IsValid(O); O = O->GetOuter())
	{
		if (O == Target) return true;
	}
	return false;
}

bool FObjectPtr::IsA(const UClass* SomeBase) const
{
	if (!Handle || !SomeBase) return false;
	return Handle->GetClass()->IsChildOf(SomeBase);
}

uint32 GetTypeHash(const FObjectPtr& P) 
{ 
	return static_cast<uint32>(reinterpret_cast<uintptr_t>(P.Handle));
}