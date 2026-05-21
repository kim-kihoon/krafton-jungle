// This file is generated. Do not edit manually.

#include "../Source/Engine/Animation/ActorSequence.h"
#include "../Source/Engine/Animation/AnimationSequenceBase.h"
#include "../Source/Engine/Animation/AnimInstance.h"
#include "../Source/Engine/Animation/AnimSingleNodeInstance.h"
#include "../Source/Engine/Animation/AnimStateMachineAsset.h"
#include "../Source/Engine/Animation/AnimStateMachineAssetInstance.h"
#include "../Source/Engine/Asset/AnimationSequence.h"
#include "../Source/Engine/Component/ActorSequenceComponent.h"
#include "../Source/Engine/Component/BillboardComponent.h"
#include "../Source/Engine/Component/BoxComponent.h"
#include "../Source/Engine/Component/CameraComponent.h"
#include "../Source/Engine/Component/CapsuleComponent.h"
#include "../Source/Engine/Component/DecalComponent.h"
#include "../Source/Engine/Component/FireballComponent.h"
#include "../Source/Engine/Component/GizmoComponent.h"
#include "../Source/Engine/Component/HeightFogComponent.h"
#include "../Source/Engine/Component/MeshComponent.h"
#include "../Source/Engine/Component/Movement/InterpToMovementComponent.h"
#include "../Source/Engine/Component/Movement/MovementComponent.h"
#include "../Source/Engine/Component/Movement/ProjectileMovementComponent.h"
#include "../Source/Engine/Component/Movement/PursuitMovementComponent.h"
#include "../Source/Engine/Component/Movement/RotatingMovementComponent.h"
#include "../Source/Engine/Component/PostProcess/Light/AmbientLightComponent.h"
#include "../Source/Engine/Component/PostProcess/Light/DirectionalLightComponent.h"
#include "../Source/Engine/Component/PostProcess/Light/LightComponent.h"
#include "../Source/Engine/Component/PostProcess/Light/LightComponentBase.h"
#include "../Source/Engine/Component/PostProcess/Light/PointLightComponent.h"
#include "../Source/Engine/Component/PostProcess/Light/SpotlightComponent.h"
#include "../Source/Engine/Component/PrimitiveComponent.h"
#include "../Source/Engine/Component/ProceduralMeshComponent.h"
#include "../Source/Engine/Component/ShapeComponent.h"
#include "../Source/Engine/Component/SkeletalMeshComponent.h"
#include "../Source/Engine/Component/SkinnedMeshComponent.h"
#include "../Source/Engine/Component/SoundComponent.h"
#include "../Source/Engine/Component/SphereComponent.h"
#include "../Source/Engine/Component/SpringArmComponent.h"
#include "../Source/Engine/Component/StaticMeshComponent.h"
#include "../Source/Engine/Component/SubUVComponent.h"
#include "../Source/Engine/Component/TextRenderComponent.h"
#include "../Source/Engine/GameFramework/AnimLuaTestPawn.h"
#include "../Source/Engine/GameFramework/AnimTestPawn.h"
#include "../Source/Engine/GameFramework/AReflectionTestActor.h"
#include "../Source/Engine/GameFramework/DefaultPawn.h"
#include "../Source/Engine/GameFramework/GameModeBase.h"
#include "../Source/Engine/GameFramework/Pawn.h"
#include "../Source/Engine/GameFramework/PlayerController.h"
#include "../Source/Engine/GameFramework/PrimitiveActors.h"

#include "Object/Class.h"
#include "Object/ObjectFactory.h"
#include "Object/ReflectionRegistry.h"

const FTypeInfo UActorSequence::s_TypeInfo = {
    "UActorSequence",
    &UObject::s_TypeInfo,
    sizeof(UActorSequence)
};

