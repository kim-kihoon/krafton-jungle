#include "Animation/AnimationSequenceBase.h"

void UAnimationSequenceBase::AddNotify(const FAnimNotifyEvent& Notify)
{
    if (!Notify.NotifyName.IsValid())
    {
        return;
    }

    Notifies.push_back(Notify);
    bIsDirty = true;
}

void UAnimationSequenceBase::ClearNotifies()
{
    Notifies.clear();
	bIsDirty = true;
}

bool UAnimationSequenceBase::RemoveNotifyAt(int32 NotifyIndex)
{
    if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(Notifies.size()))
    {
        return false;
    }

    Notifies.erase(Notifies.begin() + NotifyIndex);
    bIsDirty = true;
    return true;
}
