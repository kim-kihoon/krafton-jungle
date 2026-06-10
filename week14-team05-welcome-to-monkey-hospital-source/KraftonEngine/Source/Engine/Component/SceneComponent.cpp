#include "SceneComponent.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Reflection/UClass.h"
#include <GameFramework/World.h>
#include "Core/Logging/Log.h"
#include "Serialization/Archive.h"
#include "Object/GarbageCollection.h"
#include "Core/Logging/Log.h"

#include <cctype>

HIDE_FROM_COMPONENT_LIST(USceneComponent)

static void NotifyOctreeTransformChanged(USceneComponent* Comp)
{
    if (!Comp) return;

    AActor* OwnerActor = Comp->GetOwner();
    if (!OwnerActor) return;

    UWorld* World = OwnerActor->GetWorld();
    if (!World) return;
	
    World->UpdateActorInOctree(OwnerActor);
}

static FVector GetSafeNormalizedAxis(const FVector& Axis, const FVector& Fallback)
{
	const float Length = Axis.Length();
	if (Length <= 1.0e-6f)
	{
		return Fallback;
	}

	return Axis / Length;
}

static FMatrix GetRotationTranslationWithoutScale(const FMatrix& Matrix)
{
	FVector XAxis = GetSafeNormalizedAxis(FVector(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]), FVector(1.0f, 0.0f, 0.0f));
	FVector YAxis = GetSafeNormalizedAxis(FVector(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]), FVector(0.0f, 1.0f, 0.0f));
	FVector ZAxis = GetSafeNormalizedAxis(FVector(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]), FVector(0.0f, 0.0f, 1.0f));

	FMatrix Result = FMatrix::Identity;
	Result.M[0][0] = XAxis.X; Result.M[0][1] = XAxis.Y; Result.M[0][2] = XAxis.Z;
	Result.M[1][0] = YAxis.X; Result.M[1][1] = YAxis.Y; Result.M[1][2] = YAxis.Z;
	Result.M[2][0] = ZAxis.X; Result.M[2][1] = ZAxis.Y; Result.M[2][2] = ZAxis.Z;
	Result.M[3][0] = Matrix.M[3][0];
	Result.M[3][1] = Matrix.M[3][1];
	Result.M[3][2] = Matrix.M[3][2];
	return Result;
}

static bool IsSocketAttachmentNameSet(const FName& SocketName)
{
	return SocketName.IsValid() && SocketName != FName::None;
}

static FQuat GetWorldRotationNoScale(const USceneComponent* Component)
{
	if (!Component)
	{
		return FQuat::Identity;
	}

	const USceneComponent* Parent = Component->GetParent();
	const FName AttachSocketName = Component->GetAttachSocketName();
	if (Parent && IsSocketAttachmentNameSet(AttachSocketName) && Parent->HasSocket(AttachSocketName))
	{
		const FQuat SocketWorldQuat = GetRotationTranslationWithoutScale(Parent->GetSocketTransform(AttachSocketName).ToMatrix()).ToQuat().GetNormalized();
		return (SocketWorldQuat * Component->GetRelativeQuat()).GetNormalized();
	}

	const FQuat ParentWorldQuat = GetWorldRotationNoScale(Parent);
	return (ParentWorldQuat * Component->GetRelativeQuat()).GetNormalized();
}

static const char* GetObjectClassName(const UObject* Object)
{
	return Object && Object->GetClass() ? Object->GetClass()->GetName() : "None";
}

static FString TrimWhitespace(FString Value)
{
	while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.front())))
	{
		Value.erase(Value.begin());
	}
	while (!Value.empty() && std::isspace(static_cast<unsigned char>(Value.back())))
	{
		Value.pop_back();
	}
	return Value;
}

static FName NormalizeSocketAttachmentName(const FName& SocketName)
{
	if (!SocketName.IsValid() || SocketName == FName::None)
	{
		return FName::None;
	}

	const FString TrimmedName = TrimWhitespace(SocketName.ToString());
	return TrimmedName.empty() ? FName::None : FName(TrimmedName);
}

