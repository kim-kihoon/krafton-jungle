#include "Component/Physics/RigidBodyComponent.h"

#include "Audio/AudioSystem.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Logger.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Object.h"
#include "Object/ObjectFactory.h"
#include "Physics/JoltPhysicsSystem.h"
#include "Serialization/Archive.h"

#include <algorithm>

namespace
{
constexpr float GravityAcceleration = 9.8f;

bool IsLiveObjectPointer(const UObject* Object)
{
    if (Object == nullptr)
    {
        return false;
    }

    for (const UObject* LiveObject : GUObjectArray)
    {
        if (LiveObject == Object)
        {
            return true;
        }
    }

    return false;
}

} // namespace

DEFINE_CLASS(URigidBodyComponent, UActorComponent)
REGISTER_FACTORY(URigidBodyComponent)

URigidBodyComponent::URigidBodyComponent()
{
    bCanEverTick = true;
}

void URigidBodyComponent::BeginPlay()
{
    UActorComponent::BeginPlay();
    ClampEditableValues();

    if (UpdatedComponent == nullptr && Owner != nullptr)
    {
        UpdatedComponent = Owner->GetRootComponent();
    }
}

void URigidBodyComponent::Serialize(FArchive& Ar)
{
    UActorComponent::Serialize(Ar);
    uint32 UpdatedComponentUUID = UpdatedComponent ? UpdatedComponent->GetUUID() : 0;
    Ar << "UpdatedComponentUUID" << UpdatedComponentUUID;
    Ar << "Velocity" << Velocity;
    Ar << "BodyType" << BodyType;
    Ar << "SimulatePhysics" << bSimulatePhysics;
    Ar << "UseGravity" << bUseGravity;
    Ar << "CanBePickedUp" << bCanBePickedUp;
    Ar << "Mass" << Mass;
    Ar << "GravityScale" << GravityScale;
    Ar << "LinearDamping" << LinearDamping;
    Ar << "MaxSpeed" << MaxSpeed;
    Ar << "SleepSpeed" << SleepSpeed;
    Ar << "AngularVelocity" << AngularVelocity;
    Ar << "AngularDamping" << AngularDamping;
    Ar << "MaxAngularSpeed" << MaxAngularSpeed;
    Ar << "PickupSoundPath" << PickupSoundPath;
    Ar << "DropSoundPath" << DropSoundPath;

    if (Ar.IsLoading())
    {
        ClampEditableValues();
    }
}

void URigidBodyComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    static const char* BodyTypeNames[] = { "Static", "Dynamic", "Kinematic" };

    UActorComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "Updated Component", EPropertyType::SceneComponentRef, &UpdatedComponent });
    OutProps.push_back({ "Velocity", EPropertyType::Vec3, &Velocity });
    OutProps.push_back({ "Body Type", EPropertyType::Enum, &BodyType, 0.0f, 0.0f, 0.0f, BodyTypeNames, 3 });
    OutProps.push_back({ "Simulate Physics", EPropertyType::Bool, &bSimulatePhysics });
    OutProps.push_back({ "Use Gravity", EPropertyType::Bool, &bUseGravity });
    OutProps.push_back({ "Can Be Picked Up", EPropertyType::Bool, &bCanBePickedUp });
    OutProps.push_back({ "Mass", EPropertyType::Float, &Mass, 0.01f, 1000.0f, 0.1f });
    OutProps.push_back({ "Gravity Scale", EPropertyType::Float, &GravityScale, 0.0f, 10.0f, 0.01f });
    OutProps.push_back({ "Linear Damping", EPropertyType::Float, &LinearDamping, 0.0f, 50.0f, 0.01f });
    OutProps.push_back({ "Max Speed", EPropertyType::Float, &MaxSpeed, 0.0f, 1000.0f, 0.1f });
    OutProps.push_back({ "Sleep Speed", EPropertyType::Float, &SleepSpeed, 0.0f, 10.0f, 0.01f });
    OutProps.push_back({ "Angular Damping", EPropertyType::Float, &AngularDamping, 0.0f, 50.0f, 0.01f });
    OutProps.push_back({ "Max Angular Speed", EPropertyType::Float, &MaxAngularSpeed, 0.0f, 720.0f, 1.0f });
    OutProps.push_back({ "Pickup Sound", EPropertyType::String, &PickupSoundPath });
    OutProps.push_back({ "Drop Sound", EPropertyType::String, &DropSoundPath });
}

void URigidBodyComponent::PostEditProperty(const char* PropertyName)
{
    UActorComponent::PostEditProperty(PropertyName);
    ClampEditableValues();
}

USceneComponent* URigidBodyComponent::GetUpdatedComponent() const
{
    if (IsLiveObjectPointer(UpdatedComponent) && Cast<UShapeComponent>(UpdatedComponent) == nullptr)
    {
        return UpdatedComponent;
    }

    if (IsLiveObjectPointer(Owner))
    {
        USceneComponent* Root = Owner->GetRootComponent();
        return IsLiveObjectPointer(Root) ? Root : nullptr;
    }

    return nullptr;
}

