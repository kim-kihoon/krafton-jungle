#pragma once

#include "Math/Transform.h"
#include "Math/Rotator.h"
#include "Component/ActorComponent.h"
#include "Math/MathUtils.h"
#include "Object/FName.h"

class AActor;

#include "Object/Ptr/WeakObjectPtr.h"
#include "Source/Engine/Component/SceneComponent.generated.h"

UCLASS()
class USceneComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	USceneComponent();
	~USceneComponent();
	void RouteComponentDestroyed() override;
	void BeginDestroy() override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

	// Parent Relation Manager
	UFUNCTION(Callable, Category="Scene|Hierarchy")
    void AttachToComponent(USceneComponent* InParent, const FName& InSocketName = FName::None);
	UFUNCTION(Callable, Category="Scene|Hierarchy")
	void SetParent(USceneComponent* NewParent);
	UFUNCTION(Pure, Category="Scene|Hierarchy")
	USceneComponent* GetParent() const { return ParentComponent.Get(); }
	UFUNCTION(Callable, Category="Scene|Hierarchy")
	void AddChild(USceneComponent* NewChild);
	UFUNCTION(Callable, Category="Scene|Hierarchy")
	void RemoveChild(USceneComponent* Child);
	UFUNCTION(Pure, Category="Scene|Hierarchy")
	bool ContainsChild(const USceneComponent* Child) const;
    UFUNCTION(Pure, Category="Scene|Hierarchy")
    FName GetAttachSocketName() const { return AttachSocketName; }
	UFUNCTION(Pure, Category="Scene|Hierarchy")
	bool IsDescendantOf(const USceneComponent* MaybeAncestor) const;
	UFUNCTION(Pure, Category="Scene|Hierarchy")
	TArray<USceneComponent*> GetChildComponents() const { return ChildComponents; }
	const TArray<USceneComponent*>& GetChildren() const { return ChildComponents; }

	void PreGetEditableProperties() override;
	void PostEditProperty(const char* PropertyName) override;

	// 반사 직렬화 전/후 훅 (Euler 캐시 스냅샷/복원). 반사 자체는 UObject 기본값(true)으로 자동.
	void OnPreSave(FArchive& Ar) override;
	void OnPostLoad(FArchive& Ar) override;

	virtual void UpdateWorldMatrix() const;
	UFUNCTION(Pure, Category="Scene|Socket")
	virtual bool HasSocket(const FName& SocketName) const;
	UFUNCTION(Pure, Category="Scene|Socket")
	virtual FTransform GetSocketTransform(const FName& SocketName) const;
	UFUNCTION(Pure, Category="Scene|Socket")
	FVector GetSocketWorldLocation(const FName& SocketName) const;
	UFUNCTION(Pure, Category="Scene|Socket")
	FRotator GetSocketWorldRotation(const FName& SocketName) const;
	UFUNCTION(Pure, Category="Scene|Socket")
	FVector GetSocketWorldScale(const FName& SocketName) const;
	UFUNCTION(Pure, Category="Scene|Socket")
	FVector GetSocketForwardVector(const FName& SocketName) const;
	UFUNCTION(Pure, Category="Scene|Socket")
	FVector GetSocketRightVector(const FName& SocketName) const;
	UFUNCTION(Pure, Category="Scene|Socket")
	FVector GetSocketUpVector(const FName& SocketName) const;
	UFUNCTION(Callable, Category="Scene|Transform")
	virtual void AddWorldOffset(const FVector& WorldDelta);
	UFUNCTION(Callable, Category="Scene|Transform")
	virtual void SetRelativeLocation(const FVector& NewLocation);
	UFUNCTION(Callable, Category="Scene|Transform")
	virtual void SetRelativeRotation(const FRotator& NewRotation);
	virtual void SetRelativeRotation(const FQuat& NewRotation);
	void SetRelativeRotation(const FVector& EulerRotation);	// FVector 호환
	UFUNCTION(Callable, Category="Scene|Transform")
	virtual void SetRelativeScale(const FVector& NewScale);
	UFUNCTION(Callable, Category="Scene|Transform")
	void SetRelativeTransform(const FTransform& NewTransform);
	UFUNCTION(Callable, Category="Scene|Transform")
	void SetAbsoluteScale(bool bInAbsoluteScale) { bAbsoluteScale = bInAbsoluteScale; MarkTransformDirty(); }
	UFUNCTION(Pure, Category="Scene|Transform")
	bool IsAbsoluteScale() const { return bAbsoluteScale; }

	// 누적 회전용 — Quat 합성으로 적용해 짐벌락/Euler 라운드트립 손실을 회피한다.
	// 매 프레임 누적이 필요한 코드는 GetRelativeRotation()→+delta→Set 패턴 대신 이걸 써야 한다.
	void AddLocalRotation(const FQuat& DeltaQuat);
	UFUNCTION(Callable, Category="Scene|Transform")
	void AddLocalRotation(const FRotator& DeltaRotator);
	void MarkTransformDirty();
	virtual void OnTransformDirty();
	void ApplyCachedEditRotator();
	FRotator& GetCachedEditRotator();	// 에디터 UI용 Euler 캐시 접근
	// Quat을 직접 세팅하면서 Euler 캐시도 함께 지정 (짐벌락 방지)
	void SetRelativeRotationWithEulerHint(const FQuat& NewQuat, const FRotator& EulerHint);
	const FMatrix& GetWorldMatrix() const;
	const FMatrix& GetWorldInverseMatrix() const;
	UFUNCTION(Pure, Category="Scene|Transform")
	FMatrix GetWorldMatrixValue() const { return GetWorldMatrix(); }
	UFUNCTION(Pure, Category="Scene|Transform")
	FMatrix GetWorldInverseMatrixValue() const { return GetWorldInverseMatrix(); }
	UFUNCTION(Pure, Category="Scene|Transform")
	FTransform GetWorldTransform() const;
	UFUNCTION(Callable, Category="Scene|Transform")
	void SetWorldLocation(FVector NewWorldLocation);
	UFUNCTION(Callable, Category="Scene|Transform")
	void SetWorldRotation(const FRotator& NewWorldRotation);
	void SetWorldRotation(const FQuat& NewWorldRotation);
	UFUNCTION(Pure, Category="Scene|Transform")
	FVector GetWorldLocation() const;
	UFUNCTION(Pure, Category="Scene|Transform")
	FRotator GetWorldRotation() const;
	UFUNCTION(Pure, Category="Scene|Transform")
	FVector GetWorldScale() const;
	UFUNCTION(Pure, Category="Scene|Transform")
	FTransform GetRelativeTransform() const { return RelativeTransform; }
	UFUNCTION(Pure, Category="Scene|Transform")
	FVector GetRelativeLocation() const { return RelativeTransform.Location; }
	UFUNCTION(Pure, Category="Scene|Transform")
	FRotator GetRelativeRotation() const { return RelativeTransform.GetRotator(); }
	const FQuat& GetRelativeQuat() const { return RelativeTransform.Rotation; }
	UFUNCTION(Pure, Category="Scene|Transform")
	FVector GetRelativeScale() const { return RelativeTransform.Scale; }
	UFUNCTION(Pure, Category="Scene|Transform")
	FVector GetForwardVector() const;
	UFUNCTION(Pure, Category="Scene|Transform")
	FVector GetUpVector() const;
	UFUNCTION(Pure, Category="Scene|Transform")
	FVector GetRightVector() const;

	UFUNCTION(Pure, Category="Scene|Transform")
	FMatrix GetRelativeMatrix() const;

	UFUNCTION(Callable, Category="Scene|Transform")
	void Move(const FVector& Delta);
	UFUNCTION(Callable, Category="Scene|Transform")
	void MoveLocal(const FVector& Delta);
	UFUNCTION(Callable, Category="Scene|Transform")
	void Rotate(float DeltaYaw, float DeltaPitch);

