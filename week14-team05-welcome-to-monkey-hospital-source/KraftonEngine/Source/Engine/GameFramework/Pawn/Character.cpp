#include "GameFramework/Pawn/Character.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Math/Rotator.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Runtime/Engine.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
	FString ToLowerCopy(const FString& Value)
	{
		FString Result = Value;
		std::transform(
			Result.begin(),
			Result.end(),
			Result.begin(),
			[](unsigned char Character)
			{
				return static_cast<char>(std::tolower(Character));
			});
		return Result;
	}

	int32 FindCharacterRagdollAnchorBoneIndex(const FSkeletalMesh* MeshAsset)
	{
		if (!MeshAsset)
		{
			return -1;
		}

		const char* CandidateTokens[] = { "pelvis", "hips", "hip", "root" };
		for (const char* CandidateToken : CandidateTokens)
		{
			const FString Token = ToLowerCopy(CandidateToken ? FString(CandidateToken) : FString());
			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
			{
				if (ToLowerCopy(MeshAsset->Bones[BoneIndex].Name) == Token)
				{
					return BoneIndex;
				}
			}
		}

		for (const char* CandidateToken : CandidateTokens)
		{
			const FString Token = ToLowerCopy(CandidateToken ? FString(CandidateToken) : FString());
			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
			{
				if (ToLowerCopy(MeshAsset->Bones[BoneIndex].Name).find(Token) != FString::npos)
				{
					return BoneIndex;
				}
			}
		}

		return MeshAsset->Bones.empty() ? -1 : 0;
	}

	int32 FindCharacterChestBoneIndex(const FSkeletalMesh* MeshAsset)
	{
		if (!MeshAsset)
		{
			return -1;
		}

		const char* CandidateTokens[] = { "spine_03", "spine_02", "spine_01", "spine", "chest", "upperchest", "torso" };
		for (const char* CandidateToken : CandidateTokens)
		{
			const FString Token = ToLowerCopy(CandidateToken ? FString(CandidateToken) : FString());
			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
			{
				if (ToLowerCopy(MeshAsset->Bones[BoneIndex].Name) == Token)
				{
					return BoneIndex;
				}
			}
		}

		for (const char* CandidateToken : CandidateTokens)
		{
			const FString Token = ToLowerCopy(CandidateToken ? FString(CandidateToken) : FString());
			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
			{
				if (ToLowerCopy(MeshAsset->Bones[BoneIndex].Name).find(Token) != FString::npos)
				{
					return BoneIndex;
				}
			}
		}

		return -1;
	}

	const char* LexToString(ECharacterPhysicsOwnershipMode Mode)
	{
		switch (Mode)
		{
		case ECharacterPhysicsOwnershipMode::CharacterDriven:
			return "CharacterDriven";
		case ECharacterPhysicsOwnershipMode::TransitionToRagdoll:
			return "TransitionToRagdoll";
		case ECharacterPhysicsOwnershipMode::RagdollDriven:
			return "RagdollDriven";
		case ECharacterPhysicsOwnershipMode::TransitionFromRagdoll:
			return "TransitionFromRagdoll";
		default:
			return "Unknown";
		}
	}

	const char* LexToString(ECharacterPhysicsCollisionMode Mode)
	{
		switch (Mode)
		{
		case ECharacterPhysicsCollisionMode::FullRagdollOwned:
			return "FullRagdollOwned";
		case ECharacterPhysicsCollisionMode::PartialHybrid:
			return "PartialHybrid";
		default:
			return "CharacterDriven";
		}
	}

	const char* LexToString(ECharacterQueryCollisionMode Mode)
	{
		switch (Mode)
		{
		case ECharacterQueryCollisionMode::Disabled:
			return "Disabled";
		case ECharacterQueryCollisionMode::ReservedForFullRagdollProxy:
			return "ReservedForFullRagdollProxy";
		default:
			return "CharacterDriven";
		}
	}

	const char* LexToString(ECharacterGameplayOverlapOwnershipMode Mode)
	{
		switch (Mode)
		{
		case ECharacterGameplayOverlapOwnershipMode::None:
			return "None";
		case ECharacterGameplayOverlapOwnershipMode::FullRagdollQueryProxy:
			return "FullRagdollQueryProxy";
		case ECharacterGameplayOverlapOwnershipMode::PartialHybrid:
			return "PartialHybrid";
		default:
			return "CharacterDriven";
		}
	}

	const char* LexToString(EPhysicsCollisionRole Role)
	{
		switch (Role)
		{
		case EPhysicsCollisionRole::CharacterLocomotionProxy:
			return "CharacterLocomotionProxy";
		case EPhysicsCollisionRole::CharacterQueryProxy:
			return "CharacterQueryProxy";
		case EPhysicsCollisionRole::CharacterMeshPrimitive:
			return "CharacterMeshPrimitive";
		case EPhysicsCollisionRole::PartialReactionBody:
			return "PartialReactionBody";
		case EPhysicsCollisionRole::FullRagdollBody:
			return "FullRagdollBody";
		case EPhysicsCollisionRole::TriggerVolume:
			return "TriggerVolume";
		case EPhysicsCollisionRole::WorldStatic:
			return "WorldStatic";
		case EPhysicsCollisionRole::WorldDynamic:
			return "WorldDynamic";
		default:
			return "None";
		}
	}

	const char* LexToString(ECollisionEnabled Mode)
	{
		switch (Mode)
		{
		case ECollisionEnabled::NoCollision:
			return "NoCollision";
		case ECollisionEnabled::QueryOnly:
			return "QueryOnly";
		case ECollisionEnabled::PhysicsOnly:
			return "PhysicsOnly";
		case ECollisionEnabled::QueryAndPhysics:
			return "QueryAndPhysics";
		default:
			return "Unknown";
		}
	}

	const char* BoolToString(bool bValue)
	{
		return bValue ? "true" : "false";
	}
}
void ACharacter::InitDefaultComponents(const FString& SkeletalMeshFileName)
{
	// 1) Capsule — Root. CharacterMovement 의 UpdatedComponent 가 이걸 가리킴.
	CapsuleComponent = AddComponent<UCapsuleComponent>();
	SetRootComponent(CapsuleComponent);

	// 물리 시뮬레이션(중력/반발)은 CharacterMovement 가 직접 처리하므로 끄되, trigger volume
	// 등과의 overlap 이벤트는 받아야 한다. PhysX 백엔드는 static-static pair 를 생성하지 않아
	// simulate=false 인 static 바디로는 static trigger 와 overlap 이 발화되지 않으므로 kinematic 으로 등록
	// (kinematic↔static pair → setKinematicTarget 으로 위치 추적 + trigger 콜백 수신).
	// ※ overlap 을 받으려면 CollisionEnabled 가 QueryOnly/QueryAndPhysics 여야 물리 씬에 등록됨.
	CapsuleComponent->SetKinematic(true);

	// 2) SkeletalMesh — Capsule 의 자식.
	Mesh = AddComponent<USkeletalMeshComponent>();
	Mesh->AttachToComponent(CapsuleComponent);

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	if (!SkeletalMeshFileName.empty())
	{
		USkeletalMesh* Asset = FMeshManager::LoadSkeletalMesh(SkeletalMeshFileName, Device);
		Mesh->SetSkeletalMesh(Asset);
	}

	// 3) CharacterMovement — non-scene. UpdatedComponent = Capsule.
	CharacterMovement = AddComponent<UCharacterMovementComponent>();
	CharacterMovement->SetUpdatedComponent(CapsuleComponent);
}