void USceneComponent::AttachToComponent(USceneComponent* InParent, const FName& InSocketName)
{
	if (!InParent || InParent == this) return;
	AttachSocketName = NormalizeSocketAttachmentName(InSocketName);
	LastInvalidAttachSocketWarning = FName::None;
	SetParent(InParent);
}

void USceneComponent::PreGetEditableProperties()
{
	UActorComponent::PreGetEditableProperties();
	if (bCachedEulerDirty)
	{
		CachedEditRotator = RelativeTransform.GetRotator();
		bCachedEulerDirty = false;
	}
}

void USceneComponent::PostEditProperty(const char* PropertyName)
{
	bool bApplyChangeToPartition = (strcmp(PropertyName, "RelativeTransform.Location") == 0
								|| strcmp(PropertyName, "RelativeTransform.Scale") == 0
								|| strcmp(PropertyName, "CachedEditRotator") == 0
								|| strcmp(PropertyName, "Location") == 0
								|| strcmp(PropertyName, "Rotation") == 0
								|| strcmp(PropertyName, "Scale") == 0
								|| strcmp(PropertyName, "AttachSocketName") == 0
								|| strcmp(PropertyName, "Attach Socket") == 0);

	if (strcmp(PropertyName, "CachedEditRotator") == 0 || strcmp(PropertyName, "Rotation") == 0)
	{
		ApplyCachedEditRotator();
	}
	else
	{
		if (strcmp(PropertyName, "AttachSocketName") == 0 || strcmp(PropertyName, "Attach Socket") == 0)
		{
			AttachSocketName = NormalizeSocketAttachmentName(AttachSocketName);
			LastInvalidAttachSocketWarning = FName::None;
		}
		MarkTransformDirty();
	}

	if (bApplyChangeToPartition)
	{
		NotifyOctreeTransformChanged(this);
	}
}

void USceneComponent::OnPreSave(FArchive& /*Ar*/)
{
	// 반사 직렬화 전에 Euler 캐시를 현재 Quat 으로부터 스냅샷.
	CachedEditRotator = RelativeTransform.GetRotator();
	bCachedEulerDirty = false;
}

void USceneComponent::OnPostLoad(FArchive& /*Ar*/)
{
	// ParentComponent / ChildComponents 는 직렬화 제외 — 복제 단계에서 명시적으로 재구성.
	RelativeTransform.SetRotation(CachedEditRotator);
	bTransformDirty = true;
	bCachedEulerDirty = false;
	bInverseWorldDirty = true;
}

USceneComponent::USceneComponent()
{
	CachedWorldMatrix = FMatrix::Identity;

	bTransformDirty = true;
	UpdateWorldMatrix();
}

USceneComponent::~USceneComponent()
{
	if (!HasAnyFlags(RF_BeginDestroy))
	{
		BeginDestroy();
	}
}

void USceneComponent::RouteComponentDestroyed()
{
	if (bComponentDestroyRouted)
	{
		return;
	}

	TArray<USceneComponent*> Children;
	Children.reserve(ChildComponents.size());
	for (USceneComponent* Child : ChildComponents)
	{
		if (IsAliveObject(Child))
		{
			Children.push_back(Child);
		}
	}

	UActorComponent::RouteComponentDestroyed();

	if (USceneComponent* Parent = ParentComponent.GetEvenIfPendingKill())
	{
		Parent->RemoveChild(this);
	}
	ParentComponent.Reset();
	ChildComponents.clear();

	for (USceneComponent* Child : Children)
	{
		if (!IsAliveObject(Child))
		{
			continue;
		}

		Child->ParentComponent.Reset();
		Child->RouteComponentDestroyed();
		Child->MarkPendingKill();
	}
}

