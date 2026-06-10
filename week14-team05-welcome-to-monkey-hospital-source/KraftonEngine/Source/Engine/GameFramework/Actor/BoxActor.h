#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Actor/BoxActor.generated.h"
class UBoxComponent;

UCLASS()
class ABoxActor : public AActor
{
public:
	GENERATED_BODY()
	ABoxActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;

	UFUNCTION(Pure, Category="Actor|Components")
	UBoxComponent* GetBoxComponent() const { return BoxComponent; }

private:
	TWeakObjectPtr<UBoxComponent> BoxComponent = nullptr;
};
