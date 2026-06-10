#include "SkeletalMeshComponent.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"

#include "Animation/AnimationManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Sequence/AnimSequenceBase.h"
#include "Animation/Instance/AnimSingleNodeInstance.h"
#include "Animation/Instance/LuaAnimInstance.h"
#include "Animation/PoseContext.h"
#include "Asset/AssetRegistry.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Object/Object.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Reflection/UClass.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Object/GarbageCollection.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Profiling/Stats/PhysicsStats.h"
#include "Profiling/Stats/Stats.h"
#include "GameFramework/World.h"

#include <PxPhysicsAPI.h>

using namespace physx;

namespace
{
	FBodyInstanceInitParams MakeBodyInstanceInitParams(const USkeletalMeshComponent* Component, bool bEvenIfPendingKill)
	{
		FBodyInstanceInitParams Params;
		if (!Component)
		{
			return Params;
		}

		UWorld* World = bEvenIfPendingKill
			? Component->GetWorldEvenIfPendingKill()
			: Component->GetWorld();
		if (!World)
		{
			return Params;
		}

		if (IPhysicsScene* PhysicsScene = World->GetPhysicsScene())
		{
			Params = PhysicsScene->MakeBodyInstanceInitParams();
		}

		return Params;
	}

	PxVec3 ToPxVec3(const FVector& V)
	{
		return PxVec3(V.X, V.Y, V.Z);
	}

	FVector ToFVector(const PxVec3& V)
	{
		return FVector(V.x, V.y, V.z);
	}

	PxQuat ToPxQuat(const FQuat& Q)
	{
		return PxQuat(Q.X, Q.Y, Q.Z, Q.W);
	}

	FQuat ToFQuat(const PxQuat& Q)
	{
		return FQuat(Q.x, Q.y, Q.z, Q.w);
	}

	PxTransform ToPxTransform(const FTransform& Transform)
	{
		return PxTransform(ToPxVec3(Transform.Location), ToPxQuat(Transform.Rotation));
	}

	constexpr float MatrixDecomposeTolerance = 1.0e-6f;

	FMatrix GetAffineInverseForPoseSync(const FMatrix& Matrix)
	{
		const double A = Matrix.M[0][0];
		const double B = Matrix.M[0][1];
		const double C = Matrix.M[0][2];
		const double D = Matrix.M[1][0];
		const double E = Matrix.M[1][1];
		const double F = Matrix.M[1][2];
		const double G = Matrix.M[2][0];
		const double H = Matrix.M[2][1];
		const double I = Matrix.M[2][2];

		const double Det = A * (E * I - F * H) - B * (D * I - F * G) + C * (D * H - E * G);
		if (std::fabs(Det) < 1.0e-12)
		{
			return Matrix.GetInverse();
		}

		const double InvDet = 1.0 / Det;

		FMatrix Result = FMatrix::Identity;
		Result.M[0][0] = static_cast<float>((E * I - F * H) * InvDet);
		Result.M[0][1] = static_cast<float>((C * H - B * I) * InvDet);
		Result.M[0][2] = static_cast<float>((B * F - C * E) * InvDet);
		Result.M[1][0] = static_cast<float>((F * G - D * I) * InvDet);
		Result.M[1][1] = static_cast<float>((A * I - C * G) * InvDet);
		Result.M[1][2] = static_cast<float>((C * D - A * F) * InvDet);
		Result.M[2][0] = static_cast<float>((D * H - E * G) * InvDet);
		Result.M[2][1] = static_cast<float>((B * G - A * H) * InvDet);
		Result.M[2][2] = static_cast<float>((A * E - B * D) * InvDet);

		const FVector Translation = Matrix.GetLocation();
		Result.M[3][0] = -(Translation.X * Result.M[0][0] + Translation.Y * Result.M[1][0] + Translation.Z * Result.M[2][0]);
		Result.M[3][1] = -(Translation.X * Result.M[0][1] + Translation.Y * Result.M[1][1] + Translation.Z * Result.M[2][1]);
		Result.M[3][2] = -(Translation.X * Result.M[0][2] + Translation.Y * Result.M[1][2] + Translation.Z * Result.M[2][2]);
		return Result;
	}

	FTransform MatrixToPoseSyncTransform(const FMatrix& Matrix)
	{
		FTransform Result;
		Result.Location = Matrix.GetLocation();
		Result.Scale = Matrix.GetScale();

		FMatrix RotationMatrix = Matrix;
		RotationMatrix.M[3][0] = 0.0f;
		RotationMatrix.M[3][1] = 0.0f;
		RotationMatrix.M[3][2] = 0.0f;
		RotationMatrix.M[3][3] = 1.0f;

		if (std::fabs(Result.Scale.X) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[0][0] /= Result.Scale.X;
			RotationMatrix.M[0][1] /= Result.Scale.X;
			RotationMatrix.M[0][2] /= Result.Scale.X;
		}

		if (std::fabs(Result.Scale.Y) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[1][0] /= Result.Scale.Y;
			RotationMatrix.M[1][1] /= Result.Scale.Y;
			RotationMatrix.M[1][2] /= Result.Scale.Y;
		}

		if (std::fabs(Result.Scale.Z) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[2][0] /= Result.Scale.Z;
			RotationMatrix.M[2][1] /= Result.Scale.Z;
			RotationMatrix.M[2][2] /= Result.Scale.Z;
		}

		Result.Rotation = RotationMatrix.ToQuat().GetNormalized();
		return Result;
	}