void USceneComponent::BeginDestroy()
{
	if (HasAnyFlags(RF_BeginDestroy))
	{
		return;
	}

	// UActorComponent::BeginDestroy() deliberately calls virtual EndPlay() before
	// virtual RouteComponentDestroyed(). Scene/Camera/Primitive components may need
	// Owner/Outer/World during EndPlay teardown; routing first resets those links
	// and can make EndPlay dereference a null owner during scene reload GC.
	UActorComponent::BeginDestroy();
}

void USceneComponent::AddReferencedObjects(FReferenceCollector& Collector)
{
	UActorComponent::AddReferencedObjects(Collector);
	Collector.AddReferencedObjects(ChildComponents, "ChildComponents");
}



void USceneComponent::SetParent(USceneComponent* NewParent)
{
	if (NewParent == ParentComponent.Get() || NewParent == this)
	{
		return;
	}

	if (NewParent && NewParent->IsDescendantOf(this))
	{
		UE_LOG("SceneComponent hierarchy cycle rejected: %s cannot be attached under its descendant %s", GetName().c_str(), NewParent->GetName().c_str());
		return;
	}

	if (USceneComponent* OldParent = ParentComponent.Get())
	{
		OldParent->RemoveChild(this);
	}

	ParentComponent.Reset(NewParent);
    LastInvalidAttachSocketWarning = FName::None;
	if (USceneComponent* Parent = ParentComponent.Get())
	{
		Parent->ChildComponents.push_back(this);
	}

	// 부모 변경 시 자신 및 하위 자식의 월드 행렬을 갱신하도록 dirty 마킹
	MarkTransformDirty();
}

void USceneComponent::AddChild(USceneComponent* NewChild)
{
	if (NewChild == nullptr)
	{
		return;
	}

	NewChild->SetParent(this);
}

void USceneComponent::RemoveChild(USceneComponent* Child)
{
	if (Child == nullptr)
	{
		return;
	}

	auto iter = std::find(ChildComponents.begin(), ChildComponents.end(), Child);

	if (iter != ChildComponents.end())
	{
		if ((*iter)->ParentComponent.GetEvenIfPendingKill() == this)
		{
			(*iter)->ParentComponent.Reset();
		}

		ChildComponents.erase(iter);
	}
}

bool USceneComponent::ContainsChild(const USceneComponent* Child) const
{
	if (Child == nullptr)
	{
		return false;
	}

	return std::find(ChildComponents.begin(),
		ChildComponents.end(), Child) != ChildComponents.end();
}

bool USceneComponent::IsDescendantOf(const USceneComponent* MaybeAncestor) const
{
	if (!MaybeAncestor)
	{
		return false;
	}

	TSet<const USceneComponent*> Visited;
	const USceneComponent* Current = this;
	while (Current)
	{
		if (Visited.find(Current) != Visited.end())
		{
			return false;
		}
		Visited.insert(Current);

		if (Current == MaybeAncestor)
		{
			return true;
		}

		Current = Current->ParentComponent.Get();
	}
	return false;
}

