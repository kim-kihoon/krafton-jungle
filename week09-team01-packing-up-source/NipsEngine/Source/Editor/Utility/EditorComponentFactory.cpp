#include "EditorComponentFactory.h"

#include "Engine/GameFramework/AActor.h"
#include "Selection/SelectionManager.h"
#include "Component/StaticMeshComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "Component/Movement/PursuitMovementComponent.h"
#include "Component/SkyAtmosphereComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/SphereComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/Collision/CylinderComponent.h"
#include "Component/LuaScriptComponent.h"
#include "Component/AudioComponent.h"
#include "Component/AudioZoneComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Component/Physics/PhysicsHandleComponent.h"

// 새로운 컴포넌트를 레지스트리에 등록합니다. 특수한 설정(빌보드 붙이기 등)이 필요한 컴포넌트는 직접 설정합니다.
template<typename ComponentType>
UActorComponent* FEditorComponentFactory::RegisterComp(AActor* Actor)
{
    return Actor->AddComponent<ComponentType>();
}

template <>
UActorComponent* FEditorComponentFactory::RegisterComp<USceneComponent>(AActor* Actor)
{
    auto* Comp = Actor->AddComponent<USceneComponent>();

	UBillboardComponent* Billboard = Actor->AddComponent<UBillboardComponent>();
	Billboard->AttachToComponent(Comp);
	Billboard->SetEditorOnly(true);
	Billboard->SetHiddenInEditor(true);
	Billboard->SetTexturePath("Asset/Texture/Icons/EmptyActor.PNG");
    return Comp;
}

template <>
UActorComponent* FEditorComponentFactory::RegisterComp<USubUVComponent>(AActor* Actor)
{
    auto* Comp = Actor->AddComponent<USubUVComponent>();
    Comp->SetParticle(FName("Explosion"));
    Comp->SetSpriteSize(2.0f, 2.0f);
    Comp->SetFrameRate(30.f);
    return Comp;
}

template <>
UActorComponent* FEditorComponentFactory::RegisterComp<UTextRenderComponent>(AActor* Actor)
{
    auto* Comp = Actor->AddComponent<UTextRenderComponent>();
    Comp->SetFont(FName("Default"));
    Comp->SetText("TextRender");
    return Comp;
}

template <>
UActorComponent* FEditorComponentFactory::RegisterComp<UBillboardComponent>(AActor* Actor)
{
    auto* Comp = Actor->AddComponent<UBillboardComponent>();
    Comp->SetTexturePath("Asset/Texture/Pawn_64x.png");
    return Comp;
}

template <>
UActorComponent* FEditorComponentFactory::RegisterComp<UHeightFogComponent>(AActor* Actor)
{
    auto* Comp = Actor->AddComponent<UHeightFogComponent>();
    Comp->SetFogDensity(0);
    Comp->SetFogInscatteringColor(FVector4(0.72f, 0.8f, 0.9f, 1.0f));
	
	UBillboardComponent* Billboard = Actor->AddComponent<UBillboardComponent>();
	Billboard->AttachToComponent(Comp);
	Billboard->SetEditorOnly(true);
	Billboard->SetTexturePath("Asset/Texture/Icons/S_ExpoHeightFog.PNG");
    return Comp;
}

template <>
UActorComponent* FEditorComponentFactory::RegisterComp<UAudioZoneComponent>(AActor* Actor)
{
    auto* Comp = Actor->AddComponent<UAudioZoneComponent>();
	
	UBillboardComponent* Billboard = Actor->AddComponent<UBillboardComponent>();
	Billboard->AttachToComponent(Comp);
	Billboard->SetEditorOnly(true);
    Billboard->SetHiddenInEditor(true);
	Billboard->SetTexturePath("Asset/Texture/Icons/AudioVolume_64x.png");
    return Comp;
}

template <>
UActorComponent* FEditorComponentFactory::RegisterComp<URotatingMovementComponent>(AActor* Actor)
{
    auto* Comp = Actor->AddComponent<URotatingMovementComponent>();
    Comp->SetUpdatedComponent(Actor ? Actor->GetRootComponent() : nullptr);
    if (Actor)
    {
        Actor->SetTickInEditor(true);
    }
    return Comp;
}

template <typename LightType>
UActorComponent* FEditorComponentFactory::RegisterLightComp(AActor* Actor)
{
    auto* Comp = Actor->AddComponent<LightType>();

    auto* Billboard = Actor->AddComponent<UBillboardComponent>();
    Billboard->AttachToComponent(Comp);
    Billboard->SetEditorOnly(true);
    Billboard->SetHiddenInEditor(true);
    Billboard->SetTexturePath(LightType::BillboardTexturePath);
    return Comp;
}

// EditorPropertyWidget에 출력될 데이터를 담고 있는 배열입니다. 이 리스트만 관리하면 됩니다.
const TArray<FComponentMenuEntry>& FEditorComponentFactory::GetMenuRegistry()
{
    static const TArray<FComponentMenuEntry> Registry = {
        { "Scene Component", "Common", RegisterComp<USceneComponent> },
        { "StaticMesh Component", "Common", RegisterComp<UStaticMeshComponent> },
        { "SubUV Component", "Common", RegisterComp<USubUVComponent> },
        { "TextRender Component", "Common", RegisterComp<UTextRenderComponent> },
        { "Billboard Component", "Common", RegisterComp<UBillboardComponent> },
        { "HeightFog Component", "Common", RegisterComp<UHeightFogComponent> },
        { "SkyAtmosphere Component", "Common", RegisterComp<USkyAtmosphereComponent> },

        { "RotatingMovement Component", "Movement", RegisterComp<URotatingMovementComponent> },
        { "InterpToMovement Component", "Movement", RegisterComp<UInterpToMovementComponent> },
        { "PursuitMovement Component", "Movement", RegisterComp<UPursuitMovementComponent> },
        { "ProjectileMovement Component", "Movement", RegisterComp<UProjectileMovementComponent> },

        { "RigidBody Component", "Physics", RegisterComp<URigidBodyComponent> },
        { "PhysicsHandle Component", "Physics", RegisterComp<UPhysicsHandleComponent> },

        { "LuaScript Component", "Scripting", RegisterComp<ULuaScriptComponent> },
        { "Audio Component", "Audio", RegisterComp<UAudioComponent> },
        { "AudioZone Component", "Audio", RegisterComp<UAudioZoneComponent> },

        { "AmbientLight Component", "Light", RegisterLightComp<UAmbientLightComponent> },
        { "DirectionalLight Component", "Light", RegisterLightComp<UDirectionalLightComponent> },
        { "PointLight Component", "Light", RegisterLightComp<UPointLightComponent> },
        { "SpotLight Component", "Light", RegisterLightComp<USpotLightComponent> },

        { "Box Component", "Collision", RegisterComp<UBoxComponent> },
        { "Sphere Component", "Collision", RegisterComp<USphereComponent> },
        { "Capsule Component", "Collision", RegisterComp<UCapsuleComponent> },
        { "Cylinder Component", "Collision", RegisterComp<UCylinderComponent> },
    };

    return Registry;
}
