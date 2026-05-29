#include "Notify_PlaySound.h"
#include "Core/Log.h"
#include "Object/ObjectFactory.h"
#include "Audio/AudioManager.h"
#include "Core/Property/PropertyTypes.h"
#include "NotifyRegistry.h"

IMPLEMENT_CLASS(UNotify_PlaySound, UNotify)

// --- PlaySoundNotify 프로퍼티 등록 ---
BEGIN_CLASS_PROPERTIES(UNotify_PlaySound)
	Cls->AddProperty(new FStringProperty("AudioKey", "Sound", CPF_Edit, static_cast<uint32>(offsetof(ThisClass, AudioKey)), static_cast<uint32>(sizeof(((ThisClass*)0)->AudioKey))));
	Cls->AddProperty(new FFloatProperty("Volume", "Sound", CPF_Edit, static_cast<uint32>(offsetof(ThisClass, Volume)), static_cast<uint32>(sizeof(((ThisClass*)0)->Volume)), 0.0f, 2.0f, 0.01f));
	Cls->AddProperty(new FBoolProperty("Loop", "Sound", CPF_Edit, static_cast<uint32>(offsetof(ThisClass, bLoop)), static_cast<uint32>(sizeof(((ThisClass*)0)->bLoop))));
END_CLASS_PROPERTIES(UNotify_PlaySound)

void UNotify_PlaySound::OnNotify(AActor* MeshOwner, USkeletalMeshComponent* MeshComp)
{
	if (AudioKey.empty())
		return;

	FAudioManager::Get().PlayAudio(AudioKey, Volume);
}

REGISTER_NOTIFY(UNotify_PlaySound)