	int32 FindBoneIndex(const FSkeletalMesh* Asset, FName BoneName)
	{
		if (!Asset)
		{
			return -1;
		}

		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (FName(Asset->Bones[BoneIndex].Name) == BoneName)
			{
				return BoneIndex;
			}
		}

		return -1;
	}

	FConstraintFrame ToConstraintFrame(const FMatrix& Matrix)
	{
		const FTransform Transform(Matrix);

		FConstraintFrame Frame;
		Frame.Position = Transform.Location;
		Frame.Rotation = Transform.Rotation.ToRotator();
		return Frame;
	}

	FConstraintFrame ToConstraintFrame(const PxTransform& Transform)
	{
		FConstraintFrame Frame;
		Frame.Position = ToFVector(Transform.p);
		Frame.Rotation = ToFQuat(Transform.q.getNormalized()).ToRotator();
		return Frame;
	}
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    ClearAnimInstance();
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bStartRagdollOnBeginPlay)
	{
		StartRagdoll();
	}
	else if (ShouldInstantiatePhysicsAsset())
	{
		InstantiatePhysicsAsset();
	}
}

void USkeletalMeshComponent::EndPlay()
{
	TermPhysicsAsset();
	Super::EndPlay();

}

void USkeletalMeshComponent::PostLoad()
{
	Super::PostLoad();
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
    return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
    Super::SetSkeletalMesh(InMesh);
    // Mesh 가 바뀌면 이전 AnimInstance 가 가리키던 본 인덱스/카운트가 무의미해진다.
    // 새 SkeletalMesh 기준으로 AnimInstance 를 재인스턴스화한다.
    InitializeAnimation();
	if (ShouldInstantiatePhysicsAsset())
	{
		InstantiatePhysicsAsset();
	}
	else
	{
		TermPhysicsAsset();
	}
}

void USkeletalMeshComponent::PlayAnimation(UAnimSequenceBase* NewAnimToPlay, bool bLooping)
{
    SetAnimationMode(EAnimationMode::AnimationSingleNode);
    SetAnimation(NewAnimToPlay);
    SetLooping(bLooping);
    SetPlaying(NewAnimToPlay != nullptr);
}

void USkeletalMeshComponent::StopAnimation()
{
    SetAnimation(nullptr);
    SetPlaying(false);

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetCurrentTime(0.0f);
    }
}

// ──────────────────────────────────────────────
// Animation API
// ──────────────────────────────────────────────
void USkeletalMeshComponent::SetAnimationMode(EAnimationMode InMode)
{
    if (AnimationMode == InMode) return;
    AnimationMode = InMode;
    InitializeAnimation();
}

bool USkeletalMeshComponent::CanUseAnimation(UAnimSequenceBase* InAsset) const
{
    if (!InAsset)
    {
        return true;
    }

    const USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
    {
        return false;
    }

    if (const UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        FSkeletonCompatibilityReport Report;
        const bool bCompatible = FAssetRegistry::CheckAnimationForMesh(Sequence, Mesh, &Report);
        if (!bCompatible)
        {
            UE_LOG("SetAnimation rejected: skeleton mismatch. Anim=%s Mesh=%s Reason=%s",
                Sequence->GetName().c_str(),
                Mesh->GetName().c_str(),
                Report.Reason.c_str());
        }
        return bCompatible;
    }

    return true;
}

void USkeletalMeshComponent::SetAnimation(UAnimSequenceBase* InAsset)
{
    if (!CanUseAnimation(InAsset))
    {
        return;
    }

    AnimationData.AnimToPlay = InAsset;

    if (UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        AnimationData.AnimToPlayPath = Sequence->GetAssetPathFileName();
    }
    else if (!InAsset)
    {
        AnimationData.AnimToPlayPath = "None";
    }

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetAnimationAsset(InAsset);
    }
}

void USkeletalMeshComponent::SetPlayRate(float InRate)
{
    AnimationData.PlayRate = InRate;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetPlayRate(InRate);
    }
}

void USkeletalMeshComponent::SetLooping(bool bInLoop)
{
    AnimationData.bLooping = bInLoop;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetLooping(bInLoop);
    }
}

void USkeletalMeshComponent::SetPlaying(bool bInPlay)
{
    AnimationData.bPlaying = bInPlay;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetPlaying(bInPlay);
    }
}