void ACharacter::PostDuplicate()
{
	Super::PostDuplicate();
	RefreshCharacterComponentReferences();
}

void ACharacter::OnPostLoad(FArchive& Ar)
{
	Super::OnPostLoad(Ar);
	RefreshCharacterComponentReferences();
}

void ACharacter::RefreshCharacterComponentReferences()
{
	CapsuleComponent  = Cast<UCapsuleComponent>(GetRootComponent());
	Mesh              = GetComponentByClass<USkeletalMeshComponent>();
	CharacterMovement = GetComponentByClass<UCharacterMovementComponent>();

	if (CharacterMovement && !CharacterMovement->HasValidUpdatedComponent())
	{
		if (CapsuleComponent)
		{
			CharacterMovement->SetUpdatedComponent(CapsuleComponent);
		}
		else
		{
			CharacterMovement->ResolveUpdatedComponent();
		}
	}
}

bool ACharacter::EnterRagdoll()
{
	if (!Mesh)
	{
		UE_LOG("Character ragdoll entry rejected: no mesh. Actor=%s", GetName().c_str());
		return false;
	}

	if (PhysicsOwnershipMode == ECharacterPhysicsOwnershipMode::RagdollDriven)
	{
		return Mesh->IsRagdollActive();
	}

	if (PhysicsOwnershipMode == ECharacterPhysicsOwnershipMode::TransitionToRagdoll)
	{
		return true;
	}

	if (PhysicsOwnershipMode == ECharacterPhysicsOwnershipMode::TransitionFromRagdoll)
	{
		UE_LOG("Character ragdoll entry rejected: restore is in progress. Actor=%s", GetName().c_str());
		return false;
	}

	SavePreRagdollCharacterState();
	SuspendCharacterForRagdoll();
	CacheRagdollRestoreLocation();
	PhysicsOwnershipMode = ECharacterPhysicsOwnershipMode::TransitionToRagdoll;
	bPendingRagdollBodyEnable = true;
	bAwaitingRagdollRecoveryRestore = false;
	UE_LOG("Character ragdoll transition queued. Actor=%s Mesh=%s Ownership=%s PhysicsCollision=%s QueryCollision=%s Capsule=%s MeshCollision=%s CapsuleRole=%s MeshRole=%s PartialBodyRole=%s AwaitingRestore=%s QueryProxyActive=%s",
		GetName().c_str(),
		Mesh->GetName().c_str(),
		LexToString(PhysicsOwnershipMode),
		LexToString(CharacterPhysicsCollisionMode),
		LexToString(CharacterQueryCollisionMode),
		LexToString(CapsuleComponent ? CapsuleComponent->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(Mesh ? Mesh->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(GetCapsuleCollisionRole()),
		LexToString(GetMeshCollisionRole()),
		LexToString(GetPartialReactionBodyCollisionRole()),
		BoolToString(bAwaitingRagdollRecoveryRestore),
		BoolToString(IsUsingFullRagdollQueryProxy()));
	return true;
}

void ACharacter::ExitRagdoll()
{
	if (PhysicsOwnershipMode == ECharacterPhysicsOwnershipMode::CharacterDriven)
	{
		return;
	}

	if (PhysicsOwnershipMode == ECharacterPhysicsOwnershipMode::TransitionToRagdoll)
	{
		bPendingRagdollBodyEnable = false;
		RestoreCharacterAfterRagdoll();
		UE_LOG("Character ragdoll transition cancelled before runtime activation. Actor=%s", GetName().c_str());
		return;
	}

	CacheRagdollRestoreLocation();
	PhysicsOwnershipMode = ECharacterPhysicsOwnershipMode::TransitionFromRagdoll;

	if (Mesh)
	{
		Mesh->DisableRagdollPhysics();
	}

	bAwaitingRagdollRecoveryRestore = false;
	RestoreCharacterAfterRagdoll();
	UE_LOG("Character ragdoll exited. Actor=%s Ownership=%s AwaitingRestore=%s QueryProxyActive=%s",
		GetName().c_str(),
		LexToString(PhysicsOwnershipMode),
		BoolToString(bAwaitingRagdollRecoveryRestore),
		BoolToString(IsUsingFullRagdollQueryProxy()));
}

bool ACharacter::BeginRagdollRecovery()
{
	if (PhysicsOwnershipMode != ECharacterPhysicsOwnershipMode::RagdollDriven || !Mesh)
	{
		UE_LOG("Character ragdoll recovery ignored: ragdoll inactive. Actor=%s", GetName().c_str());
		return false;
	}

	const bool bStarted = Mesh->BeginRagdollRecovery();
	if (bStarted)
	{
		PhysicsOwnershipMode = ECharacterPhysicsOwnershipMode::TransitionFromRagdoll;
		bAwaitingRagdollRecoveryRestore = true;
		UE_LOG("Character ragdoll recovery started. Actor=%s Ownership=%s PhysicsCollision=%s QueryCollision=%s Capsule=%s Mesh=%s CapsuleRole=%s MeshRole=%s PartialBodyRole=%s AwaitingRestore=%s QueryProxyActive=%s",
			GetName().c_str(),
			LexToString(PhysicsOwnershipMode),
			LexToString(CharacterPhysicsCollisionMode),
			LexToString(CharacterQueryCollisionMode),
			LexToString(CapsuleComponent ? CapsuleComponent->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
			LexToString(Mesh ? Mesh->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
			LexToString(GetCapsuleCollisionRole()),
			LexToString(GetMeshCollisionRole()),
			LexToString(GetPartialReactionBodyCollisionRole()),
			BoolToString(bAwaitingRagdollRecoveryRestore),
			BoolToString(IsUsingFullRagdollQueryProxy()));
	}
	else
	{
		UE_LOG("Character ragdoll recovery rejected. Actor=%s", GetName().c_str());
	}

	return bStarted;
}

bool ACharacter::IsInRagdoll() const
{
	return PhysicsOwnershipMode != ECharacterPhysicsOwnershipMode::CharacterDriven;
}

bool ACharacter::IsUsingFullRagdollQueryProxy() const
{
	return
		CharacterPhysicsCollisionMode == ECharacterPhysicsCollisionMode::FullRagdollOwned &&
		CharacterQueryCollisionMode == ECharacterQueryCollisionMode::ReservedForFullRagdollProxy &&
		GetCurrentCapsuleCollisionEnabled() == ECollisionEnabled::QueryOnly;
}

EPhysicsCollisionRole ACharacter::GetCapsuleCollisionRole() const
{
	if (!CapsuleComponent)
	{
		return EPhysicsCollisionRole::None;
	}

	return IsUsingFullRagdollQueryProxy()
		? EPhysicsCollisionRole::CharacterQueryProxy
		: EPhysicsCollisionRole::CharacterLocomotionProxy;
}

EPhysicsCollisionRole ACharacter::GetMeshCollisionRole() const
{
	return Mesh ? EPhysicsCollisionRole::CharacterMeshPrimitive : EPhysicsCollisionRole::None;
}

EPhysicsCollisionRole ACharacter::GetPartialReactionBodyCollisionRole() const
{
	return CharacterPhysicsCollisionMode == ECharacterPhysicsCollisionMode::PartialHybrid
		? EPhysicsCollisionRole::PartialReactionBody
		: EPhysicsCollisionRole::None;
}

EPhysicsCollisionRole ACharacter::GetCollisionRoleForComponent(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return EPhysicsCollisionRole::None;
	}

	if (Component == CapsuleComponent.Get())
	{
		return GetCapsuleCollisionRole();
	}

	if (Component == Mesh.Get())
	{
		return GetMeshCollisionRole();
	}

	return EPhysicsCollisionRole::None;
}

bool ACharacter::IsCapsuleGameplayOverlapOwner() const
{
	return
		GameplayOverlapOwnershipMode != ECharacterGameplayOverlapOwnershipMode::None &&
		IsGameplayOverlapOwnerRole(GetCapsuleCollisionRole());
}

bool ACharacter::IsMeshGameplayOverlapOwner() const
{
	return IsGameplayOverlapOwnerRole(GetMeshCollisionRole());
}

bool ACharacter::ArePartialReactionBodiesGameplayOverlapOwners() const
{
	return IsGameplayOverlapOwnerRole(GetPartialReactionBodyCollisionRole());
}

bool ACharacter::IsGameplayOverlapOwnerComponent(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return false;
	}

	if (Component == CapsuleComponent.Get())
	{
		return IsCapsuleGameplayOverlapOwner();
	}

	if (Component == Mesh.Get())
	{
		return IsMeshGameplayOverlapOwner();
	}

	return false;
}

ECollisionEnabled ACharacter::GetCurrentCapsuleCollisionEnabled() const
{
	return CapsuleComponent ? CapsuleComponent->GetCollisionEnabled() : ECollisionEnabled::NoCollision;
}

ECollisionEnabled ACharacter::GetCurrentMeshCollisionEnabled() const
{
	return Mesh ? Mesh->GetCollisionEnabled() : ECollisionEnabled::NoCollision;
}

void ACharacter::AddMovementInput(const FVector& WorldDirection, float ScaleValue)
{
	if (IsInRagdoll())
	{
		return;
	}

	if (!CharacterMovement || !CharacterMovement->HasValidUpdatedComponent())
	{
		RefreshCharacterComponentReferences();
	}

	if (CharacterMovement)
	{
		CharacterMovement->AddInputVector(WorldDirection, ScaleValue);
	}
}

void ACharacter::Jump()
{
	if (IsInRagdoll())
	{
		return;
	}

	if (CharacterMovement)
	{
		CharacterMovement->Jump();
	}
}

void ACharacter::SetMouseSensitivity(float InSensitivity)
{
	MouseSensitivity = std::clamp(InSensitivity, 0.0f, 10.0f);
}

void ACharacter::SetInvertMouseY(bool bInvert)
{
	bInvertMouseY = bInvert;
}

void ACharacter::SavePreRagdollCharacterState()
{
	if (bSavedPreRagdollCharacterState)
	{
		return;
	}

	SavedPreRagdollCollisionOwnership.CapsuleCollisionEnabled = CapsuleComponent
		? CapsuleComponent->GetCollisionEnabled()
		: ECollisionEnabled::NoCollision;
	SavedPreRagdollCollisionOwnership.MeshCollisionEnabled = Mesh
		? Mesh->GetCollisionEnabled()
		: ECollisionEnabled::NoCollision;
	SavedPreRagdollCollisionOwnership.PhysicsCollisionMode = CharacterPhysicsCollisionMode;
	SavedPreRagdollCollisionOwnership.QueryCollisionMode = CharacterQueryCollisionMode;
	bSavedMovementTickEnabled = CharacterMovement
		? CharacterMovement->PrimaryComponentTick.bTickEnabled
		: true;
	bSavedMovementActive = CharacterMovement
		? CharacterMovement->IsActive()
		: true;
	bSavedPreRagdollCharacterState = true;
}

ECollisionEnabled ACharacter::ResolveCharacterDrivenCapsuleCollisionEnabled() const
{
	if (bSavedPreRagdollCharacterState)
	{
		return SavedPreRagdollCollisionOwnership.CapsuleCollisionEnabled;
	}

	return CapsuleComponent
		? CapsuleComponent->GetCollisionEnabled()
		: ECollisionEnabled::NoCollision;
}

ECollisionEnabled ACharacter::ResolveCharacterDrivenMeshCollisionEnabled() const
{
	if (bSavedPreRagdollCharacterState)
	{
		return SavedPreRagdollCollisionOwnership.MeshCollisionEnabled;
	}

	return Mesh
		? Mesh->GetCollisionEnabled()
		: ECollisionEnabled::NoCollision;
}

ECollisionEnabled ACharacter::ResolveCapsuleCollisionEnabledForCurrentOwnership() const
{
	switch (CharacterPhysicsCollisionMode)
	{
	case ECharacterPhysicsCollisionMode::CharacterDriven:
		return ResolveCharacterDrivenCapsuleCollisionEnabled();
	case ECharacterPhysicsCollisionMode::PartialHybrid:
		// Partial ragdoll keeps the normal capsule authority for now. Its dedicated
		// self-interference policy is handled in a later pass.
		return ResolveCharacterDrivenCapsuleCollisionEnabled();
	case ECharacterPhysicsCollisionMode::FullRagdollOwned:
	default:
		// Full ragdoll keeps physical blocking on the independent ragdoll bodies only.
		// The capsule may remain as a coarse query/gameplay proxy without rejoining the
		// physics solve.
		switch (CharacterQueryCollisionMode)
		{
		case ECharacterQueryCollisionMode::ReservedForFullRagdollProxy:
			return ECollisionEnabled::QueryOnly;
		case ECharacterQueryCollisionMode::Disabled:
			return ECollisionEnabled::NoCollision;
		case ECharacterQueryCollisionMode::CharacterDriven:
		default:
			return ECollisionEnabled::NoCollision;
		}
	}
}

ECollisionEnabled ACharacter::ResolveMeshCollisionEnabledForCurrentOwnership() const
{
	switch (CharacterPhysicsCollisionMode)
	{
	case ECharacterPhysicsCollisionMode::CharacterDriven:
		return ResolveCharacterDrivenMeshCollisionEnabled();
	case ECharacterPhysicsCollisionMode::PartialHybrid:
		return ResolveCharacterDrivenMeshCollisionEnabled();
	case ECharacterPhysicsCollisionMode::FullRagdollOwned:
	default:
		switch (CharacterQueryCollisionMode)
		{
		case ECharacterQueryCollisionMode::ReservedForFullRagdollProxy:
			return ECollisionEnabled::NoCollision;
		case ECharacterQueryCollisionMode::Disabled:
			return ECollisionEnabled::NoCollision;
		case ECharacterQueryCollisionMode::CharacterDriven:
		default:
			return ECollisionEnabled::NoCollision;
		}
	}
}

void ACharacter::ReconcileCharacterCollisionOwnership()
{
	if (CapsuleComponent)
	{
		CapsuleComponent->SetCollisionEnabled(ResolveCapsuleCollisionEnabledForCurrentOwnership());
	}

	if (Mesh)
	{
		Mesh->SetCollisionEnabled(ResolveMeshCollisionEnabledForCurrentOwnership());
	}
}

void ACharacter::ApplyCharacterCollisionOwnershipModes(
	ECharacterPhysicsCollisionMode NewPhysicsMode,
	ECharacterQueryCollisionMode NewQueryMode,
	const char* Context)
{
	const bool bChanged =
		CharacterPhysicsCollisionMode != NewPhysicsMode ||
		CharacterQueryCollisionMode != NewQueryMode;
	if (!bChanged)
	{
		return;
	}

	CharacterPhysicsCollisionMode = NewPhysicsMode;
	CharacterQueryCollisionMode = NewQueryMode;
	GameplayOverlapOwnershipMode =
		(NewPhysicsMode == ECharacterPhysicsCollisionMode::FullRagdollOwned &&
			NewQueryMode == ECharacterQueryCollisionMode::ReservedForFullRagdollProxy)
			? ECharacterGameplayOverlapOwnershipMode::FullRagdollQueryProxy
			: (NewPhysicsMode == ECharacterPhysicsCollisionMode::PartialHybrid
				? ECharacterGameplayOverlapOwnershipMode::PartialHybrid
				: ECharacterGameplayOverlapOwnershipMode::CharacterDriven);
	ReconcileCharacterCollisionOwnership();

	const ECollisionEnabled CapsuleCollisionEnabled = CapsuleComponent
		? CapsuleComponent->GetCollisionEnabled()
		: ECollisionEnabled::NoCollision;
	const ECollisionEnabled MeshCollisionEnabled = Mesh
		? Mesh->GetCollisionEnabled()
		: ECollisionEnabled::NoCollision;

	UE_LOG("Character collision ownership updated. Actor=%s Context=%s Physics=%s Query=%s OverlapOwnership=%s Capsule=%s Mesh=%s CapsuleRole=%s MeshRole=%s PartialBodyRole=%s CapsuleOverlapOwner=%s MeshOverlapOwner=%s PartialBodiesOverlapOwner=%s",
		GetName().c_str(),
		Context ? Context : "Unspecified",
		LexToString(CharacterPhysicsCollisionMode),
		LexToString(CharacterQueryCollisionMode),
		LexToString(GameplayOverlapOwnershipMode),
		LexToString(CapsuleCollisionEnabled),
		LexToString(MeshCollisionEnabled),
		LexToString(GetCapsuleCollisionRole()),
		LexToString(GetMeshCollisionRole()),
		LexToString(GetPartialReactionBodyCollisionRole()),
		BoolToString(IsCapsuleGameplayOverlapOwner()),
		BoolToString(IsMeshGameplayOverlapOwner()),
		BoolToString(ArePartialReactionBodiesGameplayOverlapOwners()));
}

void ACharacter::ApplyCharacterPhysicsCollisionMode(ECharacterPhysicsCollisionMode NewMode, const char* Context)
{
	ApplyCharacterCollisionOwnershipModes(NewMode, CharacterQueryCollisionMode, Context);
}

void ACharacter::ApplyCharacterQueryCollisionMode(ECharacterQueryCollisionMode NewMode, const char* Context)
{
	ApplyCharacterCollisionOwnershipModes(CharacterPhysicsCollisionMode, NewMode, Context);
}

void ACharacter::ApplyCharacterDrivenCollisionPolicy()
{
	ApplyCharacterCollisionOwnershipModes(
		ECharacterPhysicsCollisionMode::CharacterDriven,
		ECharacterQueryCollisionMode::CharacterDriven,
		"CharacterDrivenPolicy");
}

void ACharacter::ApplyFullRagdollCollisionPolicy()
{
	// Full ragdoll hands physical blocking ownership to the ragdoll bodies while
	// keeping a dedicated query/gameplay proxy slot for the capsule.
	ApplyCharacterCollisionOwnershipModes(
		ECharacterPhysicsCollisionMode::FullRagdollOwned,
		ECharacterQueryCollisionMode::ReservedForFullRagdollProxy,
		"FullRagdollPolicy");
}

void ACharacter::ApplyPartialRagdollCollisionPolicy()
{
	// Partial ragdoll remains hybrid for now: the character capsule still owns the main
	// character collision responsibilities while local physics chains layer on top.
	ApplyCharacterCollisionOwnershipModes(
		ECharacterPhysicsCollisionMode::PartialHybrid,
		ECharacterQueryCollisionMode::CharacterDriven,
		"PartialRagdollPolicy");
}

void ACharacter::SuspendCharacterForRagdoll()
{
	ApplyFullRagdollCollisionPolicy();
	if (CharacterMovement)
	{
		FVector DiscardedInput;
		CharacterMovement->ConsumeInputVector(DiscardedInput);
		CharacterMovement->SetComponentTickEnabled(false);
		CharacterMovement->Deactivate();
	}

	UE_LOG("Character collision and movement suspended for ragdoll. Actor=%s Ownership=%s PhysicsCollision=%s QueryCollision=%s Capsule=%s Mesh=%s CapsuleRole=%s MeshRole=%s PartialBodyRole=%s AwaitingRestore=%s QueryProxyActive=%s",
		GetName().c_str(),
		LexToString(PhysicsOwnershipMode),
		LexToString(CharacterPhysicsCollisionMode),
		LexToString(CharacterQueryCollisionMode),
		LexToString(CapsuleComponent ? CapsuleComponent->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(Mesh ? Mesh->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(GetCapsuleCollisionRole()),
		LexToString(GetMeshCollisionRole()),
		LexToString(GetPartialReactionBodyCollisionRole()),
		BoolToString(bAwaitingRagdollRecoveryRestore),
		BoolToString(IsUsingFullRagdollQueryProxy()));
}

void ACharacter::RestoreMovementAfterRagdoll()
{
	if (!CharacterMovement)
	{
		return;
	}

	CharacterMovement->SetComponentTickEnabled(bSavedMovementTickEnabled);
	if (bSavedMovementActive)
	{
		CharacterMovement->Activate();
	}
	else
	{
		CharacterMovement->Deactivate();
	}
}

void ACharacter::FinalizePendingRagdollEntry()
{
	if (!bPendingRagdollBodyEnable)
	{
		return;
	}

	bPendingRagdollBodyEnable = false;
	if (!Mesh)
	{
		RestoreCharacterAfterRagdoll();
		UE_LOG("Character ragdoll entry failed: mesh disappeared before activation. Actor=%s", GetName().c_str());
		return;
	}

	if (!Mesh->EnableRagdollPhysics())
	{
		RestoreCharacterAfterRagdoll();
		UE_LOG("Character ragdoll entry failed: mesh ragdoll rejected. Actor=%s Mesh=%s",
			GetName().c_str(),
			Mesh->GetName().c_str());
		return;
	}

	PhysicsOwnershipMode = ECharacterPhysicsOwnershipMode::RagdollDriven;
	UE_LOG("Character ragdoll runtime active. Actor=%s Mesh=%s Ownership=%s PhysicsCollision=%s QueryCollision=%s Capsule=%s MeshCollision=%s CapsuleRole=%s MeshRole=%s PartialBodyRole=%s MeshRagdollActive=%s MeshRecovering=%s QueryProxyActive=%s",
		GetName().c_str(),
		Mesh->GetName().c_str(),
		LexToString(PhysicsOwnershipMode),
		LexToString(CharacterPhysicsCollisionMode),
		LexToString(CharacterQueryCollisionMode),
		LexToString(CapsuleComponent ? CapsuleComponent->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(Mesh ? Mesh->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(GetCapsuleCollisionRole()),
		LexToString(GetMeshCollisionRole()),
		LexToString(GetPartialReactionBodyCollisionRole()),
		BoolToString(Mesh->IsRagdollActive()),
		BoolToString(Mesh->IsRecoveringFromRagdoll()),
		BoolToString(IsUsingFullRagdollQueryProxy()));
}

void ACharacter::CacheRagdollRestoreLocation()
{
	if (!Mesh)
	{
		return;
	}

	FVector RepresentativeLocation = FVector::ZeroVector;
	const bool bHasRepresentativeLocation =
		Mesh->TryGetRagdollRepresentativeLocation(RepresentativeLocation);

	FPhysicsAssetInstance* Instance = Mesh->GetPhysicsAssetInstance();
	USkeletalMesh* SkeletalMesh = Mesh->GetSkeletalMesh();
	FSkeletalMesh* MeshAsset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
	if (!Instance || !MeshAsset)
	{
		if (bHasRepresentativeLocation)
		{
			CachedRagdollRestoreLocation = RepresentativeLocation;
			bHasCachedRagdollRestoreLocation = true;
		}
		return;
	}

	TArray<FTransform> BoneWorldTransforms;
	if (!Instance->PullPhysicsPose(BoneWorldTransforms) || BoneWorldTransforms.empty())
	{
		if (bHasRepresentativeLocation)
		{
			CachedRagdollRestoreLocation = RepresentativeLocation;
			bHasCachedRagdollRestoreLocation = true;
		}
		return;
	}

	bool bUpdatedRestoreLocation = false;
	const int32 AnchorBoneIndex = FindCharacterRagdollAnchorBoneIndex(MeshAsset);
	if (bHasRepresentativeLocation)
	{
		CachedRagdollRestoreLocation = RepresentativeLocation;
		bHasCachedRagdollRestoreLocation = true;
		bUpdatedRestoreLocation = true;
	}
	else if (AnchorBoneIndex >= 0 && AnchorBoneIndex < static_cast<int32>(BoneWorldTransforms.size()))
	{
		CachedRagdollRestoreLocation = BoneWorldTransforms[AnchorBoneIndex].Location;
		bHasCachedRagdollRestoreLocation = true;
		bUpdatedRestoreLocation = true;
	}

	if (!bUpdatedRestoreLocation)
	{
		return;
	}

	const int32 ChestBoneIndex = FindCharacterChestBoneIndex(MeshAsset);
	if (ChestBoneIndex >= 0 && ChestBoneIndex < static_cast<int32>(BoneWorldTransforms.size()))
	{
		FVector FacingVector = BoneWorldTransforms[ChestBoneIndex].Location - CachedRagdollRestoreLocation;
		FacingVector.Z = 0.0f;

		if (FacingVector.Dot(FacingVector) <= 1.0e-4f)
		{
			FacingVector = BoneWorldTransforms[ChestBoneIndex].Rotation.GetForwardVector();
			FacingVector.Z = 0.0f;
		}

		if (FacingVector.Dot(FacingVector) > 1.0e-4f)
		{
			CachedRagdollRestoreYawDegrees = std::atan2(FacingVector.Y, FacingVector.X) * (180.0f / 3.1415926535f);
			bHasCachedRagdollRestoreYaw = true;
		}
	}
}

void ACharacter::RestoreCharacterAfterRagdoll()
{
	if (CapsuleComponent && bHasCachedRagdollRestoreLocation)
	{
		CapsuleComponent->SetWorldLocation(CachedRagdollRestoreLocation);
	}

	if (CapsuleComponent && bHasCachedRagdollRestoreYaw)
	{
		FRotator RestoreRotation = CapsuleComponent->GetWorldRotation();
		RestoreRotation.Yaw = CachedRagdollRestoreYawDegrees;
		CapsuleComponent->SetWorldRotation(RestoreRotation);

		FRotator Control = GetControlRotation();
		Control.Yaw = CachedRagdollRestoreYawDegrees;
		SetControlRotation(Control);
	}

	const ECharacterPhysicsCollisionMode RestoredPhysicsCollisionMode =
		bSavedPreRagdollCharacterState
			? SavedPreRagdollCollisionOwnership.PhysicsCollisionMode
			: ECharacterPhysicsCollisionMode::CharacterDriven;
	const ECharacterQueryCollisionMode RestoredQueryCollisionMode =
		bSavedPreRagdollCharacterState
			? SavedPreRagdollCollisionOwnership.QueryCollisionMode
			: ECharacterQueryCollisionMode::CharacterDriven;
	UE_LOG("Character ragdoll restore ready. Actor=%s Ownership=%s RestoredPhysicsCollision=%s RestoredQueryCollision=%s Capsule=%s Mesh=%s CapsuleRole=%s MeshRole=%s PartialBodyRole=%s AwaitingRestore=%s QueryProxyActive=%s HasRestoreLocation=%s RestoreLocation=(%.2f,%.2f,%.2f)",
		GetName().c_str(),
		LexToString(PhysicsOwnershipMode),
		LexToString(RestoredPhysicsCollisionMode),
		LexToString(RestoredQueryCollisionMode),
		LexToString(CapsuleComponent ? CapsuleComponent->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(Mesh ? Mesh->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(GetCapsuleCollisionRole()),
		LexToString(GetMeshCollisionRole()),
		LexToString(GetPartialReactionBodyCollisionRole()),
		BoolToString(bAwaitingRagdollRecoveryRestore),
		BoolToString(IsUsingFullRagdollQueryProxy()),
		BoolToString(bHasCachedRagdollRestoreLocation),
		CachedRagdollRestoreLocation.X,
		CachedRagdollRestoreLocation.Y,
		CachedRagdollRestoreLocation.Z);
	ApplyCharacterCollisionOwnershipModes(
		RestoredPhysicsCollisionMode,
		RestoredQueryCollisionMode,
		"RestoreCharacterAfterRagdoll");
	RestoreMovementAfterRagdoll();

	PhysicsOwnershipMode = ECharacterPhysicsOwnershipMode::CharacterDriven;
	bPendingRagdollBodyEnable = false;
	bAwaitingRagdollRecoveryRestore = false;
	bSavedPreRagdollCharacterState = false;
	SavedPreRagdollCollisionOwnership = FCharacterCollisionOwnershipSnapshot{};
	bHasCachedRagdollRestoreLocation = false;
	CachedRagdollRestoreLocation = FVector::ZeroVector;
	bHasCachedRagdollRestoreYaw = false;
	CachedRagdollRestoreYawDegrees = 0.0f;
	UE_LOG("Character ragdoll ownership restored. Actor=%s Ownership=%s PhysicsCollision=%s QueryCollision=%s Capsule=%s Mesh=%s CapsuleRole=%s MeshRole=%s PartialBodyRole=%s AwaitingRestore=%s QueryProxyActive=%s",
		GetName().c_str(),
		LexToString(PhysicsOwnershipMode),
		LexToString(CharacterPhysicsCollisionMode),
		LexToString(CharacterQueryCollisionMode),
		LexToString(CapsuleComponent ? CapsuleComponent->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(Mesh ? Mesh->GetCollisionEnabled() : ECollisionEnabled::NoCollision),
		LexToString(GetCapsuleCollisionRole()),
		LexToString(GetMeshCollisionRole()),
		LexToString(GetPartialReactionBodyCollisionRole()),
		BoolToString(bAwaitingRagdollRecoveryRestore),
		BoolToString(IsUsingFullRagdollQueryProxy()));
}

void ACharacter::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent) return;

	if (bAutoInputWASD)
	{
		// Capsule (RootComponent) 기준 — yaw 회전이 곧 캐릭터 facing. mouse look 이 yaw 만
		// 변경 → forward/right vector 가 자동 회전 → WASD 가 "카메라 보는 방향" 으로 이동.
		InputComponent->AddAxisMapping("MoveForward", 'W',  1.0f);
		InputComponent->AddAxisMapping("MoveForward", 'S', -1.0f);
		InputComponent->AddAxisMapping("MoveRight",   'D',  1.0f);
		InputComponent->AddAxisMapping("MoveRight",   'A', -1.0f);

		// WASD 의 forward/right 는 ControlRotation.Yaw 기준 — capsule rotation 과 무관.
		// "카메라가 보는 방향" (yaw 만, pitch 무시) 으로 이동.
		InputComponent->BindAxis("MoveForward", [this](float Value)
		{
			if (Value == 0.0f) return;
			const FRotator YawOnly(0.0f, GetControlRotation().Yaw, 0.0f);
			AddMovementInput(YawOnly.GetForwardVector(), Value);
		});
		InputComponent->BindAxis("MoveRight", [this](float Value)
		{
			if (Value == 0.0f) return;
			const FRotator YawOnly(0.0f, GetControlRotation().Yaw, 0.0f);
			AddMovementInput(YawOnly.GetRightVector(), Value);
		});

	}

	if (bAutoInputJump)
	{
		// Space = Jump. Walking 중에만 effective (CharacterMovement::Jump 가 guard).
		InputComponent->AddActionMapping("Jump", 0x20);
		InputComponent->BindAction("Jump", EInputEvent::Pressed, [this]()
		{
			Jump();
		});
	}

	if (bAutoInputMouseLook)
	{
		InputComponent->AddMouseAxisMapping("Turn", EInputAxisSourceType::MouseX, 1.0f);
		InputComponent->AddMouseAxisMapping("LookUp", EInputAxisSourceType::MouseY, 1.0f);

		InputComponent->BindAxis("Turn", [this](float Value)
		{
			if (Value == 0.0f) return;
			FRotator Rot = GetControlRotation();
			Rot.Yaw += Value * MouseSensitivity;
			SetControlRotation(Rot);
		});

		InputComponent->BindAxis("LookUp", [this](float Value)
		{
			if (Value == 0.0f) return;
			FRotator Rot = GetControlRotation();
			const float PitchScale = bInvertMouseY ? -MouseSensitivity : MouseSensitivity;
			Rot.Pitch += Value * PitchScale;
			Rot.Pitch = std::clamp(Rot.Pitch, MinCameraPitch, MaxCameraPitch);
			SetControlRotation(Rot);
		});
	}
}

void ACharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (PhysicsOwnershipMode == ECharacterPhysicsOwnershipMode::TransitionToRagdoll)
	{
		FinalizePendingRagdollEntry();
	}

	if (IsInRagdoll())
	{
		CacheRagdollRestoreLocation();

		if (bAwaitingRagdollRecoveryRestore &&
			Mesh &&
			!Mesh->IsRagdollActive() &&
			!Mesh->IsRecoveringFromRagdoll())
		{
			RestoreCharacterAfterRagdoll();
		}
	}
	// 같은 frame 안 ControlRotation 변경을 capsule (RootComponent) 에 즉시 반영 — 1 frame 지연 없음.
	// 옵션 충돌 가드:
	//   1) bOrientRotationToMovement = true → yaw 는 Movement::PhysOrientToMovement 가 처리.
	//   2) 직전 frame 에 root motion 이 yaw 를 적용했다 → 이번 frame 도 root motion 이 yaw 를
	//      이어받을 가능성이 큼. Character 가 control yaw 로 덮으면 root motion 회전이 즉시
	//      뒤집혀 토글링 됨 (turn-in-place / strafe anim 의 시각 손상). Movement 측에 양보.
	// 두 경우 모두 pitch/roll 만 apply, yaw 는 movement 에 양보.
	if (CapsuleComponent)
	{
		if (IsInRagdoll())
		{
			return;
		}

		const bool bMovementHandlesYaw = CharacterMovement &&
			(CharacterMovement->bOrientRotationToMovement ||
			 CharacterMovement->HasYawDrivenByRootMotion());

		FRotator R = CapsuleComponent->GetRelativeRotation();
		bool bChanged = false;
		if (bUseControllerRotationYaw && !bMovementHandlesYaw)
		{
			R.Yaw   = ControlRotation.Yaw;
			bChanged = true;
		}
		if (bUseControllerRotationPitch)
		{
			R.Pitch = ControlRotation.Pitch;
			bChanged = true;
		}
		if (bUseControllerRotationRoll)
		{
			R.Roll  = ControlRotation.Roll;
			bChanged = true;
		}
		if (bChanged) CapsuleComponent->SetRelativeRotation(R);
	}
}
