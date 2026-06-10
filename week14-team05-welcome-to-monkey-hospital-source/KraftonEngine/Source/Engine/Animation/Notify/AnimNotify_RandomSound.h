#pragma once

#include "Animation/Notify/AnimNotify.h"
#include "Animation/Notify/AnimNotify_PlaySound.h"
#include "Core/Types/CoreTypes.h"

#include "Source/Engine/Animation/Notify/AnimNotify_RandomSound.generated.h"

UCLASS()
class UAnimNotify_RandomSound : public UAnimNotify
{
public:
	GENERATED_BODY()
	UAnimNotify_RandomSound() = default;
	~UAnimNotify_RandomSound() override = default;

	UPROPERTY(Save, Category="RandomSound", DisplayName="Legacy Sound Paths")
	TArray<FString> SoundPaths;

	UPROPERTY(Save, Category="RandomSound", DisplayName="Legacy Volume")
	float Volume = 1.0f;

	UPROPERTY(Edit, Save, Category="RandomSound", DisplayName="Sounds", Type=Array, Struct=FAnimNotifySoundEntry)
	TArray<FAnimNotifySoundEntry> Sounds;

	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim) override;
};