void USkeletalMeshComponent::SetAnimInstanceClass(UClass* InClass)
{
    if (AnimInstanceClass.Get() == InClass) return;
    AnimInstanceClass = InClass;   // TSubclassOf 가 IsA 가드로 검증 (잘못된 클래스 → nullptr).
    if (AnimationMode == EAnimationMode::AnimationCustom)
    {
        InitializeAnimation();
    }
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
    if (AnimInstance == InInstance) return;
    ClearAnimInstance();
    AnimInstance = InInstance;
    if (AnimInstance)
    {
        AnimInstance->SetOuter(this);
        AnimInstance->SetOwningComponent(this);
        ApplyPersistentAnimInstanceSettings(AnimInstance);
        AnimInstance->NativeInitializeAnimation();
    }
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetAnimNodeInstance(FName NodeName) const
{
    (void)NodeName;
    return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::LoadAnimationFromPath()
{
    AnimationData.AnimToPlay = nullptr;

    if (AnimationData.AnimToPlayPath.empty() || AnimationData.AnimToPlayPath == "None")
    {
        return;
    }

    UAnimSequence* LoadedAnimation = FAnimationManager::Get().LoadAnimation(AnimationData.AnimToPlayPath.ToString());
    if (LoadedAnimation && CanUseAnimation(LoadedAnimation))
    {
        AnimationData.AnimToPlay = LoadedAnimation;
    }
    else
    {
        AnimationData.AnimToPlay = nullptr;
    }
}

void USkeletalMeshComponent::CapturePersistentAnimInstanceSettings()
{
    if (ULuaAnimInstance* LuaAnim = Cast<ULuaAnimInstance>(AnimInstance))
    {
        if (!LuaAnim->ScriptFile.empty() && LuaAnim->ScriptFile != "None")
        {
            LuaAnimScriptFile = LuaAnim->ScriptFile;
        }
    }
}

void USkeletalMeshComponent::ApplyPersistentAnimInstanceSettings(UAnimInstance* Instance)
{
    ULuaAnimInstance* LuaAnim = Cast<ULuaAnimInstance>(Instance);
    if (!LuaAnim)
    {
        return;
    }

    if (!LuaAnimScriptFile.empty() && LuaAnimScriptFile != "None")
    {
        LuaAnim->ScriptFile = LuaAnimScriptFile;
    }
    else if (!LuaAnim->ScriptFile.empty() && LuaAnim->ScriptFile != "None")
    {
        LuaAnimScriptFile = LuaAnim->ScriptFile;
    }
}

void USkeletalMeshComponent::InitializeAnimation()
{
    if (!GetSkeletalMesh())
    {
        ClearAnimInstance();
        return;
    }
    if (AnimationMode == EAnimationMode::None)
    {
        ClearAnimInstance();
        return;
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode &&
        !AnimationData.AnimToPlay &&
        !AnimationData.AnimToPlayPath.empty() &&
        AnimationData.AnimToPlayPath != "None")
    {
        LoadAnimationFromPath();
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode && !CanUseAnimation(AnimationData.AnimToPlay))
    {
        AnimationData.AnimToPlay = nullptr;
        AnimationData.AnimToPlayPath = "None";
    }

    switch (AnimationMode)
    {
    case EAnimationMode::AnimationSingleNode:
    {
        ClearAnimInstance();

        UAnimSingleNodeInstance* Single =
            UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        AnimInstance = Single;
        Single->SetOwningComponent(this);
        Single->SetAnimationAsset(AnimationData.AnimToPlay);
        Single->SetPlayRate(AnimationData.PlayRate);
        Single->SetLooping(AnimationData.bLooping);
        Single->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        Single->NativeInitializeAnimation();
        break;
    }
    case EAnimationMode::AnimationCustom:
    {
        UClass* DesiredClass = AnimInstanceClass.Get();
        if (!DesiredClass)
        {
            ClearAnimInstance();
            return;
        }

        if (AnimInstance && AnimInstance->GetClass() == DesiredClass)
        {
            AnimInstance->SetOuter(this);
            AnimInstance->SetOwningComponent(this);
            ApplyPersistentAnimInstanceSettings(AnimInstance);
            AnimInstance->NativeInitializeAnimation();
            break;
        }

        ClearAnimInstance();

        UObject* Obj = FObjectFactory::Get().Create(DesiredClass->GetName(), this);
        AnimInstance = Cast<UAnimInstance>(Obj);
		if (!AnimInstance)
        {
            // 클래스가 등록 안됐거나 캐스트 실패 — 무관한 객체가 생성됐을 수 있으니 정리.
            if (Obj) UObjectManager::Get().DestroyObject(Obj);
            return;
        }
        AnimInstance->SetOwningComponent(this);
        ApplyPersistentAnimInstanceSettings(AnimInstance);

        AnimInstance->NativeInitializeAnimation();
        break;
    }
    default:
        break;
    }
}

void USkeletalMeshComponent::ClearAnimInstance()
{
    if (AnimInstance)
    {
        CapturePersistentAnimInstanceSettings();
        if (ULuaAnimInstance* LuaAnim = Cast<ULuaAnimInstance>(AnimInstance))
        {
            LuaAnim->ReleaseLuaRuntimeForShutdown();
        }
        UObjectManager::Get().DestroyObject(AnimInstance);
        AnimInstance = nullptr;
    }
}

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	if (bRagdollActive)
	{
		uint32 ValidBodyCount = 0;
		for (const FBodyInstance* Body : Bodies)
		{
			if (Body && Body->IsValidBodyInstance())
			{
				++ValidBodyCount;
			}
		}

		uint32 ValidConstraintCount = 0;
		for (const FConstraintInstance* Constraint : Constraints)
		{
			if (Constraint)
			{
				++ValidConstraintCount;
			}
		}

		PHYSICS_STATS_ADD_RAGDOLL_COMPONENT(
			ValidBodyCount,
			static_cast<uint32>(Bodies.size()) - ValidBodyCount,
			ValidConstraintCount,
			static_cast<uint32>(Constraints.size()) - ValidConstraintCount);

		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
		SyncSkeletonPoseFromBodies();
		return;
	}

    if (EvaluateAnimInstance(DeltaTime))
    {
		SyncBodiesFromAnimationPose();
        UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
        return;
    }

	//SyncBodiesFromAnimationPose();
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

// ──────────────────────────────────────────────
// Editor / 직렬화 통합
// ──────────────────────────────────────────────
void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyValue>& OutProps)
{
    Super::GetEditableProperties(OutProps);

    // AnimInstance 자체 properties (Speed 등) 도 패널에 같이 노출 — 컴포넌트가 forward.
    // 자식이 자기 카테고리(예: "Animation|Character") 로 그룹화.
    if (AnimInstance) AnimInstance->GetEditableProperties(OutProps);
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    Super::PostEditProperty(PropertyName);
    if (!PropertyName) return;

    if (std::strcmp(PropertyName, "AnimationMode") == 0)
    {
        InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimInstanceClass") == 0)
    {
        // 클래스 슬롯이 바뀌면 Custom 모드에서 인스턴스 재생성 필요. (ours — Phase 6)
        if (AnimationMode == EAnimationMode::AnimationCustom) InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimationData") == 0)
    {
        LoadAnimationFromPath();

        if (AnimInstance)
        {
            InitializeAnimation();
        }
    }
    else if (std::strcmp(PropertyName, "AnimToPlayPath") == 0)
    {
        // theirs (main): FAnimationManager 가 path 로 실제 UAnimSequence 로딩 — Phase 4 의 TODO 해소.
        // Mode 가 None 이면 SingleNode 로 자동 전환, AnimInstance 없으면 Initialize, 있으면 SingleNode setter 들 갱신.
        LoadAnimationFromPath();

        if (AnimationMode == EAnimationMode::None)
        {
            AnimationMode = EAnimationMode::AnimationSingleNode;
        }

        if (!AnimInstance)
        {
            InitializeAnimation();
        }
        else if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
        {
            if (!CanUseAnimation(AnimationData.AnimToPlay))
            {
                AnimationData.AnimToPlay = nullptr;
                AnimationData.AnimToPlayPath = "None";
            }
            SingleNode->SetAnimationAsset(AnimationData.AnimToPlay);
            SingleNode->SetPlayRate(AnimationData.PlayRate);
            SingleNode->SetLooping(AnimationData.bLooping);
            SingleNode->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        }
    }
    else if (std::strcmp(PropertyName, "PlayRate") == 0)
    {
        SetPlayRate(AnimationData.PlayRate);
    }
    else if (std::strcmp(PropertyName, "bLooping") == 0)
    {
        SetLooping(AnimationData.bLooping);
    }
    else if (std::strcmp(PropertyName, "bPlaying") == 0)
    {
        SetPlaying(AnimationData.bPlaying);
    }
    else if (std::strcmp(PropertyName, "LuaAnimScriptFile") == 0)
    {
        if (ULuaAnimInstance* LuaAnim = Cast<ULuaAnimInstance>(AnimInstance))
        {
            LuaAnim->ScriptFile = LuaAnimScriptFile;
            LuaAnim->ReloadScript();
        }
    }
	else if (std::strcmp(PropertyName, "bRagdollActive") == 0)
	{
		// Start or End Ragdoll 가 정상적으로 반영되지 않은 경우 변경 이전 값을 쓰도록 함
		const bool bRequested = bRagdollActive;
		if (bRequested)
		{
			bRagdollActive = false;
			StartRagdoll();
		}
		else
		{
			bRagdollActive = true;
			EndRagdoll();
		}
	}
	else if (std::strcmp(PropertyName, "bSimulatePhysics") == 0)
	{
		if (ShouldInstantiatePhysicsAsset())
		{
			InstantiatePhysicsAsset();
		}
		else
		{
			TermPhysicsAsset();
		}
	}

    // AnimInstance 자체 properties 는 자식이 자체 PostEdit 처리. 컴포넌트는 dispatch 만.
    // 컴포넌트가 인식한 이름과 겹치지 않는 한 무해 (자식이 모르는 이름은 no-op).
    if (AnimInstance)
    {
        AnimInstance->PostEditProperty(PropertyName);
        CapturePersistentAnimInstanceSettings();
    }
}

void USkeletalMeshComponent::PostDuplicate()
{
	bComponentHasBegunPlay = false;

    Super::PostDuplicate();

    // USkinnedMeshComponent::PostDuplicate() 의 SetSkeletalMesh() 경로가 이미 virtual override 를 통해
    // InitializeAnimation() 을 호출할 수 있다. 없을 때만 보강해서 PIE duplicate 의 double init 을 피한다.
    if (!AnimInstance)
    {
        InitializeAnimation();
    }
    else
    {
        ApplyPersistentAnimInstanceSettings(AnimInstance);
    }
}

void USkeletalMeshComponent::PreSave()
{
    Super::PreSave();
    CapturePersistentAnimInstanceSettings();
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    if (Ar.IsSaving())
    {
        CapturePersistentAnimInstanceSettings();
    }

    Super::Serialize(Ar);

    uint8 ModeRaw = static_cast<uint8>(AnimationMode);
    Ar << ModeRaw;
    AnimationMode = static_cast<EAnimationMode>(ModeRaw);

    // AnimInstance 는 Transient 이라 Duplicate/PIE 복사에서 사라진다. Lua script path 는 컴포넌트가 별도 보관한다.
    Ar << LuaAnimScriptFile;

    // AnimToPlay 의 path 만 라운드트립. 실제 포인터 복원은 InitializeAnimation() → LoadAnimationFromPath() 가 처리.
    FString AnimToPlayPath = Ar.IsSaving() ? AnimationData.AnimToPlayPath.ToString() : FString();
    Ar << AnimToPlayPath;
    if (Ar.IsLoading())
    {
        AnimationData.AnimToPlayPath.SetPath(AnimToPlayPath);
    }
    Ar << AnimationData.PlayRate;
    Ar << AnimationData.bLooping;
    Ar << AnimationData.bPlaying;

	FString OverridePath = Ar.IsSaving() ? PhysicsAssetOverridePath : FString();
	if (Ar.IsSaving() && PhysicsAssetOverride && !PhysicsAssetOverride->GetSourcePath().empty())
	{
		OverridePath = PhysicsAssetOverride->GetSourcePath();
	}
	if (Ar.IsSaving() || Ar.HasRemaining())
	{
		Ar << OverridePath;
	}
	if (Ar.IsLoading())
	{
		PhysicsAssetOverridePath = OverridePath;
		if (!PhysicsAssetOverridePath.empty() && PhysicsAssetOverridePath != "None")
		{
			PhysicsAssetOverride = FPhysicsAssetManager::Get().Load(PhysicsAssetOverridePath);
		}
	}
}

UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const
{
	if (PhysicsAssetOverride)
	{
		return PhysicsAssetOverride.Get();
	}

	USkeletalMesh* SkelMesh = GetSkeletalMesh();
	if (!SkelMesh)
	{
		return nullptr;
	}

	return SkelMesh->GetPhysicsAsset();
}

void USkeletalMeshComponent::SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset)
{
	if (PhysicsAssetOverride.Get() == InPhysicsAsset)
	{
		return;
	}

	TermPhysicsAsset();
	PhysicsAssetOverride = InPhysicsAsset;
	PhysicsAssetOverridePath = (InPhysicsAsset && !InPhysicsAsset->GetSourcePath().empty())
		? InPhysicsAsset->GetSourcePath()
		: FString("None");

	if (ShouldInstantiatePhysicsAsset())
	{
		InstantiatePhysicsAsset();
	}
}

bool USkeletalMeshComponent::ShouldInstantiatePhysicsAsset() const
{
	return bComponentHasBegunPlay && GetPhysicsAsset() && GetWorld() && GetWorld()->GetPhysicsScene() && (GetSimulatePhysics() || bRagdollActive);
}

void USkeletalMeshComponent::InstantiatePhysicsAsset()
{
	TermPhysicsAsset();

	if (!ShouldInstantiatePhysicsAsset())
	{
		return;
	}

	PHYSICS_STATS_ADD_RAGDOLL_INSTANTIATE();

	CreateBodiesFromPhysicsAsset();

	SyncBodiesFromAnimationPose();

	CreateConstraintInstancesFromPhysicsAsset();

	UpdateConstraintFrames();

	InitConstraints();
}

void USkeletalMeshComponent::CreateBodiesFromPhysicsAsset()
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return;
	}

	PhysicsAsset->UpdateBodySetupIndexMap();

	const FBodyInstanceInitParams InitParams = MakeBodyInstanceInitParams(this, false);
	FInitBodySpawnParams SpawnParams(this);
	SpawnParams.bStaticPhysics = !GetSimulatePhysics() && !bRagdollActive;
	SpawnParams.bPhysicsTypeDeterminesSimulation = true;

	// 1. PhysicsAsset의 BodySetup을 컴포넌트 소유 런타임 BodyInstance로 복제한다.
	Bodies.resize(PhysicsAsset->GetBodySetupCount(), nullptr);
	for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->GetBodySetupCount(); ++BodyIndex)
	{
		USkeletalBodySetup* BodySetup = PhysicsAsset->GetBodySetup(BodyIndex);
		if (!BodySetup)
		{
			continue;
		}

		FBodyInstance* BodyInstance = new FBodyInstance();
		BodyInstance->InitBody(BodySetup, this, InitParams, SpawnParams);
		Bodies[BodyIndex] = BodyInstance;
	}
}

