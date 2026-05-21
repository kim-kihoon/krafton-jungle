#include "Function.h"

bool UFunction::Invoke(UObject* Object) const
{
    if (!Object || !NativeFunction)
    {
        return false;
    }

    NativeFunction(Object);
    return true;
}
