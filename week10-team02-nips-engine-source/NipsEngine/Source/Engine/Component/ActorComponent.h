#pragma once

#include "Object/Object.h"
#include "Core/PropertyTypes.h"

class AActor;

class UActorComponent : public UObject
{
public:
	DECLARE_CLASS(UActorComponent, UObject)
	
	virtual void BeginPlay();
	virtual void EndPlay();

	virtual void Activate();
	virtual void Deactivate();

	void ExecuteTick(float DeltaTime);
	void SetActive(bool bNewActive);
	inline void SetAutoActivate(bool bNewAutoActivate) { bAutoActivate = bNewAutoActivate; }
	inline void SetComponentTickEnabled(bool bEnabled) { bCanEverTick = bEnabled; }

	inline bool IsActive() { return bIsActive; }
	inline bool IsAutoActivate() { return bAutoActivate; }
	inline bool IsComponentTickEnabled() const { return bCanEverTick; }
	bool HasBegunPlay() const { return bHasBegunPlay; }

	void SetOwner(AActor* Actor) { Owner = Actor; }
	AActor* GetOwner() const { return Owner; }

	// 에디터에 노출할 프로퍼티 목록 반환. 하위 클래스에서 override하여 속성 추가.
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;

	// 프로퍼티 값 변경 후 호출. 하위 클래스에서 override하여 부수효과(리소스 재로딩 등) 처리.
	void PostEditProperty(const char* PropertyName) override {}

	virtual void Serialize(FArchive& Ar) override;

	// CopyPropertiesFrom 은 UObject 에 정의됩니다.
	// 컴포넌트-컴포넌트 간 소유 관계(Owner, Parent 등)는 Duplicate() 호출 측에서 별도 처리해야 합니다.

	void SetTransient(bool bInTransient) { bTransient = bInTransient; }
	bool IsTransient() const { return bTransient; }

	void SetEditorOnly(bool bInEditorOnly) { bIsEditorOnly = bInEditorOnly; }
	bool IsEditorOnly() const { return bIsEditorOnly; }

	void SetHiddenInEditor(bool bInHidden) { bHiddenInEditor = bInHidden; }
	bool IsHiddenInEditor() const { return bHiddenInEditor; }

    // AActor에 추가될 때 호출. 컴포넌트가 월드 시스템(SpatialIndex 등)에 자신을 등록하는 곳.
    virtual void OnRegister() {}
    // AActor에서 제거될 때 호출. OnRegister에서 등록한 내용을 정리하는 곳.
    virtual void OnUnregister() {}
    bool IsRegistered() const { return bRegistered; }

    protected:
    virtual void TickComponent(float DeltaTime) {}

    protected:
    AActor* Owner = nullptr;
    bool bRegistered = false;

    protected:
    bool bIsActive = true;
	bool bAutoActivate = true;
	bool bCanEverTick = true;
    bool bTransient = false;                // 런타임에만 존재, 직렬화 완전 제외
    bool bIsEditorOnly = false;             // 에디터 전용, PIE/Game 렌더 제외
    bool bHiddenInEditor = false;           // 에디터 컴포넌트 창에서 숨김
    bool bHasBegunPlay = false;
};