void USkeletalMeshComponent::CreateConstraintInstancesFromPhysicsAsset()
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return;
	}

	// 2. PhysicsAsset의 ConstraintTemplate을 BodyInstance끼리 연결하는 런타임 ConstraintInstance로 복제한다.
	Constraints.resize(PhysicsAsset->GetConstraintSetupCount(), nullptr);
	for (int32 ConstraintIndex = 0; ConstraintIndex < PhysicsAsset->GetConstraintSetupCount(); ++ConstraintIndex)
	{
		UPhysicsConstraintTemplate* Template = PhysicsAsset->GetConstraintSetup(ConstraintIndex);
		if (!Template)
		{
			continue;
		}

		FConstraintInstance DefaultInstance = Template->GetDefaultInstance();
		if (DefaultInstance.ConstraintName.IsNone())
		{
			DefaultInstance.ConstraintName = Template->GetConstraintName();
		}
		if (DefaultInstance.ParentBoneName.IsNone())
		{
			DefaultInstance.ParentBoneName = Template->GetParentBoneName();
		}
		if (DefaultInstance.ChildBoneName.IsNone())
		{
			DefaultInstance.ChildBoneName = Template->GetChildBoneName();
		}

		const int32 ParentBodyIndex = PhysicsAsset->FindBodyIndex(DefaultInstance.ParentBoneName);
		const int32 ChildBodyIndex = PhysicsAsset->FindBodyIndex(DefaultInstance.ChildBoneName);
		if (ParentBodyIndex < 0 || ChildBodyIndex < 0)
		{
			continue;
		}

		FBodyInstance* ParentBody = GetBodyInstance(ParentBodyIndex);
		FBodyInstance* ChildBody = GetBodyInstance(ChildBodyIndex);
		if (!ParentBody || !ChildBody)
		{
			continue;
		}

		FConstraintInstance* RuntimeConstraint = new FConstraintInstance();
		*RuntimeConstraint = DefaultInstance;
		RuntimeConstraint->ConstraintIndex = ConstraintIndex;
		Constraints[ConstraintIndex] = RuntimeConstraint;
	}
}

