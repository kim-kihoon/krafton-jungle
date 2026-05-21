#pragma once

#include "GameFramework/Pawn.h"

#include "AAnimLuaTestPawn.generated.h"

class UCameraComponent;
class USceneComponent;
class UScriptComponent;
class USkeletalMeshComponent;
class USpringArmComponent;

UCLASS()
class AAnimLuaTestPawn : public APawn
{
public:
    GENERATED_BODY(AAnimLuaTestPawn, APawn)

    AAnimLuaTestPawn() = default;

    void InitDefaultComponents() override;
    void HandleAnimNotify(const FAnimNotifyEvent& Notify) override;

    USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComp; }
    UCameraComponent* GetCameraComponent() const { return CameraComp; }
    UScriptComponent* GetScriptComponent() const { return ScriptComp; }

private:
    USceneComponent* SceneRoot = nullptr;
    USkeletalMeshComponent* SkeletalMeshComp = nullptr;
    USpringArmComponent* SpringArmComp = nullptr;
    UCameraComponent* CameraComp = nullptr;
    UScriptComponent* ScriptComp = nullptr;
};