void USceneComponent::UpdateWorldMatrix() const
{
	if (bTransformDirty == false)
	{
		return;
	}

	FMatrix RelativeMatrix = GetRelativeMatrix();

	if (USceneComponent* Parent = ParentComponent.Get())
	{
		const bool bHasAttachSocketName = IsSocketAttachmentNameSet(AttachSocketName);
		const bool bParentHasSocket = bHasAttachSocketName && ParentComponent->HasSocket(AttachSocketName);

		if (bParentHasSocket)
		{
			const FMatrix SocketMatrix = ParentComponent->GetSocketTransform(AttachSocketName).ToMatrix();
			CachedWorldMatrix = RelativeMatrix * GetRotationTranslationWithoutScale(SocketMatrix);
		}
		else if (bHasAttachSocketName)
		{
			if (LastInvalidAttachSocketWarning != AttachSocketName)
			{
				UE_LOG("Attach socket not found, falling back to parent transform. Component=%s ComponentClass=%s Parent=%s ParentClass=%s Socket='%s'",
					GetName().c_str(),
					GetObjectClassName(this),
					ParentComponent ? ParentComponent->GetName().c_str() : "None",
					GetObjectClassName(ParentComponent),
					AttachSocketName.ToString().c_str());
				LastInvalidAttachSocketWarning = AttachSocketName;
			}

		    if (bAbsoluteScale)
		    {
		        // 에디터 아이콘 빌보드는 부모 스케일과 분리해 화면상 크기 변화를 막는다.
		        CachedWorldMatrix = RelativeMatrix * GetRotationTranslationWithoutScale(Parent->GetWorldMatrix());
		    }
		    else
		    {
		        CachedWorldMatrix = RelativeMatrix * Parent->GetWorldMatrix();
		    }
		}
		else
		{
			if (bAbsoluteScale)
			{
				// 에디터 아이콘 빌보드는 부모 스케일과 분리해 화면상 크기 변화를 막는다.
				CachedWorldMatrix = RelativeMatrix * GetRotationTranslationWithoutScale(ParentComponent->GetWorldMatrix());
			}
			else
			{
				CachedWorldMatrix = RelativeMatrix * ParentComponent->GetWorldMatrix();
			}
		}
	}
	else
	{
		CachedWorldMatrix = RelativeMatrix;
	}

	bTransformDirty = false;
}

bool USceneComponent::HasSocket(const FName& /*SocketName*/) const
{
	return false;
}

FTransform USceneComponent::GetSocketTransform(const FName& /*SocketName*/) const
{
	return FTransform(GetWorldMatrix());
}

FVector USceneComponent::GetSocketWorldLocation(const FName& SocketName) const
{
	return GetSocketTransform(SocketName).Location;
}

FRotator USceneComponent::GetSocketWorldRotation(const FName& SocketName) const
{
	return GetSocketTransform(SocketName).GetRotator();
}

FVector USceneComponent::GetSocketWorldScale(const FName& SocketName) const
{
	return GetSocketTransform(SocketName).Scale;
}

FVector USceneComponent::GetSocketForwardVector(const FName& SocketName) const
{
	const FMatrix Matrix = GetSocketTransform(SocketName).ToMatrix();
	FVector Forward(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]);
	Forward.Normalize();
	return Forward;
}

FVector USceneComponent::GetSocketRightVector(const FName& SocketName) const
{
	const FMatrix Matrix = GetSocketTransform(SocketName).ToMatrix();
	FVector Right(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]);
	Right.Normalize();
	return Right;
}

FVector USceneComponent::GetSocketUpVector(const FName& SocketName) const
{
	const FMatrix Matrix = GetSocketTransform(SocketName).ToMatrix();
	FVector Up(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]);
	Up.Normalize();
	return Up;
}

void USceneComponent::AddWorldOffset(const FVector& WorldDelta)
{
	if (USceneComponent* Parent = ParentComponent.Get())
	{
		const FMatrix& parentWorldMatrix = Parent->GetWorldMatrix();

		FMatrix parentWorldInverseMatrix = parentWorldMatrix.GetInverse();

		FVector localDelta = parentWorldInverseMatrix.TransformVector(WorldDelta);

		Move(localDelta);
	}
	else
	{
		Move(WorldDelta);
	}
}

void USceneComponent::SetRelativeLocation(const FVector& NewLocation)
{
	RelativeTransform.Location = NewLocation;
	MarkTransformDirty();
	NotifyOctreeTransformChanged(this);
}

void USceneComponent::SetRelativeRotation(const FRotator& NewRotation)
{
	CachedEditRotator = NewRotation.GetClamped();
	bCachedEulerDirty = false;
	RelativeTransform.SetRotation(NewRotation);
	MarkTransformDirty();
	NotifyOctreeTransformChanged(this);
}