void USkeletalMeshComponent::UpdateConstraintFrames()
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return;
	}

	for (FConstraintInstance* Constraint : Constraints)
	{
		if (!Constraint)
		{
			continue;
		}

		const int32 ParentBodyIndex = PhysicsAsset->FindBodyIndex(Constraint->ParentBoneName);
		const int32 ChildBodyIndex = PhysicsAsset->FindBodyIndex(Constraint->ChildBoneName);
		if (ParentBodyIndex < 0 || ChildBodyIndex < 0)
		{
			continue;
		}

		FBodyInstance* ParentBody = GetBodyInstance(ParentBodyIndex);
		FBodyInstance* ChildBody = GetBodyInstance(ChildBodyIndex);
		if (!ParentBody || !ChildBody || !ParentBody->Actor || !ChildBody->Actor)
		{
			continue;
		}

		const PxTransform ParentWorld = ParentBody->Actor->getGlobalPose();
		const PxTransform ChildWorld = ChildBody->Actor->getGlobalPose();
		if (!ParentWorld.isValid() || !ChildWorld.isValid())
		{
			continue;
		}

		// PxD6JointCreate()의 localFrame은 각 actor local 기준이다.
		// 따라서 엔진 FMatrix 조합 대신 실제 PhysX actor pose로부터
		// actor^-1 * jointWorld 형태로 계산해야 locked linear가 시작 pose에서 만족된다.
		const PxTransform JointWorld = ChildWorld;
		Constraint->ParentFrame = ToConstraintFrame(ParentWorld.getInverse() * JointWorld);
		Constraint->ChildFrame = ToConstraintFrame(ChildWorld.getInverse() * JointWorld);
	}
}

