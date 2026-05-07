#include "GameFramework/PrimitiveActors.h"

#include "Component/DecalComponent.h"
#include "Component/CameraComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/SkyAtmosphereComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/AudioZoneComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/Physics/PhysicsHandleComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/SubUVComponent.h"
#include "Core/ResourceManager.h"
#include "GameFramework/World.h"
#include <format>

DEFINE_CLASS(APawnActor, AActor)
REGISTER_FACTORY(APawnActor)

DEFINE_CLASS(ASceneActor, AActor)
REGISTER_FACTORY(ASceneActor)

DEFINE_CLASS(AStaticMeshActor, AActor)
REGISTER_FACTORY(AStaticMeshActor)

DEFINE_CLASS(ASubUVActor, AActor)
REGISTER_FACTORY(ASubUVActor)

DEFINE_CLASS(ATextRenderActor, AActor)
REGISTER_FACTORY(ATextRenderActor)

DEFINE_CLASS(ABillboardActor, AActor)
REGISTER_FACTORY(ABillboardActor)

DEFINE_CLASS(ADecalActor, AActor)
REGISTER_FACTORY(ADecalActor)

DEFINE_CLASS(ALightActor, AActor)
// Base class, REGISTER_FACTORY 없음

DEFINE_CLASS(ADirectionalLightActor, ALightActor)
REGISTER_FACTORY(ADirectionalLightActor)

DEFINE_CLASS(AAmbientLightActor, ALightActor)
REGISTER_FACTORY(AAmbientLightActor)

DEFINE_CLASS(APointLightActor, ALightActor)
REGISTER_FACTORY(APointLightActor)

DEFINE_CLASS(ASpotLightActor, ALightActor)
REGISTER_FACTORY(ASpotLightActor)

DEFINE_CLASS(ASkyAtmosphereActor, AActor)
REGISTER_FACTORY(ASkyAtmosphereActor)

DEFINE_CLASS(AHeightFogActor, AActor)
REGISTER_FACTORY(AHeightFogActor)

DEFINE_CLASS(AAudioZoneActor, AActor)
REGISTER_FACTORY(AAudioZoneActor)

DEFINE_CLASS(APlayerStartActor, AActor)
REGISTER_FACTORY(APlayerStartActor)

void APawnActor::InitDefaultComponents()
{
	EnsureDefaultComponents();
}

void APawnActor::EnsureDefaultComponents()
{
	USceneComponent* SceneRoot = GetRootComponent();
	if (SceneRoot == nullptr)
	{
		SceneRoot = AddComponent<USceneComponent>();
		SetRootComponent(SceneRoot);
	}

	UCameraComponent* Camera = nullptr;
	UCapsuleComponent* Capsule = nullptr;
	URigidBodyComponent* RigidBody = nullptr;
	UCharacterMovementComponent* CharacterMovement = nullptr;
	UPhysicsHandleComponent* PhysicsHandle = nullptr;
	UBillboardComponent* Billboard = nullptr;

	for (UActorComponent* Component : GetComponents())
	{
		if (Camera == nullptr) Camera = Cast<UCameraComponent>(Component);
		if (Capsule == nullptr) Capsule = Cast<UCapsuleComponent>(Component);
		if (RigidBody == nullptr) RigidBody = Cast<URigidBodyComponent>(Component);
		if (CharacterMovement == nullptr) CharacterMovement = Cast<UCharacterMovementComponent>(Component);
		if (PhysicsHandle == nullptr) PhysicsHandle = Cast<UPhysicsHandleComponent>(Component);
		if (Billboard == nullptr) Billboard = Cast<UBillboardComponent>(Component);
	}

	const bool bCreatedCapsule = Capsule == nullptr;
	if (bCreatedCapsule)
	{
		Capsule = AddComponent<UCapsuleComponent>();
		Capsule->AttachToComponent(SceneRoot);
		Capsule->SetRelativeLocation(FVector(0.0f, 0.0f, 0.85f));
		Capsule->SetCapsuleSize(0.85f, 0.28f);
	}
	Capsule->SetBlockComponent(true);

	if (RigidBody == nullptr)
	{
		RigidBody = AddComponent<URigidBodyComponent>();
	}
	RigidBody->SetUpdatedComponent(SceneRoot);
	RigidBody->SetBodyType(EPhysicsBodyType::Kinematic);
	RigidBody->SetSimulatePhysics(true);
	RigidBody->SetUseGravity(false);
	RigidBody->SetCanBePickedUp(false);

	if (CharacterMovement == nullptr)
	{
		CharacterMovement = AddComponent<UCharacterMovementComponent>();
	}
	CharacterMovement->SetActive(true);
	CharacterMovement->SetComponentTickEnabled(true);
	CharacterMovement->SetUpdatedComponent(SceneRoot);
	CharacterMovement->SetRigidBody(RigidBody);

	const bool bCreatedCamera = Camera == nullptr;
	if (bCreatedCamera)
	{
		Camera = AddComponent<UCameraComponent>();
		Camera->AttachToComponent(SceneRoot);
		Camera->SetRelativeLocation(FVector(0.0f, 0.0f, 1.55f));
	}
	else if (Camera->GetParent() == nullptr)
	{
		Camera->AttachToComponent(SceneRoot);
	}

	if (PhysicsHandle == nullptr)
	{
		AddComponent<UPhysicsHandleComponent>();
	}

	if (Billboard == nullptr)
	{
		Billboard = AddComponent<UBillboardComponent>();
		Billboard->AttachToComponent(SceneRoot);
		Billboard->SetEditorOnly(true);
		Billboard->SetHiddenInEditor(true);
		Billboard->SetTexturePath("Asset/Texture/Pawn_64x.png");
	}
}

