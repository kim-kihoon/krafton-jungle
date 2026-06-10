#include "AnimSequenceBase.h"
#include "Animation/Notify/AnimNotify.h"
#include "Animation/Notify/AnimNotifyState.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
void UAnimSequenceBase::Serialize(FArchive& Ar)
{
    UObject::Serialize(Ar);

    Ar << PlayLength;
    Ar << FrameRate;
    Ar << Notifies;
}


void UAnimSequenceBase::AddReferencedObjects(FReferenceCollector& Collector)
{
    UObject::AddReferencedObjects(Collector);
    for (FAnimNotifyEvent& NotifyEvent : Notifies)
    {
        if (NotifyEvent.Notify && !IsValid(NotifyEvent.Notify))
        {
            NotifyEvent.Notify = nullptr;
        }
        if (NotifyEvent.NotifyState && !IsValid(NotifyEvent.NotifyState))
        {
            NotifyEvent.NotifyState = nullptr;
        }
        Collector.AddReferencedObject(NotifyEvent.Notify, "AnimSequenceBase.Notify");
        Collector.AddReferencedObject(NotifyEvent.NotifyState, "AnimSequenceBase.NotifyState");
    }
}