void USkeletalMeshComponent::InitConstraints()
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return;
	}

	const FBodyInstanceInitParams InitParams = MakeBodyInstanceInitParams(this, false);
	for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(Constraints.size()); ++ConstraintIndex)
	{
		FConstraintInstance* Constraint = Constraints[ConstraintIndex];
		if (!Constraint)
		{
			continue;
		}

		const int32 ParentBodyIndex = PhysicsAsset->FindBodyIndex(Constraint->ParentBoneName);
		const int32 ChildBodyIndex = PhysicsAsset->FindBodyIndex(Constraint->ChildBoneName);
		if (ParentBodyIndex < 0 || ChildBodyIndex < 0)
		{
			continue;
		}

		FBodyInstance* ParentBody = GetBodyInstance(ParentBodyIndex);
		FBodyInstance* ChildBody = GetBodyInstance(ChildBodyIndex);
		if (!ParentBody || !ChildBody)
		{
			continue;
		}

		Constraint->InitConstraint(ParentBody, ChildBody, InitParams);
	}
}

void USkeletalMeshComponent::TermPhysicsAsset()
{
	if (!Bodies.empty() || !Constraints.empty())
	{
		PHYSICS_STATS_ADD_RAGDOLL_TERM();
	}

	const FBodyInstanceInitParams InitParams = MakeBodyInstanceInitParams(this, true);

	for (FConstraintInstance* Constraint : Constraints)
	{
		if (Constraint)
		{
			Constraint->TermConstraint();
			delete Constraint;
		}
	}
	Constraints.clear();

	for (FBodyInstance* Body : Bodies)
	{
		if (Body)
		{
			Body->TermBody(InitParams);
			delete Body;
		}
	}
	Bodies.clear();
}