void ASceneActor::InitDefaultComponents()
{
	auto SceneRoot = AddComponent<USceneComponent>();
	SetRootComponent(SceneRoot);

	UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
	Billboard->AttachToComponent(SceneRoot);
	Billboard->SetEditorOnly(true);
	Billboard->SetHiddenInEditor(true);
	Billboard->SetTexturePath("Asset/Texture/Icons/EmptyActor.PNG");
}

void AStaticMeshActor::InitDefaultComponents()
{
	auto* StaticMesh = AddComponent<UStaticMeshComponent>();
	SetRootComponent(StaticMesh);
}

void ASubUVActor::InitDefaultComponents()
{
	SetTickInEditor(true); // Editor Tick을 받도록 변경

    auto* SubUV = AddComponent<USubUVComponent>();
    SetRootComponent(SubUV);
	SubUV->SetParticle(FName("Explosion"));
	SubUV->SetSpriteSize(2.0f, 2.0f);
	SubUV->SetFrameRate(30.f);
}

void ATextRenderActor::InitDefaultComponents()
{
	UTextRenderComponent* Text = AddComponent<UTextRenderComponent>();
	SetRootComponent(Text);
	Text->SetFont(FName("Default"));
	Text->SetText("TextRender");
}

void ABillboardActor::InitDefaultComponents()
{
	UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
	SetRootComponent(Billboard);
	Billboard->SetTexturePath(("Asset/Texture/Pawn_64x.png"));
	Billboard->SetEditorOnly(true);
}

void ADecalActor::InitDefaultComponents()
{
	UDecalComponent* Decal = AddComponent<UDecalComponent>();
	SetRootComponent(Decal);

	UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
	Billboard->AttachToComponent(Decal);
	Billboard->SetEditorOnly(true);
	Billboard->SetHiddenInEditor(true);
	Billboard->SetTexturePath("Asset/Texture/Icons/S_DecalActorIcon.PNG");
}

void ADirectionalLightActor::InitDefaultComponents()
{
	SetTickInEditor(true);

	UDirectionalLightComponent* DirLight = AddComponent<UDirectionalLightComponent>();
	SetRootComponent(DirLight);
	SetupBillboard(DirLight);
}

void AAmbientLightActor::InitDefaultComponents()
{
	UAmbientLightComponent* AmbientLight = AddComponent<UAmbientLightComponent>();
	SetRootComponent(AmbientLight);
	SetupBillboard(AmbientLight);
}

void APointLightActor::InitDefaultComponents()
{
	UPointLightComponent* PointLight = AddComponent<UPointLightComponent>();
	SetRootComponent(PointLight);
	SetupBillboard(PointLight);
}

void ASpotLightActor::InitDefaultComponents()
{
	USpotLightComponent* SpotLight = AddComponent<USpotLightComponent>();
	SetRootComponent(SpotLight);
	SetupBillboard(SpotLight);
}


void ASkyAtmosphereActor::InitDefaultComponents()
{
	USkyAtmosphereComponent* SkyAtmosphere = AddComponent<USkyAtmosphereComponent>();
	SetRootComponent(SkyAtmosphere);
	
	UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
	Billboard->AttachToComponent(SkyAtmosphere);
	Billboard->SetEditorOnly(true);
	Billboard->SetHiddenInEditor(true);
	Billboard->SetTexturePath("Asset/Texture/Icons/SkyLight.PNG");
}

void AHeightFogActor::InitDefaultComponents()
{
	UHeightFogComponent* HeightFog = AddComponent<UHeightFogComponent>();
	SetRootComponent(HeightFog);
	
	UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
	Billboard->AttachToComponent(HeightFog);
	Billboard->SetEditorOnly(true);
	Billboard->SetHiddenInEditor(true);
	Billboard->SetTexturePath("Asset/Texture/Icons/S_ExpoHeightFog.PNG");
}

void AAudioZoneActor::InitDefaultComponents()
{
	UAudioZoneComponent* AudioZone = AddComponent<UAudioZoneComponent>();
	SetRootComponent(AudioZone);

	UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
	Billboard->AttachToComponent(AudioZone);
	Billboard->SetEditorOnly(true);
	Billboard->SetHiddenInEditor(true);
	Billboard->SetTexturePath("Asset/Texture/Icons/AudioVolume_64x.png");
}

void APlayerStartActor::InitDefaultComponents()
{
	USceneComponent* SceneRoot = AddComponent<USceneComponent>();
	SetRootComponent(SceneRoot);

	UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
	Billboard->AttachToComponent(SceneRoot);
	Billboard->SetEditorOnly(true);
	Billboard->SetHiddenInEditor(true);
	Billboard->SetTexturePath("Asset/Texture/Pawn_64x.png");
}

void ALightActor::PostDuplicate(UObject* Original)
{
    AActor::PostDuplicate(Original);

    ULightComponentBase* LightComp = Cast<ULightComponentBase>(GetRootComponent());
    if (!LightComp)
    {
        for (UActorComponent* Comp : GetComponents())
        {
            if (LightComp = Cast<ULightComponentBase>(Comp))
                break;
        }
    }

    if (LightComp)
    {
        SetupBillboard(LightComp);
    }
}

void ALightActor::SetupBillboard(USceneComponent* Root)
{
    ULightComponentBase* LightComp = Cast<ULightComponentBase>(Root);
    if (LightComp && LightComp->GetBillboardTexturePath())
    {
        UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
        Billboard->AttachToComponent(LightComp);
        Billboard->SetEditorOnly(true);
        Billboard->SetHiddenInEditor(true);
        Billboard->SetTexturePath(LightComp->GetBillboardTexturePath());
    }
}
