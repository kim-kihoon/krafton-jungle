#include "GameFramework/AnimLuaTestPawn.h"

#include "Animation/AnimationSequenceBase.h"
#include "Component/CameraComponent.h"
#include "Component/SceneComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/SpringArmComponent.h"
#include "Core/ResourceManager.h"
#include "Runtime/Script/ScriptComponent.h"

DEFINE_CLASS(AAnimLuaTestPawn, APawn)
REGISTER_FACTORY(AAnimLuaTestPawn)

void AAnimLuaTestPawn::InitDefaultComponents()
{
    SceneRoot = AddComponent<USceneComponent>();
    SetRootComponent(SceneRoot);

    SkeletalMeshComp = AddComponent<USkeletalMeshComponent>();
    SkeletalMeshComp->AttachToComponent(SceneRoot);
    SkeletalMeshComp->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
    SkeletalMeshComp->SetRelativeScale(FVector(0.01f, 0.01f, 0.01f));
    SkeletalMeshComp->SetSkeletalMesh(FResourceManager::Get().LoadSkeletalMesh("Asset/SkeletalMesh/GwenFBX/Gwen_skeletalmesh_Buffbone_Cstm_Healthbar.bin"));
    SkeletalMeshComp->SetUseSkinCache(true);

    SpringArmComp = AddComponent<USpringArmComponent>();
    SpringArmComp->AttachToComponent(SceneRoot);
    SpringArmComp->SetRelativeLocation(FVector(0.0f, 0.0f, 1.6f));
    SpringArmComp->SetTargetArmLength(5.0f);
    SpringArmComp->SetSocketOffset(FVector(0.0f, 0.0f, 0.25f));

    CameraComp = AddComponent<UCameraComponent>();
    CameraComp->AttachToComponent(SpringArmComp);

    ScriptComp = AddComponent<UScriptComponent>();
    ScriptComp->SetScriptName("AnimTest");

    AddTag("AnimLuaTestPawn");
}

void AAnimLuaTestPawn::HandleAnimNotify(const FAnimNotifyEvent& Notify)
{
    APawn::HandleAnimNotify(Notify);

    if (!ScriptComp || !Notify.NotifyName.IsValid())
    {
        return;
    }

    ScriptComp->CallScriptFunction("HandleAnimNotify", Notify.NotifyName.ToString());
}