void USkeletalMeshComponent::StartRagdoll()
{
	if (bRagdollActive) return;
	if (!GetSkeletalMesh() || !GetPhysicsAsset()) return;

	bSimulatePhysicsBeforeRagdoll = bSimulatePhysics;
	bRagdollActive = true;
	bSimulatePhysics = true;

	EnsureBoneEditPose();
	InstantiatePhysicsAsset();
}

void USkeletalMeshComponent::EndRagdoll()
{
	if (!bRagdollActive) return;

	SyncSkeletonPoseFromBodies();
	bRagdollActive = false;
	bSimulatePhysics = bSimulatePhysicsBeforeRagdoll;
	InstantiatePhysicsAsset();
}

void USkeletalMeshComponent::SyncBodiesFromAnimationPose()
{
	SCOPE_STAT_CAT("Ragdoll_SyncBodiesFromAnimation", "Physics");

	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	USkeletalMesh* SkelMesh = GetSkeletalMesh();
	FSkeletalMesh* Asset = SkelMesh ? SkelMesh->GetSkeletalMeshAsset() : nullptr;
	if (!PhysicsAsset || !Asset)
	{
		return;
	}

	TArray<FMatrix> BoneGlobalMatrices;
	GetCurrentBoneGlobalMatrices(BoneGlobalMatrices);

	for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
	{
		FBodyInstance* Body = Bodies[BodyIndex];
		USkeletalBodySetup* BodySetup = PhysicsAsset->GetBodySetup(BodyIndex);
		if (!Body || !Body->Actor || !BodySetup)
		{
			continue;
		}

		const FName BoneName = BodySetup->GetBoneName();
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (FName(Asset->Bones[BoneIndex].Name) != BoneName)
			{
				continue;
			}

			if (BoneIndex < static_cast<int32>(BoneGlobalMatrices.size()))
			{
				// Rotation 에서 Scale 분리 필수
				const FTransform BodyTransform = MatrixToPoseSyncTransform(BoneGlobalMatrices[BoneIndex] * GetWorldMatrix());
				Body->Actor->setGlobalPose(ToPxTransform(BodyTransform));
			}
			break;
		}
	}
}

void USkeletalMeshComponent::SyncSkeletonPoseFromBodies()
{
	SCOPE_STAT_CAT("Ragdoll_SyncSkeletonFromBodies", "Physics");

	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	USkeletalMesh* SkelMesh = GetSkeletalMesh();
	FSkeletalMesh* Asset = SkelMesh ? SkelMesh->GetSkeletalMeshAsset() : nullptr;
	if (!PhysicsAsset || !Asset)
	{
		return;
	}

	EnsureBoneEditPose();

	const int32 BoneCount = static_cast<int32>(Asset->Bones.size());
	if (BoneCount <= 0 || BoneEditLocalMatrices.size() != Asset->Bones.size())
	{
		return;
	}

	TArray<FMatrix> BodyTargetGlobals;
	TArray<uint8> bHasBodyTarget;
	BodyTargetGlobals.resize(BoneCount, FMatrix::Identity);
	bHasBodyTarget.resize(BoneCount, 0);

	TArray<FMatrix> CurrentGlobals;
	GetCurrentBoneGlobalMatrices(CurrentGlobals);
	if (CurrentGlobals.size() != Asset->Bones.size())
	{
		return;
	}

	const FMatrix ComponentWorldInv = GetAffineInverseForPoseSync(GetWorldMatrix());
	bool bPoseChanged = false;
	for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
	{
		FBodyInstance* Body = Bodies[BodyIndex];
		USkeletalBodySetup* BodySetup = PhysicsAsset->GetBodySetup(BodyIndex);
		if (!Body || !Body->Actor || !BodySetup)
		{
			continue;
		}

		const int32 BoneIndex = FindBoneIndex(Asset, BodySetup->GetBoneName());
		if (BoneIndex < 0 || BoneIndex >= BoneCount)
		{
			continue;
		}

		const PxTransform BodyPose = Body->Actor->getGlobalPose();
		FTransform BodyWorldTransform;
		BodyWorldTransform.Location = ToFVector(BodyPose.p);
		BodyWorldTransform.Rotation = ToFQuat(BodyPose.q).GetNormalized();
		BodyWorldTransform.Scale = FVector(1.0f, 1.0f, 1.0f);

		FTransform BodyComponentTransform = MatrixToPoseSyncTransform(BodyWorldTransform.ToMatrix() * ComponentWorldInv);

		// PhysX rigid actor에는 scale이 없으므로 현재 skeleton global scale을 유지한다.
		BodyComponentTransform.Scale = MatrixToPoseSyncTransform(CurrentGlobals[BoneIndex]).Scale;

		BodyTargetGlobals[BoneIndex] = BodyComponentTransform.ToMatrix();
		bHasBodyTarget[BoneIndex] = 1;
		bPoseChanged = true;
	}

	if (!bPoseChanged)
	{
		return;
	}

	TArray<FMatrix> NewGlobals;
	NewGlobals.resize(BoneCount, FMatrix::Identity);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;

		FMatrix DesiredGlobal = FMatrix::Identity;
		if (bHasBodyTarget[BoneIndex])
		{
			DesiredGlobal = BodyTargetGlobals[BoneIndex];
		}
		else
		{
			const FMatrix& CurrentLocal = BoneEditLocalMatrices[BoneIndex];
			DesiredGlobal = (ParentIndex >= 0)
				? CurrentLocal * NewGlobals[ParentIndex]
				: CurrentLocal;
		}

		NewGlobals[BoneIndex] = DesiredGlobal;
		if (ParentIndex >= 0)
		{
			BoneEditLocalMatrices[BoneIndex] = DesiredGlobal * GetAffineInverseForPoseSync(NewGlobals[ParentIndex]);
		}
		else
		{
			BoneEditLocalMatrices[BoneIndex] = DesiredGlobal;
		}
	}

	bUseBoneEditPose = true;
	RefreshSkinningAfterPoseChanged();
	MarkWorldBoundsDirty();
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstance(FName BoneName) const
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return nullptr;
	}

	PhysicsAsset->UpdateBodySetupIndexMap();
	return GetBodyInstance(PhysicsAsset->FindBodyIndex(BoneName));
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstance(int32 BodyIndex) const
{
	if (BodyIndex < 0 || BodyIndex >= static_cast<int32>(Bodies.size()))
	{
		return nullptr;
	}

	return Bodies[BodyIndex];
}

