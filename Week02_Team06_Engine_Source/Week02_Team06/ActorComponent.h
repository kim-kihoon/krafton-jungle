#pragma once
#include "Defines.h"
#include "Object.h"

class AActor;

class UActorComponent : public UObject
{
	friend class AActor;

	DECLARE_CLASS(UActorComponent, UObject)

public:
	UActorComponent() = default;
	virtual ~UActorComponent() = default;

public:
	AActor* GetOwner() const { return Owner; }

	template<class T>
	T* GetOwner() const
	{
		return dynamic_cast<T*>(GetOwner());
	}

	virtual void InitializeComponent() {};
	virtual void Release() {};

	virtual void TickComponent(float DeltaTime) {}

private:
	AActor* Owner = nullptr;
};

