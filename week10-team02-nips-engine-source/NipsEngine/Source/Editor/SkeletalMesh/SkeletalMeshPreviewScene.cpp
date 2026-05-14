#include "Editor/SkeletalMesh/SkeletalMeshPreviewScene.h"

#include "Editor/EditorEngine.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "GameFramework/WorldContext.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/ResourceManager.h"
#include "Engine/Viewport/ViewportCamera.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <windows.h>

#include "Component/GizmoComponent.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"

namespace
{
int32 GPreviewWorldCounter = 0;
constexpr const char* DefaultSkeletalMeshPath = "Asset/Fbx/Quinn_UE5/SKM_Quinn_Simple.asset";

float GetBoneJointPickRadius()
{
	return 0.05f;
}

bool IntersectRayJointSphere(const FRay& Ray, const FVector& JointPosition, float Radius, float& OutRayT, float& OutDistanceSq)
{
	const FVector ToJoint = JointPosition - Ray.Origin;
	float RayT = FVector::DotProduct(ToJoint, Ray.Direction);

	if (RayT < 0.0f)
	{
		const bool bRayStartsInsideJoint = ToJoint.SizeSquared() <= Radius * Radius;
		if (!bRayStartsInsideJoint)
		{
			return false;
		}

		RayT = 0.0f;
	}

	const FVector ClosestPoint = Ray.Origin + Ray.Direction * RayT;
	const float DistanceSq = (JointPosition - ClosestPoint).SizeSquared();
	if (DistanceSq > Radius * Radius)
	{
		return false;
	}

	OutRayT = RayT;
	OutDistanceSq = DistanceSq;
	return true;
}
}

FSkeletalMeshPreviewScene::~FSkeletalMeshPreviewScene()
{
	Shutdown();
}

void FSkeletalMeshPreviewScene::Initialize(UEditorEngine* InEditor)
{
	if (Editor != nullptr)
	{
		return;
	}

	Editor = InEditor;

	ViewportClient.SetPreviewScene(this);
	PreviewViewport.SetClient(&ViewportClient);
	ViewportClient.SetState(&PreviewViewport.GetState());
	ViewportClient.SetViewportType(EVT_Perspective);
	ViewportClient.ApplyCameraMode();

	PreviewInputController.SetViewportClient(&ViewportClient);
	PreviewInputRouter.SetEditorWorldController(&PreviewInputController);

	std::string ContextName = "SkeletalMeshViewer_Preview_" + std::to_string(GPreviewWorldCounter++);
	WorldHandle = FName(ContextName.c_str());

	PreviewInputController.SetPreviewScene(this);

	// Gizmo
	PreviewGizmo = UObjectManager::Get().CreateObject<UGizmoComponent>();
	if (PreviewGizmo)
	{
		PreviewGizmo->Deactivate();
	}

	// World
	FWorldContext& Context = Editor->CreateWorldContext(EWorldType::ViewerPreview, WorldHandle, ContextName);
	PreviewWorld = Context.World;
	PreviewWorld->SetWorldType(EWorldType::ViewerPreview);
	PreviewWorld->SetActiveCamera(ViewportClient.GetCamera());

	// Actor
	PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshActor>();
	PreviewActor->InitDefaultComponents();

	USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(DefaultSkeletalMeshPath);
	SetSkeletalMesh(Mesh);

	ADirectionalLightActor* LightActor = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, -45.0f, 180.0f));

	AAmbientLightActor* AmbientLightActor = PreviewWorld->SpawnActor<AAmbientLightActor>();
	AmbientLightActor->InitDefaultComponents();
	UAmbientLightComponent* AmbientLightComponent = static_cast<UAmbientLightComponent*>(AmbientLightActor->GetRootComponent());
	AmbientLightComponent->SetIntensity(2.0f);
}

void FSkeletalMeshPreviewScene::Shutdown()
{
	if (Editor != nullptr && PreviewWorld != nullptr)
	{
		Editor->DestroyWorldContext(WorldHandle);

		if (PreviewGizmo)
		{
			UObjectManager::Get().DestroyObject(PreviewGizmo);
			PreviewGizmo = nullptr;
		}

		PreviewWorld = nullptr;
		PreviewActor = nullptr;
		ViewportClient.SetState(nullptr);
		PreviewViewport.SetClient(nullptr);
		Editor = nullptr;
	}
}