void URigidBodyComponent::SetHeldByPhysicsHandle(bool bHeld)
{
    if (!IsDynamicBody())
    {
        return;
    }

    if (bHeldByPhysicsHandle == bHeld)
    {
        return;
    }

    bHeldByPhysicsHandle = bHeld;
    if (bHeld)
    {
        bWasSimulatingBeforeHold = bSimulatePhysics;
        bSimulatePhysics = false;
        bGrounded = false;
        Velocity = FVector::ZeroVector;
        AngularVelocity = FVector::ZeroVector;
        FJoltPhysicsSystem::Get().SetBodyKinematic(this);
        FJoltPhysicsSystem::Get().SetBodyTransformFromComponent(this);
    }
    else
    {
        bSimulatePhysics = bWasSimulatingBeforeHold;
        FJoltPhysicsSystem::Get().SetBodyDynamic(this);
        FJoltPhysicsSystem::Get().SetBodyLinearVelocity(this, Velocity);
    }
}

void URigidBodyComponent::SetVelocity(const FVector& InVelocity)
{
    if (!IsDynamicBody())
    {
        Velocity = FVector::ZeroVector;
        AngularVelocity = FVector::ZeroVector;
        return;
    }

    const FVector PreviousVelocity = Velocity;
    Velocity = InVelocity;
    if (Velocity.Z > 0.0f)
    {
        bGrounded = false;
    }

    if (bDropSoundPending)
    {
        if (Velocity.Z < -0.15f)
        {
            bDropSoundObservedFalling = true;
        }

        if (bDropSoundObservedFalling && PreviousVelocity.Z < -0.15f && Velocity.Z > -0.08f)
        {
            TryPlayPendingDropSound();
        }
    }

    FJoltPhysicsSystem::Get().SetBodyLinearVelocity(this, Velocity);
}

void URigidBodyComponent::AddImpulse(const FVector& Impulse)
{
    ClampEditableValues();
    if (!IsDynamicBody())
    {
        return;
    }

    if (FJoltPhysicsSystem::Get().IsBodyManaged(this))
    {
        FJoltPhysicsSystem::Get().AddBodyImpulse(this, Impulse);
        return;
    }

    Velocity += Impulse / Mass;
    bGrounded = false;
    bGroundPushOutSinceLastTick = false;
}

void URigidBodyComponent::NotifyBlockingPushOut(const FVector& PushDelta)
{
    const FVector PushNormal = PushDelta.GetSafeNormal();
    if (PushNormal.IsNearlyZero())
    {
        return;
    }

    if (PushDelta.Z > 0.0f)
    {
        bGrounded = true;
        bGroundPushOutSinceLastTick = true;
        if (bDropSoundPending)
        {
            TryPlayPendingDropSound();
        }
        if (Velocity.Z < 0.0f)
        {
            Velocity.Z = 0.0f;
        }
    }

    const float IntoSurfaceSpeed = FVector::DotProduct(Velocity, PushNormal);
    if (IntoSurfaceSpeed < 0.0f)
    {
        Velocity -= PushNormal * IntoSurfaceSpeed;
    }
}

