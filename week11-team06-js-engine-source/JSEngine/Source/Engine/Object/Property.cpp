#include "Property.h"

#include "Class.h"
#include "Object.h"

std::unique_ptr<FObjectProperty> MakeObjectProperty(
    const char* Name,
    size_t Offset,
    size_t Size,
    EPropertyFlags Flags,
    UClass* ObjectClass)
{
    return std::make_unique<FObjectProperty>(Name, Offset, Size, Flags, ObjectClass);
}

void FProperty::SerializeItem(FArchive& Ar, UObject* Container) const
{
    if (!Container || HasPropertyFlag(Flags, EPropertyFlags::Transient))
    {
        return;
    }

    const FString PropertyKey(Name);
    if (Ar.IsLoading() && !Ar.HasKey(PropertyKey))
    {
        return;
    }

    Ar.SetCurrentKey(PropertyKey);
    SerializeValue(Ar, ContainerPtrToRawValuePtr(Container));
}

void FObjectProperty::SerializeValue(FArchive& Ar, void* ValuePtr) const
{
    UObject** ObjectPtr = static_cast<UObject**>(ValuePtr);
    if (!ObjectPtr)
    {
        return;
    }

    uint32 ObjectUUID = (Ar.IsSaving() && *ObjectPtr) ? (*ObjectPtr)->GetUUID() : 0;
    Ar << ObjectUUID;

    if (Ar.IsLoading())
    {
        *ObjectPtr = nullptr;
        UObjectManager::Get().RegisterPendingObjectReference(ObjectPtr, ObjectUUID, ObjectClass);
    }
}

size_t FArrayProperty::GetArrayNum(UObject* Container) const
{
    if (!Container || !ArrayOps || !HasPropertyFlag(Flags, EPropertyFlags::Read))
    {
        return 0;
    }

    void* ArrayPtr = ContainerPtrToRawValuePtr(Container);
    return ArrayPtr ? ArrayOps->Num(ArrayPtr) : 0;
}

bool FArrayProperty::Resize(UObject* Container, size_t NewSize) const
{
    if (!Container || !ArrayOps || !HasPropertyFlag(Flags, EPropertyFlags::Write))
    {
        return false;
    }

    void* ArrayPtr = ContainerPtrToRawValuePtr(Container);
    if (!ArrayPtr)
    {
        return false;
    }

    ArrayOps->Resize(ArrayPtr, NewSize);
    return true;
}

bool FArrayProperty::RemoveArrayElement(UObject* Container, size_t Index) const
{
    if (!Container || !ArrayOps || !HasPropertyFlag(Flags, EPropertyFlags::Write))
    {
        return false;
    }

    void* ArrayPtr = ContainerPtrToRawValuePtr(Container);
    if (!ArrayPtr || Index >= ArrayOps->Num(ArrayPtr))
    {
        return false;
    }

    ArrayOps->RemoveAt(ArrayPtr, Index);
    return true;
}

void FArrayProperty::SerializeValue(FArchive& Ar, void* ArrayPtr) const
{
    if (!ArrayPtr || !ArrayOps || !InnerProperty)
    {
        return;
    }

    int32 Num = Ar.IsSaving() ? static_cast<int32>(ArrayOps->Num(ArrayPtr)) : 0;
    Ar.BeginArray(Name ? Name : "", Num);

    if (Ar.IsLoading())
    {
        ArrayOps->Resize(ArrayPtr, static_cast<size_t>(Num));
    }

    for (int32 Index = 0; Index < Num; ++Index)
    {
        void* ElementPtr = ArrayOps->GetElementPtr(ArrayPtr, static_cast<size_t>(Index));
        InnerProperty->SerializeValue(Ar, ElementPtr);
    }

    Ar.EndArray();
}