FConstraintInstance* USkeletalMeshComponent::GetConstraintInstance(int32 ConstraintIndex) const
{
	if (ConstraintIndex < 0 || ConstraintIndex >= static_cast<int32>(Constraints.size()))
	{
		return nullptr;
	}

	return Constraints[ConstraintIndex];
}

void USkeletalMeshComponent::UpdateWorldAABB() const
{
	if (bRagdollActive && !Bodies.empty())
	{
		FBoundingBox RagdollBounds;

		for (FBodyInstance* Body : Bodies)
		{
			if (!Body || !Body->Actor)
			{
				continue;
			}

			if (Body->Actor->getNbShapes() == 0)
			{
				continue;
			}

			// PhysX actor + attached shapes 기준 world AABB
			const PxBounds3 PxBounds = Body->Actor->getWorldBounds(1.01f);

			if (!PxBounds.isValid())
			{
				continue;
			}

			const FVector Min = ToFVector(PxBounds.minimum);
			const FVector Max = ToFVector(PxBounds.maximum);

			RagdollBounds.Expand(Min);
			RagdollBounds.Expand(Max);
		}

		if (RagdollBounds.IsValid())
		{
			WorldAABBMinLocation = RagdollBounds.Min;
			WorldAABBMaxLocation = RagdollBounds.Max;
			bWorldAABBDirty = false;
			bHasValidWorldAABB = true;
			return;
		}
	}

	USkinnedMeshComponent::UpdateWorldAABB();
}

ECollisionPropertyExposure USkeletalMeshComponent::GetCollisionPropertyExposure() const
{
	return ECollisionPropertyExposure::Full;
}

bool USkeletalMeshComponent::EvaluateAnimInstance(float DeltaTime)
{
    if (!AnimInstance) return false;

    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh) return false;
    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset || Asset->Bones.empty()) return false;

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        if (!CanUseAnimation(SingleNode->GetAnimationAsset()))
        {
            SingleNode->SetAnimationAsset(nullptr);
            return false;
        }
    }

    AnimInstance->UpdateAnimation(DeltaTime);

    // Root motion 적용은 UCharacterMovementComponent 가 책임.
    // CMC::TickComponent (TG_DuringPhysics) 가 매 frame 이 AnimInstance->ConsumeRootMotion 으로
    // 누적값을 가져가 capsule 이동 / 회전에 반영한다 (sweep / floor stick 통과).
    // Mesh 는 actor transform 을 직접 만지지 않는다 — UE 본가 패턴.
    //
    // 주의: CMC 가 없는 actor 에 root motion 켠 anim 을 붙이면 누적값이 anywhere 도
    // 소비되지 않아 in-place 로 보인다. ACharacter 외 케이스에서 root motion 이 필요해지면
    // 별도 소비 경로가 추가되어야 한다.

    FPoseContext Out;
    Out.SkeletalMesh = Mesh;
    Out.Pose.resize(Asset->Bones.size());
    Out.ResetToRefPose();
    AnimInstance->EvaluatePose(Out);

    SetAnimationPose(Out.Pose, Out.MorphWeights);
    return true;
}

void USkeletalMeshComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
    USkinnedMeshComponent::AddReferencedObjects(Collector);

    Collector.AddReferencedObject(AnimationData.AnimToPlay, "USkeletalMeshComponent.AnimationData.AnimToPlay");
    Collector.AddReferencedObject(AnimInstance, "USkeletalMeshComponent.AnimInstance");
	Collector.AddReferencedObject(PhysicsAssetOverride, "USkeletalMeshComponent.PhysicsAssetOverride");
}
