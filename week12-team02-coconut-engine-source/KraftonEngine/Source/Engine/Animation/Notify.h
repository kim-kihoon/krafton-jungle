#pragma once
#include "Object/Object.h"

class AActor;
class USkeletalMeshComponent;

class UNotify : public UObject
{
public:
	DECLARE_CLASS(UNotify, UObject)

	virtual void OnNotify(AActor* MeshOwner, USkeletalMeshComponent* MeshComp);
};
