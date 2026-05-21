#pragma once
#include "AActor.h"
#include "AReflectionTestActor.generated.h"

UCLASS()
class AReflectionTestActor : public AActor
{
    GENERATED_BODY(AReflectionTestActor, AActor)

	void InitDefaultComponents() override;
public:
    UPROPERTY(Edit, LuaRead, LuaWrite)
    float Speed = 0.0f;

    UPROPERTY(Edit, LuaRead, LuaWrite)
    float Health = 0.0f;
};