protected:
	TWeakObjectPtr<USceneComponent> ParentComponent;
	TArray<USceneComponent*> ChildComponents;

	bool bAbsoluteScale = false;

	mutable bool bTransformDirty = true;

	UPROPERTY(Edit, Save, Category="Transform", DisplayName="Location", Member=RelativeTransform.Location, Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Transform", DisplayName="Scale", Member=RelativeTransform.Scale, Type=Vec3, Min=0.0f, Max=0.0f, Speed=0.1f);
	FTransform RelativeTransform;
	UPROPERTY(Edit, Save, Category="Transform", DisplayName="Rotation", Type=Rotator, Min=0.0f, Max=0.0f, Speed=0.1f)
	mutable FRotator CachedEditRotator;	// 에디터 프로퍼티 바인딩용 (Euler 캐시)
	mutable bool bCachedEulerDirty = true;	// Quat가 외부에서 변경됐을 때만 Euler 재계산
	UPROPERTY(Edit, Save, Category="Transform", DisplayName="Attach Socket")
	FName AttachSocketName = FName::None;

	//world matrix caching
	mutable FMatrix CachedWorldMatrix{};
	//inverse world matrix caching
	mutable FMatrix CachedInverseWorldMatrix{};
	mutable bool bInverseWorldDirty = true;
	mutable FName LastInvalidAttachSocketWarning = FName::None;
};