void USceneComponent::SetRelativeRotation(const FQuat& NewRotation)
{
	bCachedEulerDirty = true;
	RelativeTransform.SetRotation(NewRotation);
	MarkTransformDirty();
	NotifyOctreeTransformChanged(this);
}

void USceneComponent::SetRelativeRotation(const FVector& EulerRotation)
{
	FRotator Rot(EulerRotation);
	CachedEditRotator = Rot;
	bCachedEulerDirty = false;
	RelativeTransform.SetRotation(Rot);
	MarkTransformDirty();
	NotifyOctreeTransformChanged(this);
}


void USceneComponent::AddLocalRotation(const FQuat& DeltaQuat)
{
	// Quat 합성으로 누적 — Euler 라운드트립이 없어 짐벌락에 안전.
	// 곱 순서: 로컬 축 기준 회전이므로 Current * Delta.
	RelativeTransform.SetRotation(RelativeTransform.Rotation * DeltaQuat);
	bCachedEulerDirty = true;
	MarkTransformDirty();
	NotifyOctreeTransformChanged(this);
}

void USceneComponent::AddLocalRotation(const FRotator& DeltaRotator)
{
	AddLocalRotation(DeltaRotator.ToQuaternion());
}

void USceneComponent::SetRelativeScale(const FVector& NewScale)
{
	RelativeTransform.Scale = NewScale;
	MarkTransformDirty();
	NotifyOctreeTransformChanged(this);
}

void USceneComponent::SetRelativeTransform(const FTransform& NewTransform)
{
	RelativeTransform = NewTransform;
	MarkTransformDirty();
	NotifyOctreeTransformChanged(this);
}


void USceneComponent::MarkTransformDirty()
{
	if (bTransformDirty && bInverseWorldDirty)
	{
		return; // 이미 dirty면 자식 재귀/부수효과를 다시 하지 않음
	}

	bTransformDirty = true;
	bInverseWorldDirty = true;
	OnTransformDirty();
	for (auto* Child : ChildComponents)
	{
		Child->MarkTransformDirty();
	}
}

void USceneComponent::OnTransformDirty()
{
}

FRotator& USceneComponent::GetCachedEditRotator()
{
	if (bCachedEulerDirty)
	{
		CachedEditRotator = RelativeTransform.GetRotator();
		bCachedEulerDirty = false;
	}
	return CachedEditRotator;
}

void USceneComponent::SetRelativeRotationWithEulerHint(const FQuat& NewQuat, const FRotator& EulerHint)
{
	CachedEditRotator = EulerHint.GetClamped();
	bCachedEulerDirty = false;
	RelativeTransform.SetRotation(NewQuat);
	MarkTransformDirty();
    NotifyOctreeTransformChanged(this);
}

void USceneComponent::ApplyCachedEditRotator()
{
	CachedEditRotator = CachedEditRotator.GetClamped();
	CachedEditRotator.Pitch = Clamp(CachedEditRotator.Pitch, -89.9f, 89.9f);
	bCachedEulerDirty = false;
	RelativeTransform.SetRotation(CachedEditRotator);
	MarkTransformDirty();
    NotifyOctreeTransformChanged(this);
}

const FMatrix& USceneComponent::GetWorldMatrix() const
{
	if (bTransformDirty == true)
	{
		UpdateWorldMatrix();
	}

	return CachedWorldMatrix;
}

const FMatrix& USceneComponent::GetWorldInverseMatrix() const
{
	GetWorldMatrix();

	if (bInverseWorldDirty == true)
	{
		CachedInverseWorldMatrix = CachedWorldMatrix.GetInverse();
		bInverseWorldDirty = false;
	}
	return CachedInverseWorldMatrix;
}

void USceneComponent::SetWorldLocation(FVector NewWorldLocation)
{
	if (USceneComponent* Parent = ParentComponent.Get())
	{
		const FMatrix& parentWorldInverseMatrix = Parent->GetWorldMatrix().GetInverse();

		FVector newRelativeLocation = NewWorldLocation * parentWorldInverseMatrix;

		SetRelativeLocation(newRelativeLocation);
	}
	else
	{
		SetRelativeLocation(NewWorldLocation);
	}
}

