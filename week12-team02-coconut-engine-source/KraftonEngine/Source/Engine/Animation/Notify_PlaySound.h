#pragma once
#include "Notify.h"

// notify 예시용 클래스
class UNotify_PlaySound : public UNotify
{
public:
	DECLARE_CLASS(UNotify_PlaySound, UNotify)

	virtual void OnNotify(AActor* MeshOwner, USkeletalMeshComponent* MeshComp) override;

public:
	FString AudioKey;
	float   Volume = 1.0f;
	bool    bLoop = false;
};