FVector URigidBodyComponent::GetPhysicsLocation() const
{
    if (const USceneComponent* Scene = GetUpdatedComponent())
    {
        return Scene->GetWorldLocation();
    }

    return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

void URigidBodyComponent::SetPhysicsLocation(const FVector& NewLocation)
{
    if (USceneComponent* Scene = GetUpdatedComponent())
    {
        Scene->SetWorldLocation(NewLocation);
        FJoltPhysicsSystem::Get().SetBodyTransformFromComponent(this);
        return;
    }

    if (Owner != nullptr)
    {
        Owner->SetActorLocation(NewLocation);
        FJoltPhysicsSystem::Get().SetBodyTransformFromComponent(this);
    }
}

void URigidBodyComponent::SetPhysicsRotation(const FQuat& NewRotation)
{
    if (USceneComponent* Scene = GetUpdatedComponent())
    {
        Scene->SetRelativeRotationQuat(NewRotation);
        FJoltPhysicsSystem::Get().SetBodyTransformFromComponent(this);
        return;
    }

    if (Owner != nullptr)
    {
        Owner->SetActorRotation(NewRotation.Euler());
        FJoltPhysicsSystem::Get().SetBodyTransformFromComponent(this);
    }
}

void URigidBodyComponent::PlayPickupSound() const
{
    if (!PickupSoundPath.empty())
    {
        FAudioSystem::Get().PlayAtLocation(PickupSoundPath, GetPhysicsLocation(), 1.0f, false, 0.5f, 8.0f);
    }
}

void URigidBodyComponent::PlayDropSound() const
{
    if (!DropSoundPath.empty())
    {
        FAudioSystem::Get().PlayAtLocation(DropSoundPath, GetPhysicsLocation(), 1.0f, false, 0.5f, 8.0f);
    }
}

void URigidBodyComponent::QueueDropSound(const FString& SoundPath)
{
    if (SoundPath.empty())
    {
        return;
    }

    PendingDropSoundPath = SoundPath;
    bDropSoundPending = true;
    bDropSoundObservedFalling = Velocity.Z < -0.15f;
}

void URigidBodyComponent::TryPlayPendingDropSound()
{
    if (!bDropSoundPending || PendingDropSoundPath.empty())
    {
        return;
    }

    FAudioSystem::Get().PlayAtLocation(PendingDropSoundPath, GetPhysicsLocation(), 1.0f, false, 0.5f, 8.0f);
    PendingDropSoundPath.clear();
    bDropSoundPending = false;
    bDropSoundObservedFalling = false;
}

void URigidBodyComponent::TickComponent(float DeltaTime)
{
    if (FJoltPhysicsSystem::Get().IsBodyManaged(this))
    {
        return;
    }

    if (DeltaTime <= 0.0f || !bSimulatePhysics || bHeldByPhysicsHandle || !IsDynamicBody())
    {
        return;
    }

    ClampEditableValues();
    ApplyBlockingResponse();

    bGroundPushOutSinceLastTick = false;

    if (bUseGravity && !(bGrounded && Velocity.Z <= 0.0f))
    {
        Velocity += FVector(0.0f, 0.0f, -GravityAcceleration * GravityScale) * DeltaTime;
    }

    if (LinearDamping > 0.0f)
    {
        const float DampingScale = std::max(0.0f, 1.0f - LinearDamping * DeltaTime);
        Velocity *= DampingScale;
    }

    if (MaxSpeed > 0.0f && Velocity.SizeSquared() > MaxSpeed * MaxSpeed)
    {
        Velocity = Velocity.GetSafeNormal() * MaxSpeed;
    }

    if (bGrounded &&
        Velocity.SizeSquared() <= SleepSpeed * SleepSpeed)
    {
        Velocity = FVector::ZeroVector;
        AngularVelocity = FVector::ZeroVector;
        if (bDropSoundPending && bDropSoundObservedFalling)
        {
            TryPlayPendingDropSound();
        }
        return;
    }

    if (USceneComponent* Scene = GetUpdatedComponent())
    {
        FVector FrameMove = Velocity * DeltaTime;
        if (!FrameMove.IsNearlyZero())
        {
            Scene->AddWorldOffset(FrameMove);
        }
    }

    if (Velocity.Z > SleepSpeed)
    {
        bGrounded = false;
    }
}

void URigidBodyComponent::ClampEditableValues()
{
    BodyType = std::clamp(BodyType, static_cast<int32>(EPhysicsBodyType::Static), static_cast<int32>(EPhysicsBodyType::Kinematic));
    Mass = std::max(0.01f, Mass);
    GravityScale = std::max(0.0f, GravityScale);
    LinearDamping = std::max(0.0f, LinearDamping);
    MaxSpeed = std::max(0.0f, MaxSpeed);
    SleepSpeed = std::max(0.0f, SleepSpeed);
    AngularDamping = std::max(0.0f, AngularDamping);
    MaxAngularSpeed = std::max(0.0f, MaxAngularSpeed);

    if (!IsDynamicBody())
    {
        Velocity = FVector::ZeroVector;
        AngularVelocity = FVector::ZeroVector;
        bGrounded = false;
        bGroundPushOutSinceLastTick = false;
    }
}

void URigidBodyComponent::ApplyBlockingResponse()
{
    if (Owner == nullptr)
    {
        return;
    }

    for (UPrimitiveComponent* Primitive : Owner->GetPrimitiveComponents())
    {
        if (Primitive == nullptr)
        {
            continue;
        }

        for (const FBlockingResult& Blocking : Primitive->GetBlockingInfos())
        {
            if (Blocking.OtherComp == nullptr)
            {
                continue;
            }

            FVector Normal = Primitive->GetWorldAABB().GetCenter() - Blocking.OtherComp->GetWorldAABB().GetCenter();
            if (Normal.IsNearlyZero())
            {
                Normal = Blocking.Hit.Normal;
                if (Normal.IsNearlyZero())
                {
                    continue;
                }
            }

            Normal = Normal.GetSafeNormal();
            const float IntoSurfaceSpeed = FVector::DotProduct(Velocity, Normal);
            if (IntoSurfaceSpeed < 0.0f)
            {
                Velocity -= Normal * IntoSurfaceSpeed;
            }

            if (Normal.Z > 0.5f && Velocity.Z < 0.0f)
            {
                Velocity.Z = 0.0f;
            }
        }
    }
}