void FSkeletalMeshPreviewScene::Tick(float DeltaTime)
{
	if (!Editor)
	{
		return;
	}

	bool bGizmoActive = PreviewGizmo && (PreviewGizmo->IsPressedOnHandle() || PreviewGizmo->IsHolding());

	const bool bMouseControlDown = FInputRouter::GetKey(VK_LBUTTON) || FInputRouter::GetKey(VK_RBUTTON) || FInputRouter::GetKey(VK_MBUTTON) || bGizmoActive;
	const bool bWasPreviewInputCaptured = bPreviewInputCaptured;

	const bool bPreviewCaptureBegin =
		bPreviewHovered &&
		(FInputRouter::GetKeyDown(VK_LBUTTON) || FInputRouter::GetKeyDown(VK_RBUTTON) || FInputRouter::GetKeyDown(VK_MBUTTON));

	if (bPreviewCaptureBegin)
	{
		bPreviewInputCaptured = true;
	}

	FInputRouteContext Context;
	Context.Window = Editor->GetWindow();
	Context.ViewportRect = PreviewInputRect;
	Context.bHovered = bPreviewHovered || bPreviewInputCaptured || bWasPreviewInputCaptured;
	Context.bInputActive = true;
	Context.bControlLocked = false;
	Context.bHasActiveCamera = true;
	Context.bIgnoreGuiBlock = true;

	PreviewInputRouter.Tick(DeltaTime, Context);

	if (!bMouseControlDown)
	{
		bPreviewInputCaptured = false;
		if (!FInputRouter::GetKey(VK_RBUTTON))
		{
			PreviewInputController.OnRightMouseButtonUp();
		}
	}

	ViewportClient.Tick(DeltaTime);
}

void FSkeletalMeshPreviewScene::SetVisible(bool bInVisible)
{
	if (!Editor)
	{
		return;
	}

	if (FWorldContext* Context = Editor->GetWorldContextFromHandle(WorldHandle))
	{
		Context->bPaused = !bInVisible;
	}
}

void FSkeletalMeshPreviewScene::SetSkeletalMesh(USkeletalMesh* Mesh)
{
	if (!PreviewActor)
	{
		return;
	}

	if (USkeletalMeshComponent* PreviewMeshComponent = static_cast<USkeletalMeshComponent*>(PreviewActor->GetRootComponent()))
	{
		PreviewMeshComponent->SetSkeletalMesh(Mesh);
	}
	SelectBone(-1);
	ResetPose();
}

void FSkeletalMeshPreviewScene::ResetPose()
{
	if (USkeletalMeshComponent* Comp = GetPreviewMeshComponent())
	{
		Comp->ResetPose();
	}
}

USkeletalMeshComponent* FSkeletalMeshPreviewScene::GetPreviewMeshComponent() const
{
	if (PreviewActor)
	{
		return static_cast<USkeletalMeshComponent*>(PreviewActor->GetRootComponent());
	}
	return nullptr;
}

USkeletalMesh* FSkeletalMeshPreviewScene::GetCurrentSkeletalMesh() const
{
	if (USkeletalMeshComponent* Comp = GetPreviewMeshComponent())
	{
		return Comp->GetSkeletalMesh();
	}
	return nullptr;
}

void FSkeletalMeshPreviewScene::SelectBone(int32 BoneIndex)
{
	SelectedBoneIndex = BoneIndex;

	if (PreviewGizmo)
	{
		if (BoneIndex >= 0 && GetPreviewMeshComponent())
		{
			PreviewGizmo->SetTargetBone(GetPreviewMeshComponent(), BoneIndex);
		}
		else
		{
			PreviewGizmo->ClearTransformTarget();
		}
	}
}

int32 FSkeletalMeshPreviewScene::GetSelectedBoneIndex() const
{
	return SelectedBoneIndex;
}

bool FSkeletalMeshPreviewScene::PickBoneJoint(const FRay& Ray, int32& OutBoneIndex) const
{
	OutBoneIndex = -1;

	USkeletalMeshComponent* MeshComp = GetPreviewMeshComponent();
	if (MeshComp == nullptr || !MeshComp->HasValidMesh()) return false;

	USkeletalMesh* Mesh = MeshComp->GetSkeletalMesh();
	if (Mesh == nullptr || !Mesh->HasValidSkeleton()) return false;

	const TArray<FSkeletalBone>& Bones = Mesh->GetBones();

	float BestHitT = FLT_MAX;

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
	{
		const FVector JointPosition = MeshComp->GetBoneWorldTransform(BoneIndex).GetLocation();
		const float PickRadius = GetBoneJointPickRadius();

		float RayT = 0.0f;
		float DistanceSq = 0.0f;

		if (IntersectRayJointSphere(Ray, JointPosition, PickRadius, RayT, DistanceSq))
		{
			float HitT = RayT - std::sqrt(PickRadius * PickRadius - DistanceSq);

			if (HitT < BestHitT)
			{
				BestHitT = HitT;
				OutBoneIndex = BoneIndex;
			}
		}
	}

	return OutBoneIndex >= 0;
}

void FSkeletalMeshPreviewScene::SetViewportSize(uint32 Width, uint32 Height)
{
	PreviewViewport.SetRect(FViewportRect(0, 0, Width, Height));
	ViewportClient.SetViewportSize(static_cast<float>(Width), static_cast<float>(Height));
}

void FSkeletalMeshPreviewScene::SetInputRectFromScreenRect(float MinX, float MinY, float MaxX, float MaxY)
{
	const int32 X = static_cast<int32>(MinX);
	const int32 Y = static_cast<int32>(MinY);
	const int32 Width = static_cast<int32>(MaxX - MinX);
	const int32 Height = static_cast<int32>(MaxY - MinY);

	PreviewInputRect = FViewportRect(X, Y, Width, Height);
}