void USceneComponent::SetWorldRotation(const FRotator& NewWorldRotation)
{
	SetWorldRotation(NewWorldRotation.ToQuaternion());
}

void USceneComponent::SetWorldRotation(const FQuat& NewWorldRotation)
{
	const FQuat WorldQuat = NewWorldRotation.GetNormalized();

	if (USceneComponent* Parent = ParentComponent.Get())
	{
		FQuat ParentWorldQuat = GetWorldRotationNoScale(Parent);
		if (IsSocketAttachmentNameSet(AttachSocketName) && Parent->HasSocket(AttachSocketName))
		{
			ParentWorldQuat = GetRotationTranslationWithoutScale(Parent->GetSocketTransform(AttachSocketName).ToMatrix()).ToQuat().GetNormalized();
		}
		SetRelativeRotation((ParentWorldQuat.Inverse() * WorldQuat).GetNormalized());
	}
	else
	{
		SetRelativeRotation(WorldQuat);
	}
}

FVector USceneComponent::GetWorldLocation() const
{
	const FMatrix& WorldMatrix = GetWorldMatrix();
	return FVector(WorldMatrix.M[3][0], WorldMatrix.M[3][1], WorldMatrix.M[3][2]);
}

FRotator USceneComponent::GetWorldRotation() const
{
	const FQuat WorldQuat = GetWorldRotationNoScale(this);
	return WorldQuat.ToRotator();
}

FVector USceneComponent::GetWorldScale() const
{
	const FMatrix& WorldMatrix = GetWorldMatrix();

	float ScaleX = FVector(WorldMatrix.M[0][0], WorldMatrix.M[0][1], WorldMatrix.M[0][2]).Length();
	float ScaleY = FVector(WorldMatrix.M[1][0], WorldMatrix.M[1][1], WorldMatrix.M[1][2]).Length();
	float ScaleZ = FVector(WorldMatrix.M[2][0], WorldMatrix.M[2][1], WorldMatrix.M[2][2]).Length();

	return FVector(ScaleX, ScaleY, ScaleZ);
}

FTransform USceneComponent::GetWorldTransform() const
{
	return FTransform(GetWorldMatrix());
}

FVector USceneComponent::GetForwardVector() const
{
	const FMatrix& Matrix = GetWorldMatrix();
	FVector Forward(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]);
	Forward.Normalize();
	return Forward;
}

FVector USceneComponent::GetRightVector() const
{
	const FMatrix& Matrix = GetWorldMatrix();
	FVector Right(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]);
	Right.Normalize();
	return Right;
}

FVector USceneComponent::GetUpVector() const
{
	const FMatrix& Matrix = GetWorldMatrix();
	FVector Up(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]);
	Up.Normalize();
	return Up;
}

void USceneComponent::Move(const FVector& Delta)
{
	SetRelativeLocation(RelativeTransform.Location + Delta);
}

void USceneComponent::MoveLocal(const FVector& Delta)
{
	FVector Forward = GetForwardVector();
	FVector Right = GetRightVector();
	FVector Up = GetUpVector();

	SetRelativeLocation(RelativeTransform.Location
		+ Forward * Delta.X
		+ Right * Delta.Y
		+ Up * Delta.Z);
}

void USceneComponent::Rotate(float DeltaYaw, float DeltaPitch)
{
	FRotator Rot = GetCachedEditRotator();
	Rot.Yaw += DeltaYaw;
	Rot.Pitch += DeltaPitch;
	Rot.Pitch = Clamp(Rot.Pitch, -89.9f, 89.9f);
	Rot.Roll = 0.0f;

	SetRelativeRotationWithEulerHint(Rot.ToQuaternion(), Rot);
}

FMatrix USceneComponent::GetRelativeMatrix() const
{
	return RelativeTransform.ToMatrix();
}
