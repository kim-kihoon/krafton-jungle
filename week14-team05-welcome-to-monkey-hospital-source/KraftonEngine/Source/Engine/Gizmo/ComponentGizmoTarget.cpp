#include "ComponentGizmoTarget.h"
#include "Component/SceneComponent.h"
#include <cmath>

FComponentGizmoTarget::FComponentGizmoTarget()
	: Component(nullptr)
{
}

FComponentGizmoTarget::FComponentGizmoTarget(USceneComponent* InComponent)
	: Component(InComponent)
{
}

bool FComponentGizmoTarget::IsValid() const
{
	return Component != nullptr;
}

UWorld* FComponentGizmoTarget::GetWorld() const
{
	return Component ? Component->GetWorld() : nullptr;
}

FVector FComponentGizmoTarget::GetWorldLocation() const
{
	return Component ? Component->GetWorldLocation() : FVector::ZeroVector;
}

FRotator FComponentGizmoTarget::GetWorldRotation() const
{
	return Component ? Component->GetWorldRotation() : FRotator::ZeroRotator;
}

FQuat FComponentGizmoTarget::GetWorldQuat() const
{
	return Component ? Component->GetWorldRotation().ToQuaternion() : FQuat::Identity;
}

FVector FComponentGizmoTarget::GetWorldScale() const
{
	return Component ? Component->GetWorldScale() : FVector::OneVector;
}

void FComponentGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
	if (Component)
	{
		Component->SetWorldLocation(NewLocation);
	}
}

void FComponentGizmoTarget::SetWorldRotation(const FRotator& NewRotation)
{
	if (Component)
	{
		Component->SetWorldRotation(NewRotation);
	}
}

void FComponentGizmoTarget::SetWorldRotation(const FQuat& NewQuat)
{
	if (Component)
	{
		Component->SetWorldRotation(NewQuat);
	}
}

void FComponentGizmoTarget::SetWorldScale(const FVector& NewScale)
{
	if (!Component)
	{
		return;
	}

	FVector RelativeScale = NewScale;
	if (USceneComponent* Parent = Component->GetParent())
	{
		const FVector ParentScale = Parent->GetWorldScale();
		RelativeScale.X = std::abs(ParentScale.X) > 1.0e-6f ? NewScale.X / ParentScale.X : NewScale.X;
		RelativeScale.Y = std::abs(ParentScale.Y) > 1.0e-6f ? NewScale.Y / ParentScale.Y : NewScale.Y;
		RelativeScale.Z = std::abs(ParentScale.Z) > 1.0e-6f ? NewScale.Z / ParentScale.Z : NewScale.Z;
	}

	Component->SetRelativeScale(RelativeScale);
}

void FComponentGizmoTarget::AddWorldOffset(const FVector& Delta)
{
	if (Component)
	{
		Component->AddWorldOffset(Delta);
	}
}

void FComponentGizmoTarget::AddWorldRotation(const FQuat& Delta, bool bWorldSpace)
{
	if (!Component)
	{
		return;
	}

	if (bWorldSpace)
	{
		const FQuat CurrentWorldRotation = Component->GetWorldRotation().ToQuaternion();
		Component->SetWorldRotation((Delta * CurrentWorldRotation).GetNormalized());
	}
	else
	{
		Component->AddLocalRotation(Delta.GetNormalized());
	}
}

void FComponentGizmoTarget::AddScaleDelta(const FVector& Delta)
{
	if (Component)
	{
		FVector NewScale = GetWorldScale() + Delta;
		if (NewScale.X < 0.001f) NewScale.X = 0.001f;
		if (NewScale.Y < 0.001f) NewScale.Y = 0.001f;
		if (NewScale.Z < 0.001f) NewScale.Z = 0.001f;
		SetWorldScale(NewScale);
	}
}
