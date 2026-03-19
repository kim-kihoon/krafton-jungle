#pragma once
#include "Object.h"

class AActor;
class UWorld;
class UResourceManager;

class ULevel : public UObject
{
public:
	ULevel() = default;
	virtual ~ULevel() = default;

public:
	UWorld* GetWorld();

	void LoadLevel(UResourceManager& ResourceManager);
	// Actor는 GUObjectArray가 수명 관리 — Release에서는 목록만 비움
	virtual void Release() override;

public:
	UWorld*         OwningWorld = nullptr;

	//Actor는 참조만 함
	TArray<AActor*> Actors;
};

