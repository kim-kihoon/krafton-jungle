#pragma once

#include "GameFramework/Pawn/LuaCharacter.h"

#include "Source/Game/Player/HospitalPlayerActor.generated.h"

UCLASS()
class AHospitalPlayerActor : public ALuaCharacter
{
public:
	GENERATED_BODY()
	AHospitalPlayerActor() = default;
	~AHospitalPlayerActor() override = default;
};