UClass* UActorSequence::StaticClass()
{
    static UClass Class(
        "UActorSequence",
        Super::StaticClass(),
        sizeof(UActorSequence),
        CF_None,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UActorSequence>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        Class.AddProperty(MakeProperty<FFloatProperty>(
                            "StartTime",
                            offsetof(UActorSequence, StartTime),
                            sizeof(float),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FFloatProperty>(
                            "Duration",
                            offsetof(UActorSequence, Duration),
                            sizeof(float),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FBoolProperty>(
                            "bLoop",
                            offsetof(UActorSequence, bLoop),
                            sizeof(bool),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUActorSequence
    {
        FAutoRegisterUActorSequence()
        {
            UActorSequence::StaticClass();
        }
    };

    static FAutoRegisterUActorSequence GAutoRegisterUActorSequence;
}

const FTypeInfo UActorSequencePlayer::s_TypeInfo = {
    "UActorSequencePlayer",
    &UObject::s_TypeInfo,
    sizeof(UActorSequencePlayer)
};

UClass* UActorSequencePlayer::StaticClass()
{
    static UClass Class(
        "UActorSequencePlayer",
        Super::StaticClass(),
        sizeof(UActorSequencePlayer),
        CF_None,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UActorSequencePlayer>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        Class.AddProperty(MakeObjectProperty(
                            "OwnerComponent",
                            offsetof(UActorSequencePlayer, OwnerComponent),
                            sizeof(UActorSequenceComponent*),
                            EPropertyFlags::Transient |
                                EPropertyFlags::Read,
                            UActorSequenceComponent::StaticClass()));

        Class.AddProperty(MakeObjectProperty(
                            "Sequence",
                            offsetof(UActorSequencePlayer, Sequence),
                            sizeof(UActorSequence*),
                            EPropertyFlags::Transient |
                                EPropertyFlags::Read,
                            UActorSequence::StaticClass()));

        Class.AddProperty(MakeProperty<FFloatProperty>(
                            "CurrentTime",
                            offsetof(UActorSequencePlayer, CurrentTime),
                            sizeof(float),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FFloatProperty>(
                            "PlayRate",
                            offsetof(UActorSequencePlayer, PlayRate),
                            sizeof(float),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FFloatProperty>(
                            "StartOffsetSeconds",
                            offsetof(UActorSequencePlayer, StartOffsetSeconds),
                            sizeof(float),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FBoolProperty>(
                            "bPlaying",
                            offsetof(UActorSequencePlayer, bPlaying),
                            sizeof(bool),
                            EPropertyFlags::Read |
                                EPropertyFlags::LuaRead));

        Class.AddProperty(MakeProperty<FBoolProperty>(
                            "bPaused",
                            offsetof(UActorSequencePlayer, bPaused),
                            sizeof(bool),
                            EPropertyFlags::Read |
                                EPropertyFlags::LuaRead));

        Class.AddProperty(MakeProperty<FBoolProperty>(
                            "bPauseAtEnd",
                            offsetof(UActorSequencePlayer, bPauseAtEnd),
                            sizeof(bool),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FBoolProperty>(
                            "bResolveDirty",
                            offsetof(UActorSequencePlayer, bResolveDirty),
                            sizeof(bool),
                            EPropertyFlags::Transient |
                                EPropertyFlags::Read));

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUActorSequencePlayer
    {
        FAutoRegisterUActorSequencePlayer()
        {
            UActorSequencePlayer::StaticClass();
        }
    };

    static FAutoRegisterUActorSequencePlayer GAutoRegisterUActorSequencePlayer;
}

const FTypeInfo UAnimationSequenceBase::s_TypeInfo = {
    "UAnimationSequenceBase",
    &UObject::s_TypeInfo,
    sizeof(UAnimationSequenceBase)
};

UClass* UAnimationSequenceBase::StaticClass()
{
    static UClass Class(
        "UAnimationSequenceBase",
        Super::StaticClass(),
        sizeof(UAnimationSequenceBase),
        CF_None,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UAnimationSequenceBase>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUAnimationSequenceBase
    {
        FAutoRegisterUAnimationSequenceBase()
        {
            UAnimationSequenceBase::StaticClass();
        }
    };

    static FAutoRegisterUAnimationSequenceBase GAutoRegisterUAnimationSequenceBase;
}

const FTypeInfo UAnimInstance::s_TypeInfo = {
    "UAnimInstance",
    &UObject::s_TypeInfo,
    sizeof(UAnimInstance)
};

UClass* UAnimInstance::StaticClass()
{
    static UClass Class(
        "UAnimInstance",
        Super::StaticClass(),
        sizeof(UAnimInstance),
        CF_None,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UAnimInstance>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        Class.AddProperty(MakeObjectProperty(
                            "SkelMeshComponent",
                            offsetof(UAnimInstance, SkelMeshComponent),
                            sizeof(USkeletalMeshComponent*),
                            EPropertyFlags::Transient |
                                EPropertyFlags::Read,
                            USkeletalMeshComponent::StaticClass()));

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUAnimInstance
    {
        FAutoRegisterUAnimInstance()
        {
            UAnimInstance::StaticClass();
        }
    };

    static FAutoRegisterUAnimInstance GAutoRegisterUAnimInstance;
}

const FTypeInfo UAnimSingleNodeInstance::s_TypeInfo = {
    "UAnimSingleNodeInstance",
    &UAnimInstance::s_TypeInfo,
    sizeof(UAnimSingleNodeInstance)
};

UClass* UAnimSingleNodeInstance::StaticClass()
{
    static UClass Class(
        "UAnimSingleNodeInstance",
        Super::StaticClass(),
        sizeof(UAnimSingleNodeInstance),
        CF_None,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUAnimSingleNodeInstance
    {
        FAutoRegisterUAnimSingleNodeInstance()
        {
            UAnimSingleNodeInstance::StaticClass();
        }
    };

    static FAutoRegisterUAnimSingleNodeInstance GAutoRegisterUAnimSingleNodeInstance;
}

const FTypeInfo UAnimStateMachineAsset::s_TypeInfo = {
    "UAnimStateMachineAsset",
    &UObject::s_TypeInfo,
    sizeof(UAnimStateMachineAsset)
};

UClass* UAnimStateMachineAsset::StaticClass()
{
    static UClass Class(
        "UAnimStateMachineAsset",
        Super::StaticClass(),
        sizeof(UAnimStateMachineAsset),
        CF_None,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UAnimStateMachineAsset>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUAnimStateMachineAsset
    {
        FAutoRegisterUAnimStateMachineAsset()
        {
            UAnimStateMachineAsset::StaticClass();
        }
    };

    static FAutoRegisterUAnimStateMachineAsset GAutoRegisterUAnimStateMachineAsset;
}

const FTypeInfo UAnimStateMachineAssetInstance::s_TypeInfo = {
    "UAnimStateMachineAssetInstance",
    &UAnimInstance::s_TypeInfo,
    sizeof(UAnimStateMachineAssetInstance)
};

UClass* UAnimStateMachineAssetInstance::StaticClass()
{
    static UClass Class(
        "UAnimStateMachineAssetInstance",
        Super::StaticClass(),
        sizeof(UAnimStateMachineAssetInstance),
        CF_None,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UAnimStateMachineAssetInstance>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        Class.AddProperty(MakeProperty<FAnimStateMachineAssetProperty>(
                            "StateMachineAsset",
                            offsetof(UAnimStateMachineAssetInstance, StateMachineAsset),
                            sizeof(FAnimStateMachineAssetRef),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUAnimStateMachineAssetInstance
    {
        FAutoRegisterUAnimStateMachineAssetInstance()
        {
            UAnimStateMachineAssetInstance::StaticClass();
        }
    };

    static FAutoRegisterUAnimStateMachineAssetInstance GAutoRegisterUAnimStateMachineAssetInstance;
}

const FTypeInfo UAnimationSequence::s_TypeInfo = {
    "UAnimationSequence",
    &UAnimationSequenceBase::s_TypeInfo,
    sizeof(UAnimationSequence)
};

UClass* UAnimationSequence::StaticClass()
{
    static UClass Class(
        "UAnimationSequence",
        Super::StaticClass(),
        sizeof(UAnimationSequence),
        CF_None,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UAnimationSequence>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUAnimationSequence
    {
        FAutoRegisterUAnimationSequence()
        {
            UAnimationSequence::StaticClass();
        }
    };

    static FAutoRegisterUAnimationSequence GAutoRegisterUAnimationSequence;
}

const FTypeInfo UActorSequenceComponent::s_TypeInfo = {
    "UActorSequenceComponent",
    &UActorComponent::s_TypeInfo,
    sizeof(UActorSequenceComponent)
};

UClass* UActorSequenceComponent::StaticClass()
{
    static UClass Class(
        "UActorSequenceComponent",
        Super::StaticClass(),
        sizeof(UActorSequenceComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UActorSequenceComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        Class.AddProperty(MakeObjectProperty(
                            "Sequence",
                            offsetof(UActorSequenceComponent, Sequence),
                            sizeof(UActorSequence*),
                            EPropertyFlags::Transient |
                                EPropertyFlags::Read,
                            UActorSequence::StaticClass()));

        Class.AddProperty(MakeObjectProperty(
                            "SequencePlayer",
                            offsetof(UActorSequenceComponent, SequencePlayer),
                            sizeof(UActorSequencePlayer*),
                            EPropertyFlags::Transient |
                                EPropertyFlags::Read,
                            UActorSequencePlayer::StaticClass()));

        Class.AddProperty(MakeObjectProperty(
                            "PreviewSequencePlayer",
                            offsetof(UActorSequenceComponent, PreviewSequencePlayer),
                            sizeof(UActorSequencePlayer*),
                            EPropertyFlags::Transient |
                                EPropertyFlags::Read,
                            UActorSequencePlayer::StaticClass()));

        Class.AddProperty(MakeProperty<FBoolProperty>(
                            "bAutoPlay",
                            offsetof(UActorSequenceComponent, bAutoPlay),
                            sizeof(bool),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FBoolProperty>(
                            "bPauseAtEnd",
                            offsetof(UActorSequenceComponent, bPauseAtEnd),
                            sizeof(bool),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FFloatProperty>(
                            "PlayRate",
                            offsetof(UActorSequenceComponent, PlayRate),
                            sizeof(float),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FFloatProperty>(
                            "StartOffsetSeconds",
                            offsetof(UActorSequenceComponent, StartOffsetSeconds),
                            sizeof(float),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUActorSequenceComponent
    {
        FAutoRegisterUActorSequenceComponent()
        {
            UActorSequenceComponent::StaticClass();
        }
    };

    static FAutoRegisterUActorSequenceComponent GAutoRegisterUActorSequenceComponent;
}

UClass* UBillboardComponent::StaticClass()
{
    static UClass Class(
        "UBillboardComponent",
        Super::StaticClass(),
        sizeof(UBillboardComponent),
        CF_Component,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUBillboardComponent
    {
        FAutoRegisterUBillboardComponent()
        {
            UBillboardComponent::StaticClass();
        }
    };

    static FAutoRegisterUBillboardComponent GAutoRegisterUBillboardComponent;
}

UClass* UBoxComponent::StaticClass()
{
    static UClass Class(
        "UBoxComponent",
        Super::StaticClass(),
        sizeof(UBoxComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UBoxComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUBoxComponent
    {
        FAutoRegisterUBoxComponent()
        {
            UBoxComponent::StaticClass();
        }
    };

    static FAutoRegisterUBoxComponent GAutoRegisterUBoxComponent;
}

UClass* UCameraComponent::StaticClass()
{
    static UClass Class(
        "UCameraComponent",
        Super::StaticClass(),
        sizeof(UCameraComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UCameraComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUCameraComponent
    {
        FAutoRegisterUCameraComponent()
        {
            UCameraComponent::StaticClass();
        }
    };

    static FAutoRegisterUCameraComponent GAutoRegisterUCameraComponent;
}

UClass* UCapsuleComponent::StaticClass()
{
    static UClass Class(
        "UCapsuleComponent",
        Super::StaticClass(),
        sizeof(UCapsuleComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UCapsuleComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUCapsuleComponent
    {
        FAutoRegisterUCapsuleComponent()
        {
            UCapsuleComponent::StaticClass();
        }
    };

    static FAutoRegisterUCapsuleComponent GAutoRegisterUCapsuleComponent;
}

UClass* UDecalComponent::StaticClass()
{
    static UClass Class(
        "UDecalComponent",
        Super::StaticClass(),
        sizeof(UDecalComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UDecalComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUDecalComponent
    {
        FAutoRegisterUDecalComponent()
        {
            UDecalComponent::StaticClass();
        }
    };

    static FAutoRegisterUDecalComponent GAutoRegisterUDecalComponent;
}

UClass* UFireballComponent::StaticClass()
{
    static UClass Class(
        "UFireballComponent",
        Super::StaticClass(),
        sizeof(UFireballComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UFireballComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUFireballComponent
    {
        FAutoRegisterUFireballComponent()
        {
            UFireballComponent::StaticClass();
        }
    };

    static FAutoRegisterUFireballComponent GAutoRegisterUFireballComponent;
}

UClass* UGizmoComponent::StaticClass()
{
    static UClass Class(
        "UGizmoComponent",
        Super::StaticClass(),
        sizeof(UGizmoComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UGizmoComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUGizmoComponent
    {
        FAutoRegisterUGizmoComponent()
        {
            UGizmoComponent::StaticClass();
        }
    };

    static FAutoRegisterUGizmoComponent GAutoRegisterUGizmoComponent;
}

UClass* UHeightFogComponent::StaticClass()
{
    static UClass Class(
        "UHeightFogComponent",
        Super::StaticClass(),
        sizeof(UHeightFogComponent),
        CF_Component,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUHeightFogComponent
    {
        FAutoRegisterUHeightFogComponent()
        {
            UHeightFogComponent::StaticClass();
        }
    };

    static FAutoRegisterUHeightFogComponent GAutoRegisterUHeightFogComponent;
}

UClass* UMeshComponent::StaticClass()
{
    static UClass Class(
        "UMeshComponent",
        Super::StaticClass(),
        sizeof(UMeshComponent),
        CF_Component,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUMeshComponent
    {
        FAutoRegisterUMeshComponent()
        {
            UMeshComponent::StaticClass();
        }
    };

    static FAutoRegisterUMeshComponent GAutoRegisterUMeshComponent;
}

UClass* UInterpToMovementComponent::StaticClass()
{
    static UClass Class(
        "UInterpToMovementComponent",
        Super::StaticClass(),
        sizeof(UInterpToMovementComponent),
        CF_Component,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUInterpToMovementComponent
    {
        FAutoRegisterUInterpToMovementComponent()
        {
            UInterpToMovementComponent::StaticClass();
        }
    };

    static FAutoRegisterUInterpToMovementComponent GAutoRegisterUInterpToMovementComponent;
}

UClass* UMovementComponent::StaticClass()
{
    static UClass Class(
        "UMovementComponent",
        Super::StaticClass(),
        sizeof(UMovementComponent),
        CF_Component,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUMovementComponent
    {
        FAutoRegisterUMovementComponent()
        {
            UMovementComponent::StaticClass();
        }
    };

    static FAutoRegisterUMovementComponent GAutoRegisterUMovementComponent;
}

UClass* UProjectileMovementComponent::StaticClass()
{
    static UClass Class(
        "UProjectileMovementComponent",
        Super::StaticClass(),
        sizeof(UProjectileMovementComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UProjectileMovementComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUProjectileMovementComponent
    {
        FAutoRegisterUProjectileMovementComponent()
        {
            UProjectileMovementComponent::StaticClass();
        }
    };

    static FAutoRegisterUProjectileMovementComponent GAutoRegisterUProjectileMovementComponent;
}

UClass* UPursuitMovementComponent::StaticClass()
{
    static UClass Class(
        "UPursuitMovementComponent",
        Super::StaticClass(),
        sizeof(UPursuitMovementComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UPursuitMovementComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUPursuitMovementComponent
    {
        FAutoRegisterUPursuitMovementComponent()
        {
            UPursuitMovementComponent::StaticClass();
        }
    };

    static FAutoRegisterUPursuitMovementComponent GAutoRegisterUPursuitMovementComponent;
}

UClass* URotatingMovementComponent::StaticClass()
{
    static UClass Class(
        "URotatingMovementComponent",
        Super::StaticClass(),
        sizeof(URotatingMovementComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<URotatingMovementComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterURotatingMovementComponent
    {
        FAutoRegisterURotatingMovementComponent()
        {
            URotatingMovementComponent::StaticClass();
        }
    };

    static FAutoRegisterURotatingMovementComponent GAutoRegisterURotatingMovementComponent;
}

UClass* UAmbientLightComponent::StaticClass()
{
    static UClass Class(
        "UAmbientLightComponent",
        Super::StaticClass(),
        sizeof(UAmbientLightComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UAmbientLightComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUAmbientLightComponent
    {
        FAutoRegisterUAmbientLightComponent()
        {
            UAmbientLightComponent::StaticClass();
        }
    };

    static FAutoRegisterUAmbientLightComponent GAutoRegisterUAmbientLightComponent;
}

UClass* UDirectionalLightComponent::StaticClass()
{
    static UClass Class(
        "UDirectionalLightComponent",
        Super::StaticClass(),
        sizeof(UDirectionalLightComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UDirectionalLightComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUDirectionalLightComponent
    {
        FAutoRegisterUDirectionalLightComponent()
        {
            UDirectionalLightComponent::StaticClass();
        }
    };

    static FAutoRegisterUDirectionalLightComponent GAutoRegisterUDirectionalLightComponent;
}

UClass* ULightComponent::StaticClass()
{
    static UClass Class(
        "ULightComponent",
        Super::StaticClass(),
        sizeof(ULightComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ULightComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterULightComponent
    {
        FAutoRegisterULightComponent()
        {
            ULightComponent::StaticClass();
        }
    };

    static FAutoRegisterULightComponent GAutoRegisterULightComponent;
}

UClass* ULightComponentBase::StaticClass()
{
    static UClass Class(
        "ULightComponentBase",
        Super::StaticClass(),
        sizeof(ULightComponentBase),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ULightComponentBase>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterULightComponentBase
    {
        FAutoRegisterULightComponentBase()
        {
            ULightComponentBase::StaticClass();
        }
    };

    static FAutoRegisterULightComponentBase GAutoRegisterULightComponentBase;
}

UClass* UPointLightComponent::StaticClass()
{
    static UClass Class(
        "UPointLightComponent",
        Super::StaticClass(),
        sizeof(UPointLightComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UPointLightComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUPointLightComponent
    {
        FAutoRegisterUPointLightComponent()
        {
            UPointLightComponent::StaticClass();
        }
    };

    static FAutoRegisterUPointLightComponent GAutoRegisterUPointLightComponent;
}

UClass* USpotlightComponent::StaticClass()
{
    static UClass Class(
        "USpotlightComponent",
        Super::StaticClass(),
        sizeof(USpotlightComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<USpotlightComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUSpotlightComponent
    {
        FAutoRegisterUSpotlightComponent()
        {
            USpotlightComponent::StaticClass();
        }
    };

    static FAutoRegisterUSpotlightComponent GAutoRegisterUSpotlightComponent;
}

UClass* UPrimitiveComponent::StaticClass()
{
    static UClass Class(
        "UPrimitiveComponent",
        Super::StaticClass(),
        sizeof(UPrimitiveComponent),
        CF_Component,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUPrimitiveComponent
    {
        FAutoRegisterUPrimitiveComponent()
        {
            UPrimitiveComponent::StaticClass();
        }
    };

    static FAutoRegisterUPrimitiveComponent GAutoRegisterUPrimitiveComponent;
}

UClass* UProceduralMeshComponent::StaticClass()
{
    static UClass Class(
        "UProceduralMeshComponent",
        Super::StaticClass(),
        sizeof(UProceduralMeshComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UProceduralMeshComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUProceduralMeshComponent
    {
        FAutoRegisterUProceduralMeshComponent()
        {
            UProceduralMeshComponent::StaticClass();
        }
    };

    static FAutoRegisterUProceduralMeshComponent GAutoRegisterUProceduralMeshComponent;
}

UClass* UShapeComponent::StaticClass()
{
    static UClass Class(
        "UShapeComponent",
        Super::StaticClass(),
        sizeof(UShapeComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UShapeComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUShapeComponent
    {
        FAutoRegisterUShapeComponent()
        {
            UShapeComponent::StaticClass();
        }
    };

    static FAutoRegisterUShapeComponent GAutoRegisterUShapeComponent;
}

UClass* USkeletalMeshComponent::StaticClass()
{
    static UClass Class(
        "USkeletalMeshComponent",
        Super::StaticClass(),
        sizeof(USkeletalMeshComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<USkeletalMeshComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUSkeletalMeshComponent
    {
        FAutoRegisterUSkeletalMeshComponent()
        {
            USkeletalMeshComponent::StaticClass();
        }
    };

    static FAutoRegisterUSkeletalMeshComponent GAutoRegisterUSkeletalMeshComponent;
}

UClass* USkinnedMeshComponent::StaticClass()
{
    static UClass Class(
        "USkinnedMeshComponent",
        Super::StaticClass(),
        sizeof(USkinnedMeshComponent),
        CF_Component,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUSkinnedMeshComponent
    {
        FAutoRegisterUSkinnedMeshComponent()
        {
            USkinnedMeshComponent::StaticClass();
        }
    };

    static FAutoRegisterUSkinnedMeshComponent GAutoRegisterUSkinnedMeshComponent;
}

UClass* USoundComponent::StaticClass()
{
    static UClass Class(
        "USoundComponent",
        Super::StaticClass(),
        sizeof(USoundComponent),
        CF_Component,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUSoundComponent
    {
        FAutoRegisterUSoundComponent()
        {
            USoundComponent::StaticClass();
        }
    };

    static FAutoRegisterUSoundComponent GAutoRegisterUSoundComponent;
}

UClass* USphereComponent::StaticClass()
{
    static UClass Class(
        "USphereComponent",
        Super::StaticClass(),
        sizeof(USphereComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<USphereComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUSphereComponent
    {
        FAutoRegisterUSphereComponent()
        {
            USphereComponent::StaticClass();
        }
    };

    static FAutoRegisterUSphereComponent GAutoRegisterUSphereComponent;
}

UClass* USpringArmComponent::StaticClass()
{
    static UClass Class(
        "USpringArmComponent",
        Super::StaticClass(),
        sizeof(USpringArmComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<USpringArmComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUSpringArmComponent
    {
        FAutoRegisterUSpringArmComponent()
        {
            USpringArmComponent::StaticClass();
        }
    };

    static FAutoRegisterUSpringArmComponent GAutoRegisterUSpringArmComponent;
}

UClass* UStaticMeshComponent::StaticClass()
{
    static UClass Class(
        "UStaticMeshComponent",
        Super::StaticClass(),
        sizeof(UStaticMeshComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UStaticMeshComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUStaticMeshComponent
    {
        FAutoRegisterUStaticMeshComponent()
        {
            UStaticMeshComponent::StaticClass();
        }
    };

    static FAutoRegisterUStaticMeshComponent GAutoRegisterUStaticMeshComponent;
}

UClass* USubUVComponent::StaticClass()
{
    static UClass Class(
        "USubUVComponent",
        Super::StaticClass(),
        sizeof(USubUVComponent),
        CF_Component,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUSubUVComponent
    {
        FAutoRegisterUSubUVComponent()
        {
            USubUVComponent::StaticClass();
        }
    };

    static FAutoRegisterUSubUVComponent GAutoRegisterUSubUVComponent;
}

UClass* UTextRenderComponent::StaticClass()
{
    static UClass Class(
        "UTextRenderComponent",
        Super::StaticClass(),
        sizeof(UTextRenderComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UTextRenderComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUTextRenderComponent
    {
        FAutoRegisterUTextRenderComponent()
        {
            UTextRenderComponent::StaticClass();
        }
    };

    static FAutoRegisterUTextRenderComponent GAutoRegisterUTextRenderComponent;
}

UClass* AAnimLuaTestPawn::StaticClass()
{
    static UClass Class(
        "AAnimLuaTestPawn",
        Super::StaticClass(),
        sizeof(AAnimLuaTestPawn),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<AAnimLuaTestPawn>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAAnimLuaTestPawn
    {
        FAutoRegisterAAnimLuaTestPawn()
        {
            AAnimLuaTestPawn::StaticClass();
        }
    };

    static FAutoRegisterAAnimLuaTestPawn GAutoRegisterAAnimLuaTestPawn;
}

UClass* AAnimTestPawn::StaticClass()
{
    static UClass Class(
        "AAnimTestPawn",
        Super::StaticClass(),
        sizeof(AAnimTestPawn),
        CF_Actor,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAAnimTestPawn
    {
        FAutoRegisterAAnimTestPawn()
        {
            AAnimTestPawn::StaticClass();
        }
    };

    static FAutoRegisterAAnimTestPawn GAutoRegisterAAnimTestPawn;
}

const FTypeInfo AReflectionTestActor::s_TypeInfo = {
    "AReflectionTestActor",
    &AActor::s_TypeInfo,
    sizeof(AReflectionTestActor)
};

UClass* AReflectionTestActor::StaticClass()
{
    static UClass Class(
        "AReflectionTestActor",
        Super::StaticClass(),
        sizeof(AReflectionTestActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<AReflectionTestActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        Class.AddProperty(MakeProperty<FFloatProperty>(
                            "Speed",
                            offsetof(AReflectionTestActor, Speed),
                            sizeof(float),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        Class.AddProperty(MakeProperty<FFloatProperty>(
                            "Health",
                            offsetof(AReflectionTestActor, Health),
                            sizeof(float),
                            EPropertyFlags::Read |
                                EPropertyFlags::Write |
                                EPropertyFlags::Edit |
                                EPropertyFlags::LuaRead |
                                EPropertyFlags::LuaWrite));

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAReflectionTestActor
    {
        FAutoRegisterAReflectionTestActor()
        {
            AReflectionTestActor::StaticClass();
        }
    };

    static FAutoRegisterAReflectionTestActor GAutoRegisterAReflectionTestActor;
}

UClass* ADefaultPawn::StaticClass()
{
    static UClass Class(
        "ADefaultPawn",
        Super::StaticClass(),
        sizeof(ADefaultPawn),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ADefaultPawn>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterADefaultPawn
    {
        FAutoRegisterADefaultPawn()
        {
            ADefaultPawn::StaticClass();
        }
    };

    static FAutoRegisterADefaultPawn GAutoRegisterADefaultPawn;
}

UClass* AGameModeBase::StaticClass()
{
    static UClass Class(
        "AGameModeBase",
        Super::StaticClass(),
        sizeof(AGameModeBase),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<AGameModeBase>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAGameModeBase
    {
        FAutoRegisterAGameModeBase()
        {
            AGameModeBase::StaticClass();
        }
    };

    static FAutoRegisterAGameModeBase GAutoRegisterAGameModeBase;
}

UClass* APawn::StaticClass()
{
    static UClass Class(
        "APawn",
        Super::StaticClass(),
        sizeof(APawn),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<APawn>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAPawn
    {
        FAutoRegisterAPawn()
        {
            APawn::StaticClass();
        }
    };

    static FAutoRegisterAPawn GAutoRegisterAPawn;
}

UClass* APlayerController::StaticClass()
{
    static UClass Class(
        "APlayerController",
        Super::StaticClass(),
        sizeof(APlayerController),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<APlayerController>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAPlayerController
    {
        FAutoRegisterAPlayerController()
        {
            APlayerController::StaticClass();
        }
    };

    static FAutoRegisterAPlayerController GAutoRegisterAPlayerController;
}

UClass* ACubeActor::StaticClass()
{
    static UClass Class(
        "ACubeActor",
        Super::StaticClass(),
        sizeof(ACubeActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ACubeActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterACubeActor
    {
        FAutoRegisterACubeActor()
        {
            ACubeActor::StaticClass();
        }
    };

    static FAutoRegisterACubeActor GAutoRegisterACubeActor;
}

UClass* ASphereActor::StaticClass()
{
    static UClass Class(
        "ASphereActor",
        Super::StaticClass(),
        sizeof(ASphereActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ASphereActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterASphereActor
    {
        FAutoRegisterASphereActor()
        {
            ASphereActor::StaticClass();
        }
    };

    static FAutoRegisterASphereActor GAutoRegisterASphereActor;
}

UClass* APlaneActor::StaticClass()
{
    static UClass Class(
        "APlaneActor",
        Super::StaticClass(),
        sizeof(APlaneActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<APlaneActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAPlaneActor
    {
        FAutoRegisterAPlaneActor()
        {
            APlaneActor::StaticClass();
        }
    };

    static FAutoRegisterAPlaneActor GAutoRegisterAPlaneActor;
}

UClass* AAttachTestActor::StaticClass()
{
    static UClass Class(
        "AAttachTestActor",
        Super::StaticClass(),
        sizeof(AAttachTestActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<AAttachTestActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAAttachTestActor
    {
        FAutoRegisterAAttachTestActor()
        {
            AAttachTestActor::StaticClass();
        }
    };

    static FAutoRegisterAAttachTestActor GAutoRegisterAAttachTestActor;
}

UClass* ASceneActor::StaticClass()
{
    static UClass Class(
        "ASceneActor",
        Super::StaticClass(),
        sizeof(ASceneActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ASceneActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterASceneActor
    {
        FAutoRegisterASceneActor()
        {
            ASceneActor::StaticClass();
        }
    };

    static FAutoRegisterASceneActor GAutoRegisterASceneActor;
}

UClass* APlayerStart::StaticClass()
{
    static UClass Class(
        "APlayerStart",
        Super::StaticClass(),
        sizeof(APlayerStart),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<APlayerStart>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAPlayerStart
    {
        FAutoRegisterAPlayerStart()
        {
            APlayerStart::StaticClass();
        }
    };

    static FAutoRegisterAPlayerStart GAutoRegisterAPlayerStart;
}

UClass* AFogActor::StaticClass()
{
    static UClass Class(
        "AFogActor",
        Super::StaticClass(),
        sizeof(AFogActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<AFogActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAFogActor
    {
        FAutoRegisterAFogActor()
        {
            AFogActor::StaticClass();
        }
    };

    static FAutoRegisterAFogActor GAutoRegisterAFogActor;
}

UClass* AStaticMeshActor::StaticClass()
{
    static UClass Class(
        "AStaticMeshActor",
        Super::StaticClass(),
        sizeof(AStaticMeshActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<AStaticMeshActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAStaticMeshActor
    {
        FAutoRegisterAStaticMeshActor()
        {
            AStaticMeshActor::StaticClass();
        }
    };

    static FAutoRegisterAStaticMeshActor GAutoRegisterAStaticMeshActor;
}

UClass* ASkeletalMeshActor::StaticClass()
{
    static UClass Class(
        "ASkeletalMeshActor",
        Super::StaticClass(),
        sizeof(ASkeletalMeshActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ASkeletalMeshActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterASkeletalMeshActor
    {
        FAutoRegisterASkeletalMeshActor()
        {
            ASkeletalMeshActor::StaticClass();
        }
    };

    static FAutoRegisterASkeletalMeshActor GAutoRegisterASkeletalMeshActor;
}

UClass* ASubUVActor::StaticClass()
{
    static UClass Class(
        "ASubUVActor",
        Super::StaticClass(),
        sizeof(ASubUVActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ASubUVActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterASubUVActor
    {
        FAutoRegisterASubUVActor()
        {
            ASubUVActor::StaticClass();
        }
    };

    static FAutoRegisterASubUVActor GAutoRegisterASubUVActor;
}

UClass* ATextRenderActor::StaticClass()
{
    static UClass Class(
        "ATextRenderActor",
        Super::StaticClass(),
        sizeof(ATextRenderActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ATextRenderActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterATextRenderActor
    {
        FAutoRegisterATextRenderActor()
        {
            ATextRenderActor::StaticClass();
        }
    };

    static FAutoRegisterATextRenderActor GAutoRegisterATextRenderActor;
}

UClass* ABillboardActor::StaticClass()
{
    static UClass Class(
        "ABillboardActor",
        Super::StaticClass(),
        sizeof(ABillboardActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ABillboardActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterABillboardActor
    {
        FAutoRegisterABillboardActor()
        {
            ABillboardActor::StaticClass();
        }
    };

    static FAutoRegisterABillboardActor GAutoRegisterABillboardActor;
}

UClass* ADecalActor::StaticClass()
{
    static UClass Class(
        "ADecalActor",
        Super::StaticClass(),
        sizeof(ADecalActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ADecalActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterADecalActor
    {
        FAutoRegisterADecalActor()
        {
            ADecalActor::StaticClass();
        }
    };

    static FAutoRegisterADecalActor GAutoRegisterADecalActor;
}

UClass* AFireballActor::StaticClass()
{
    static UClass Class(
        "AFireballActor",
        Super::StaticClass(),
        sizeof(AFireballActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<AFireballActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAFireballActor
    {
        FAutoRegisterAFireballActor()
        {
            AFireballActor::StaticClass();
        }
    };

    static FAutoRegisterAFireballActor GAutoRegisterAFireballActor;
}

UClass* ADecalSpotLightActor::StaticClass()
{
    static UClass Class(
        "ADecalSpotLightActor",
        Super::StaticClass(),
        sizeof(ADecalSpotLightActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ADecalSpotLightActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterADecalSpotLightActor
    {
        FAutoRegisterADecalSpotLightActor()
        {
            ADecalSpotLightActor::StaticClass();
        }
    };

    static FAutoRegisterADecalSpotLightActor GAutoRegisterADecalSpotLightActor;
}

UClass* ALightActor::StaticClass()
{
    static UClass Class(
        "ALightActor",
        Super::StaticClass(),
        sizeof(ALightActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ALightActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterALightActor
    {
        FAutoRegisterALightActor()
        {
            ALightActor::StaticClass();
        }
    };

    static FAutoRegisterALightActor GAutoRegisterALightActor;
}

UClass* AAmbientLightActor::StaticClass()
{
    static UClass Class(
        "AAmbientLightActor",
        Super::StaticClass(),
        sizeof(AAmbientLightActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<AAmbientLightActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAAmbientLightActor
    {
        FAutoRegisterAAmbientLightActor()
        {
            AAmbientLightActor::StaticClass();
        }
    };

    static FAutoRegisterAAmbientLightActor GAutoRegisterAAmbientLightActor;
}

UClass* ADirectionalLightActor::StaticClass()
{
    static UClass Class(
        "ADirectionalLightActor",
        Super::StaticClass(),
        sizeof(ADirectionalLightActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ADirectionalLightActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterADirectionalLightActor
    {
        FAutoRegisterADirectionalLightActor()
        {
            ADirectionalLightActor::StaticClass();
        }
    };

    static FAutoRegisterADirectionalLightActor GAutoRegisterADirectionalLightActor;
}

UClass* APointLightActor::StaticClass()
{
    static UClass Class(
        "APointLightActor",
        Super::StaticClass(),
        sizeof(APointLightActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<APointLightActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAPointLightActor
    {
        FAutoRegisterAPointLightActor()
        {
            APointLightActor::StaticClass();
        }
    };

    static FAutoRegisterAPointLightActor GAutoRegisterAPointLightActor;
}

UClass* ASpotlightActor::StaticClass()
{
    static UClass Class(
        "ASpotlightActor",
        Super::StaticClass(),
        sizeof(ASpotlightActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ASpotlightActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterASpotlightActor
    {
        FAutoRegisterASpotlightActor()
        {
            ASpotlightActor::StaticClass();
        }
    };

    static FAutoRegisterASpotlightActor GAutoRegisterASpotlightActor;
}

UClass* ABullet::StaticClass()
{
    static UClass Class(
        "ABullet",
        Super::StaticClass(),
        sizeof(ABullet),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ABullet>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterABullet
    {
        FAutoRegisterABullet()
        {
            ABullet::StaticClass();
        }
    };

    static FAutoRegisterABullet GAutoRegisterABullet;
}

UClass* ABladeSlash::StaticClass()
{
    static UClass Class(
        "ABladeSlash",
        Super::StaticClass(),
        sizeof(ABladeSlash),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ABladeSlash>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterABladeSlash
    {
        FAutoRegisterABladeSlash()
        {
            ABladeSlash::StaticClass();
        }
    };

    static FAutoRegisterABladeSlash GAutoRegisterABladeSlash;
}

UClass* ADestructibleActor::StaticClass()
{
    static UClass Class(
        "ADestructibleActor",
        Super::StaticClass(),
        sizeof(ADestructibleActor),
        CF_Actor,
        nullptr);

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterADestructibleActor
    {
        FAutoRegisterADestructibleActor()
        {
            ADestructibleActor::StaticClass();
        }
    };

    static FAutoRegisterADestructibleActor GAutoRegisterADestructibleActor;
}

UClass* ABoundsBoxActor::StaticClass()
{
    static UClass Class(
        "ABoundsBoxActor",
        Super::StaticClass(),
        sizeof(ABoundsBoxActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<ABoundsBoxActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterABoundsBoxActor
    {
        FAutoRegisterABoundsBoxActor()
        {
            ABoundsBoxActor::StaticClass();
        }
    };

    static FAutoRegisterABoundsBoxActor GAutoRegisterABoundsBoxActor;
}

UClass* UMainSceneDestructibleComponent::StaticClass()
{
    static UClass Class(
        "UMainSceneDestructibleComponent",
        Super::StaticClass(),
        sizeof(UMainSceneDestructibleComponent),
        CF_Component,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<UMainSceneDestructibleComponent>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterUMainSceneDestructibleComponent
    {
        FAutoRegisterUMainSceneDestructibleComponent()
        {
            UMainSceneDestructibleComponent::StaticClass();
        }
    };

    static FAutoRegisterUMainSceneDestructibleComponent GAutoRegisterUMainSceneDestructibleComponent;
}

UClass* AMainSceneDestructibleActor::StaticClass()
{
    static UClass Class(
        "AMainSceneDestructibleActor",
        Super::StaticClass(),
        sizeof(AMainSceneDestructibleActor),
        CF_Actor,
        []() -> UObject*
        {
            return UObjectManager::Get().CreateObject<AMainSceneDestructibleActor>();
        });

    static bool bRegistered = false;
    if (!bRegistered)
    {
        bRegistered = true;

        FReflectionRegistry::Get().RegisterUClass(&Class);
    }

    return &Class;
}

namespace
{
    struct FAutoRegisterAMainSceneDestructibleActor
    {
        FAutoRegisterAMainSceneDestructibleActor()
        {
            AMainSceneDestructibleActor::StaticClass();
        }
    };

    static FAutoRegisterAMainSceneDestructibleActor GAutoRegisterAMainSceneDestructibleActor;
}
