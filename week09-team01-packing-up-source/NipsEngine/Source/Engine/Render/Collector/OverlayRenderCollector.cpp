#include "OverlayRenderCollector.h"

#include "Component/ActorComponent.h"
#include "Component/AudioComponent.h"
#include "Component/AudioZoneComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/Collision/BoxComponent.h"
#include "Component/Collision/CapsuleComponent.h"
#include "Component/Collision/CylinderComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/Collision/SphereComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/LightComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Core/ResourceManager.h"
#include "Engine/Asset/StaticMesh.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/ActorIterator.h"
#include "Render/LineBatcher.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Spatial/WorldSpatialIndex.h"

#include <algorithm>

namespace
{
	FColor MakeBVHInternalNodeColor(int32 PathIndexFromLeaf, int32 PathLength)
	{
		if (PathLength <= 1)
		{
			return FColor::Yellow();
		}

		const float T = static_cast<float>(PathIndexFromLeaf) / static_cast<float>(PathLength - 1);
		return FColor::Lerp(FColor::Cyan(), FColor::Yellow(), T);
	}

	FMatrix MakeViewSubUVSelectionMatrix(const USubUVComponent* SubUVComp, const FRenderBus& RenderBus)
	{
		const FVector WorldScale = SubUVComp->GetWorldScale();
		return USubUVComponent::MakeBillboardWorldMatrix(
			SubUVComp->GetWorldLocation(),
			FVector(
				WorldScale.X > 0.01f ? WorldScale.X : 0.01f,
				SubUVComp->GetWidth() * WorldScale.Y,
				SubUVComp->GetHeight() * WorldScale.Z),
			RenderBus.GetCameraForward(),
			RenderBus.GetCameraRight(),
			RenderBus.GetCameraUp());
	}

	FAABB BuildQuadAABB(const FMatrix& WorldMatrix)
	{
		static constexpr FVector LocalQuadCorners[4] =
		{
			FVector(0.0f, -0.5f,  0.5f),
			FVector(0.0f,  0.5f,  0.5f),
			FVector(0.0f,  0.5f, -0.5f),
			FVector(0.0f, -0.5f, -0.5f)
		};

		FAABB Box;
		Box.Reset();

		for (const FVector& Corner : LocalQuadCorners)
		{
			Box.Expand(WorldMatrix.TransformPosition(Corner));
		}

		return Box;
	}

	FAABB BuildRenderAABB(const UPrimitiveComponent* PrimitiveComponent, const FRenderBus& RenderBus)
	{
		switch (PrimitiveComponent->GetPrimitiveType())
		{
		case EPrimitiveType::EPT_Billboard:
		{
			const UBillboardComponent* BillboardComponent = static_cast<const UBillboardComponent*>(PrimitiveComponent);
			return BuildQuadAABB(UBillboardComponent::MakeBillboardWorldMatrix(
				BillboardComponent->GetWorldLocation(),
				FVector(0.00f, BillboardComponent->GetWidth(), BillboardComponent->GetHeight()),
				RenderBus.GetCameraForward(),
				RenderBus.GetCameraRight(),
				RenderBus.GetCameraUp()));
		}
		case EPrimitiveType::EPT_Text:
		{
			const UTextRenderComponent* TextComp = static_cast<const UTextRenderComponent*>(PrimitiveComponent);
			return BuildQuadAABB(TextComp->GetTextMatrix());
		}
		case EPrimitiveType::EPT_SubUV:
		{
			const USubUVComponent* SubUVComp = static_cast<const USubUVComponent*>(PrimitiveComponent);
			return BuildQuadAABB(MakeViewSubUVSelectionMatrix(SubUVComp, RenderBus));
		}
		default:
			return PrimitiveComponent->GetWorldAABB();
		}
	}

	bool DrawShapeComponent(const UPrimitiveComponent* PrimitiveComponent, FLineBatcher* LineBatcher)
	{
		const UShapeComponent* ShapeComponent = Cast<UShapeComponent>(const_cast<UPrimitiveComponent*>(PrimitiveComponent));
		if (ShapeComponent == nullptr || LineBatcher == nullptr)
		{
			return false;
		}

		const FColor& ShapeColor = ShapeComponent->GetShapeColor();

		switch (ShapeComponent->GetCollisionType())
		{
		case ECollisionType::Box:
		{
			const UBoxComponent* BoxComponent = Cast<UBoxComponent>(const_cast<UPrimitiveComponent*>(PrimitiveComponent));
			if (BoxComponent == nullptr)
			{
				return true;
			}

			const FVector BoxExtent = BoxComponent->GetBoxExtent();
			const FVector BoxScale = BoxComponent->GetWorldScale();
			const FVector WorldExtent(
				std::fabs(BoxExtent.X) * std::fabs(BoxScale.X),
				std::fabs(BoxExtent.Y) * std::fabs(BoxScale.Y),
				std::fabs(BoxExtent.Z) * std::fabs(BoxScale.Z));
			const FOBB BoxOBB(BoxComponent->GetWorldLocation(), WorldExtent, BoxComponent->GetWorldMatrix().GetRotationMatrix());
			LineBatcher->AddOBB(BoxOBB, ShapeColor);
			return true;
		}
		case ECollisionType::Sphere:
		{
			const USphereComponent* SphereComponent = Cast<USphereComponent>(const_cast<UPrimitiveComponent*>(PrimitiveComponent));
			if (SphereComponent == nullptr)
			{
				return true;
			}

			LineBatcher->AddSphere(
				SphereComponent->GetWorldLocation(),
				std::fabs(SphereComponent->GetSphereRadius()) * std::max({
					std::fabs(SphereComponent->GetWorldScale().X),
					std::fabs(SphereComponent->GetWorldScale().Y),
					std::fabs(SphereComponent->GetWorldScale().Z) }),
				SphereComponent->GetRightVector(),
				SphereComponent->GetUpVector(),
				ShapeColor);
			return true;
		}
		case ECollisionType::Capsule:
		{
			const UCapsuleComponent* CapsuleComponent = Cast<UCapsuleComponent>(const_cast<UPrimitiveComponent*>(PrimitiveComponent));
			if (CapsuleComponent == nullptr)
			{
				return true;
			}

			const FVector CapsuleScale = CapsuleComponent->GetWorldScale();
			const float RadiusScale = std::max(std::fabs(CapsuleScale.X), std::fabs(CapsuleScale.Y));
			const float HeightScale = std::fabs(CapsuleScale.Z);
			const float Radius = std::fabs(CapsuleComponent->GetCapsuleRadius()) * RadiusScale;
			const float HalfHeight = std::max(std::fabs(CapsuleComponent->GetCapsuleHalfHeight()) * HeightScale, Radius);
			LineBatcher->AddCapsule(
				CapsuleComponent->GetWorldLocation(),
				HalfHeight,
				Radius,
				CapsuleComponent->GetUpVector(),
				CapsuleComponent->GetRightVector(),
				CapsuleComponent->GetForwardVector(),
				ShapeColor);
			return true;
		}
		case ECollisionType::Cylinder:
		{
			const UCylinderComponent* CylinderComponent = Cast<UCylinderComponent>(const_cast<UPrimitiveComponent*>(PrimitiveComponent));
			if (CylinderComponent == nullptr)
			{
				return true;
			}

			LineBatcher->AddCylinder(
				CylinderComponent->GetWorldLocation(),
				std::fabs(CylinderComponent->GetCylinderHalfHeight()) * std::fabs(CylinderComponent->GetWorldScale().Z),
				std::fabs(CylinderComponent->GetCylinderRadius()) * std::max({
					std::fabs(CylinderComponent->GetWorldScale().X),
					std::fabs(CylinderComponent->GetWorldScale().Y) }),
				CylinderComponent->GetUpVector(),
				CylinderComponent->GetRightVector(),
				CylinderComponent->GetForwardVector(),
				ShapeColor);
			return true;
		}
		default:
			return true;
		}
	}

	void DrawAudioComponentRange(const UAudioComponent* AudioComponent, FLineBatcher* LineBatcher)
	{
		if (AudioComponent == nullptr || LineBatcher == nullptr || !AudioComponent->IsSpatial())
		{
			return;
		}

		const FVector Center = AudioComponent->GetWorldLocation();
		const FVector Right = AudioComponent->GetRightVector();
		const FVector Up = AudioComponent->GetUpVector();

		LineBatcher->AddSphere(Center, AudioComponent->GetMinDistance(), Right, Up, FColor(80, 220, 255));
		LineBatcher->AddSphere(Center, AudioComponent->GetMaxDistance(), Right, Up, FColor(40, 120, 255));
	}

	void DrawAudioZoneRange(const UAudioZoneComponent* AudioZoneComponent, FLineBatcher* LineBatcher)
	{
		if (AudioZoneComponent == nullptr || LineBatcher == nullptr)
		{
			return;
		}

		const FOBB Box(
			AudioZoneComponent->GetWorldLocation(),
			AudioZoneComponent->GetScaledBoxExtent(),
			AudioZoneComponent->GetWorldMatrix().GetRotationMatrix());
		LineBatcher->AddOBB(Box, FColor(80, 255, 180));
	}
}

void FOverlayRenderCollector::CollectSelection(
	const TArray<AActor*>& SelectedActors,
	const FShowFlags& ShowFlags,
	EViewMode ViewMode,
	FRenderBus& RenderBus,
	FLineBatcher* LineBatcher)
{
	if (MeshBufferManager == nullptr)
	{
		return;
	}

	bool bHasSelectionMask = false;
	for (AActor* Actor : SelectedActors)
	{
		bHasSelectionMask |= CollectFromSelectedActor(Actor, ShowFlags, ViewMode, RenderBus, LineBatcher);
	}

	if (bHasSelectionMask)
	{
		FRenderCommand PostProcessCmd = {};
		PostProcessCmd.Type = ERenderCommandType::PostProcessOutline;
		PostProcessCmd.Material = FResourceManager::Get().GetMaterial("OutlineMaterial");

		UMaterial* Material = Cast<UMaterial>(PostProcessCmd.Material);
		Material->SetVector4("OutlineColor", FVector4(1.0f, 0.5f, 0.0f, 1.0f));
		Material->SetFloat("OutlineThicknessPixels", 5.0f);
		Material->SetVector2("OutlineViewportSize", RenderBus.GetViewportSize());
		Material->SetVector2("OutlineViewportOrigin", RenderBus.GetViewportOrigin());
		Material->DepthStencilType = EDepthStencilType::Default;
		Material->RasterizerType = ERasterizerType::SolidBackCull;
		Material->BlendType = EBlendType::AlphaBlend;

		RenderBus.AddCommand(ERenderPass::PostProcessOutline, PostProcessCmd);
	}
}

void FOverlayRenderCollector::CollectOutline(
	const TArray<AActor*>& Actors,
	const FVector4& OutlineColor,
	float OutlineThicknessPixels,
	FRenderBus& RenderBus)
{
	if (MeshBufferManager == nullptr)
	{
		return;
	}

	bool bHasSelectionMask = false;
	std::unordered_set<AActor*> SeenActors;

	for (AActor* Actor : Actors)
	{
		if (Actor == nullptr || !SeenActors.insert(Actor).second || !Actor->IsVisible())
		{
			continue;
		}

		for (UPrimitiveComponent* PrimitiveComponent : Actor->GetPrimitiveComponents())
		{
			if (PrimitiveComponent == nullptr || !PrimitiveComponent->IsVisible() || !PrimitiveComponent->SupportsOutline())
			{
				continue;
			}

			if (PrimitiveComponent->IsEditorOnly())
			{
				UWorld* World = Actor->GetFocusedWorld();
				if (World && World->GetWorldType() != EWorldType::Editor)
				{
					continue;
				}
			}

			if (Cast<UShapeComponent>(PrimitiveComponent) != nullptr)
			{
				continue;
			}

			FMeshBuffer* MeshBuffer = nullptr;
			if (PrimitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_StaticMesh)
			{
				UStaticMeshComponent* StaticMeshComp = static_cast<UStaticMeshComponent*>(PrimitiveComponent);
				MeshBuffer = MeshBufferManager->GetStaticMeshBuffer(StaticMeshComp->GetStaticMesh());
			}
			else
			{
				MeshBuffer = &MeshBufferManager->GetMeshBuffer(PrimitiveComponent->GetPrimitiveType());
			}

			if (MeshBuffer == nullptr || !MeshBuffer->IsValid())
			{
				continue;
			}

			FRenderCommand MaskCmd = {};
			MaskCmd.Type = PrimitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Billboard
				? ERenderCommandType::BillboardSelectionMask
				: ERenderCommandType::SelectionMask;
			MaskCmd.MeshBuffer = MeshBuffer;
			MaskCmd.PerObjectConstants = FPerObjectConstants(PrimitiveComponent->GetWorldMatrix());
			MaskCmd.SectionIndexStart = 0;
			MaskCmd.SectionIndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();

			if (PrimitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_SubUV)
			{
				MaskCmd.PerObjectConstants.Model = MakeViewSubUVSelectionMatrix(
					static_cast<USubUVComponent*>(PrimitiveComponent),
					RenderBus);
			}
			else if (PrimitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Billboard)
			{
				const UBillboardComponent* BillboardComponent = static_cast<const UBillboardComponent*>(PrimitiveComponent);
				MaskCmd.PerObjectConstants.Model = UBillboardComponent::MakeBillboardWorldMatrix(
					BillboardComponent->GetWorldLocation(),
					FVector(0.01f, BillboardComponent->GetWidth(), BillboardComponent->GetHeight()),
					RenderBus.GetCameraForward(),
					RenderBus.GetCameraRight(),
					RenderBus.GetCameraUp());
				MaskCmd.Constants.Billboard.Texture = static_cast<UBillboardComponent*>(PrimitiveComponent)->GetTexture();
			}

			RenderBus.AddCommand(ERenderPass::SelectionMask, MaskCmd);
			bHasSelectionMask = true;
		}
	}

	if (!bHasSelectionMask)
	{
		return;
	}

	FRenderCommand PostProcessCmd = {};
	PostProcessCmd.Type = ERenderCommandType::PostProcessOutline;
	PostProcessCmd.Material = FResourceManager::Get().GetMaterial("OutlineMaterial");
	if (PostProcessCmd.Material == nullptr)
	{
		return;
	}

	PostProcessCmd.Material->SetVector4("OutlineColor", OutlineColor);
	PostProcessCmd.Material->SetFloat("OutlineThicknessPixels", std::max(1.0f, OutlineThicknessPixels));
	PostProcessCmd.Material->SetVector2("OutlineViewportSize", RenderBus.GetViewportSize());
	PostProcessCmd.Material->SetVector2("OutlineViewportOrigin", RenderBus.GetViewportOrigin());

	if (UMaterial* Material = Cast<UMaterial>(PostProcessCmd.Material))
	{
		Material->DepthStencilType = EDepthStencilType::Default;
		Material->RasterizerType = ERasterizerType::SolidBackCull;
		Material->BlendType = EBlendType::AlphaBlend;
	}

	RenderBus.AddCommand(ERenderPass::PostProcessOutline, PostProcessCmd);
}

void FOverlayRenderCollector::CollectDebugBounds(
	UWorld* World,
	const FShowFlags& ShowFlags,
	EViewMode ViewMode,
	FRenderBus& RenderBus,
	FLineBatcher* LineBatcher)
{
	(void)ViewMode;

	if (World == nullptr || LineBatcher == nullptr)
	{
		return;
	}

	std::unordered_set<int32> SeenBVHNodeIndices;
	for (TActorIterator<AActor> Iter(World); Iter; ++Iter)
	{
		AActor* Actor = *Iter;
		if (Actor == nullptr || !Actor->IsVisible())
		{
			continue;
		}

		CollectDebugBoundsFromActor(Actor, ShowFlags, ViewMode, RenderBus, LineBatcher, SeenBVHNodeIndices);
	}
}

void FOverlayRenderCollector::CollectGrid(float GridSpacing, int32 GridHalfLineCount, FRenderBus& RenderBus, bool bOrthographic)
{
	FRenderCommand Cmd = {};
	Cmd.Type = ERenderCommandType::Grid;
	Cmd.Constants.Grid.GridSpacing = GridSpacing;
	Cmd.Constants.Grid.GridHalfLineCount = GridHalfLineCount;
	Cmd.Constants.Grid.bOrthographic = bOrthographic;
	RenderBus.AddCommand(ERenderPass::Grid, Cmd);
}

void FOverlayRenderCollector::CollectGizmo(
	UGizmoComponent* Gizmo,
	const FShowFlags& ShowFlags,
	FRenderBus& RenderBus,
	bool bIsActiveOperation)
{
	if (ShowFlags.bGizmo == false) return;
	if (!Gizmo || !Gizmo->IsVisible()) return;
	if (MeshBufferManager == nullptr) return;

	FMeshBuffer* GizmoMesh = &MeshBufferManager->GetMeshBuffer(Gizmo->GetPrimitiveType());
	FMatrix WorldMatrix = Gizmo->GetWorldMatrix();
	bool bHolding = Gizmo->IsHolding();
	int32 SelectedAxis = Gizmo->GetSelectedAxis();

	auto CreateGizmoCmd = [&](bool bInner) {
		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Gizmo;
		Cmd.MeshBuffer = GizmoMesh;

		Cmd.SectionIndexStart = 0;
		Cmd.SectionIndexCount = GizmoMesh->GetIndexBuffer().GetIndexCount();

		Cmd.PerObjectConstants = FPerObjectConstants{ WorldMatrix };

		UMaterial* Material = Cast<UMaterial>(Gizmo->GetMaterial());
		Cmd.Material = Material;

		if (bInner)
		{
			Material->DepthStencilType = EDepthStencilType::GizmoInside;
			Material->BlendType = EBlendType::AlphaBlend;
		}
		else
		{
			Material->DepthStencilType = EDepthStencilType::GizmoOutside;
			Material->BlendType = EBlendType::Opaque;
		}

		Material->SetVector4("GizmoColorTint", FVector4(1.0f, 1.0f, 1.0f, 1.0f));
		Material->SetBool("bIsInnerGizmo", bInner);
		Material->SetBool("bClicking", bHolding);
		Material->SetUInt("SelectedAxis", (SelectedAxis >= 0 && bIsActiveOperation) ? static_cast<uint32>(SelectedAxis) : 0xffffffffu);
		Material->SetFloat("HoveredAxisOpacity", 0.3f);

		return Cmd;
	};

	RenderBus.AddCommand(ERenderPass::DepthLess, CreateGizmoCmd(false));

	if (!bHolding)
	{
		RenderBus.AddCommand(ERenderPass::DepthLess, CreateGizmoCmd(true));
	}
}

bool FOverlayRenderCollector::CollectFromSelectedActor(
	AActor* Actor,
	const FShowFlags& ShowFlags,
	EViewMode ViewMode,
	FRenderBus& RenderBus,
	FLineBatcher* LineBatcher)
{
	if (!Actor->IsVisible()) return false;
	if (MeshBufferManager == nullptr) return false;

	bool bHasSelectionMask = false;
	std::unordered_set<int32> SeenBVHNodeIndices;

	for (UPrimitiveComponent* primitiveComponent : Actor->GetPrimitiveComponents())
	{
		if (!primitiveComponent->IsVisible()) continue;
		if (primitiveComponent->IsEditorOnly())
		{
			UWorld* World = Actor->GetFocusedWorld();
			if (World && World->GetWorldType() != EWorldType::Editor)
				continue;
		}

		if (DrawShapeComponent(primitiveComponent, LineBatcher))
		{
			continue;
		}

		FMeshBuffer* MeshBuffer = nullptr;
		if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_StaticMesh)
		{
			auto* StaticMeshComp = static_cast<UStaticMeshComponent*>(primitiveComponent);
			MeshBuffer = MeshBufferManager->GetStaticMeshBuffer(StaticMeshComp->GetStaticMesh());
		}
		else
		{
			MeshBuffer = &MeshBufferManager->GetMeshBuffer(primitiveComponent->GetPrimitiveType());
		}

		if (!MeshBuffer)
		{
			continue;
		}

		FRenderCommand BaseCmd{};
		BaseCmd.MeshBuffer = MeshBuffer;
		BaseCmd.PerObjectConstants = FPerObjectConstants(primitiveComponent->GetWorldMatrix());
		BaseCmd.SectionIndexStart = 0;
		BaseCmd.SectionIndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();

		if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Text)
		{
			UTextRenderComponent* TextComp = static_cast<UTextRenderComponent*>(primitiveComponent);
			const FFontResource* Font = TextComp->GetFont();
			if (!Font || !Font->IsLoaded()) continue;
			const FString& Text = TextComp->GetText();
			if (Text.empty()) continue;

			FMatrix WorldMatrix = TextComp->GetTextMatrix();

			FRenderCommand TextCmd = BaseCmd;
			BaseCmd.PerObjectConstants = FPerObjectConstants(WorldMatrix);
			TextCmd.PerObjectConstants = FPerObjectConstants(TextComp->GetWorldMatrix(), TextComp->GetColor());
			TextCmd.Type = ERenderCommandType::Font;
			TextCmd.Constants.Font.Text = &Text;
			TextCmd.Constants.Font.Font = Font;
			TextCmd.Constants.Font.Scale = TextComp->GetFontSize();
			RenderBus.AddCommand(ERenderPass::Font, TextCmd);
		}
		else if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_SubUV)
		{
			BaseCmd.PerObjectConstants.Model = MakeViewSubUVSelectionMatrix(
				static_cast<USubUVComponent*>(primitiveComponent),
				RenderBus);
		}
		else if(primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Billboard)
		{
			const UBillboardComponent* BComp = static_cast<const UBillboardComponent*>(primitiveComponent);
			BaseCmd.PerObjectConstants.Model = UBillboardComponent::MakeBillboardWorldMatrix(
				BComp->GetWorldLocation(),
				FVector(0.01f, BComp->GetWidth(), BComp->GetHeight()),
				RenderBus.GetCameraForward(),
				RenderBus.GetCameraRight(),
				RenderBus.GetCameraUp());
		}

		if (!primitiveComponent->SupportsOutline()) continue;

		FRenderCommand MaskCmd = BaseCmd;
		if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Billboard)
		{
			MaskCmd.Type = ERenderCommandType::BillboardSelectionMask;
			MaskCmd.Constants.Billboard.Texture = static_cast<UBillboardComponent*>(primitiveComponent)->GetTexture();
		}
		else
		{
			MaskCmd.Type = ERenderCommandType::SelectionMask;
		}
		RenderBus.AddCommand(ERenderPass::SelectionMask, MaskCmd);
		bHasSelectionMask = true;

	}

	for (UActorComponent* Component : Actor->GetComponents())
	{
		const ULightComponent* LightComponent = Cast<ULightComponent>(Component);
		if (LightComponent == nullptr || !LightComponent->IsVisible() || LineBatcher == nullptr)
		{
			continue;
		}

		switch (LightComponent->GetLightType())
		{
		case ELightType::LightType_Directional:
		{
			const UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(LightComponent);
			LineBatcher->AddDirectionalLight(Light->GetWorldLocation(), Light->GetForwardVector(), Light->GetRightVector(), Light->GetLightColor().ToVector4());
			break;
		}
		case ELightType::LightType_AmbientLight:
			break;
		case ELightType::LightType_Point:
		{
			const UPointLightComponent* Light = Cast<UPointLightComponent>(LightComponent);
			const FColor LineColor = FColor(255, 220, 100);
			LineBatcher->AddSphere(Light->GetWorldLocation(), Light->GetAttenuationRadius(), Light->GetRightVector(), Light->GetUpVector(), LineColor);
			break;
		}
		case ELightType::LightType_Spot:
		{
			const USpotLightComponent* Light = Cast<USpotLightComponent>(LightComponent);
			LineBatcher->AddSpotLight(
				Light->GetWorldLocation(),
				Light->GetUpVector() * -1.0f,
				Light->GetRightVector() * -1.0f,
				Light->GetAttenuationRadius(),
				Light->GetInnerConeAngle(),
				Light->GetOuterConeAngle()
			);
			break;
		}
		}
	}

	return bHasSelectionMask;
}

void FOverlayRenderCollector::CollectDebugBoundsFromActor(
	AActor* Actor,
	const FShowFlags& ShowFlags,
	EViewMode ViewMode,
	FRenderBus& RenderBus,
	FLineBatcher* LineBatcher,
	std::unordered_set<int32>& SeenBVHNodeIndices)
{
	(void)ViewMode;

	if (Actor == nullptr || LineBatcher == nullptr)
	{
		return;
	}

	for (UPrimitiveComponent* PrimitiveComponent : Actor->GetPrimitiveComponents())
	{
		if (PrimitiveComponent == nullptr || !PrimitiveComponent->IsVisible() || PrimitiveComponent->IsHiddenInEditor())
		{
			continue;
		}

		if (PrimitiveComponent->IsEditorOnly())
		{
			UWorld* World = Actor->GetFocusedWorld();
			if (World && World->GetWorldType() != EWorldType::Editor)
			{
				continue;
			}
		}

		const bool bDrawBounds = PrimitiveComponent->ShouldDrawDebugBounds(ShowFlags.bBoundingVolume);
		if (bDrawBounds)
		{
			if (!DrawShapeComponent(PrimitiveComponent, LineBatcher))
			{
				LineBatcher->AddAABB(BuildRenderAABB(PrimitiveComponent, RenderBus), FColor::White());
			}
		}

		CollectBVHInternalNodeAABBs(PrimitiveComponent, ShowFlags, RenderBus, LineBatcher, SeenBVHNodeIndices);
	}

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (const UAudioComponent* AudioComponent = Cast<UAudioComponent>(Component))
		{
			const bool bGlobalAudioComponentRange = ShowFlags.bAudioRange && ShowFlags.bAudioComponentRange;
			if (AudioComponent->ShouldDrawAudioRange(bGlobalAudioComponentRange))
			{
				DrawAudioComponentRange(AudioComponent, LineBatcher);
			}
		}

		if (const UAudioZoneComponent* AudioZoneComponent = Cast<UAudioZoneComponent>(Component))
		{
			const bool bGlobalAudioZoneRange = ShowFlags.bAudioRange && ShowFlags.bAudioZoneRange;
			if (AudioZoneComponent->ShouldDrawAudioRange(bGlobalAudioZoneRange))
			{
				DrawAudioZoneRange(AudioZoneComponent, LineBatcher);
			}
		}
	}
}

void FOverlayRenderCollector::CollectBVHInternalNodeAABBs(
	UPrimitiveComponent* PrimitiveComponent,
	const FShowFlags& ShowFlags,
	FRenderBus& RenderBus,
	FLineBatcher* LineBatcher,
	std::unordered_set<int32>& SeenNodeIndices)
{
	if (PrimitiveComponent == nullptr || LineBatcher == nullptr ||
		!PrimitiveComponent->ShouldDrawDebugBounds(ShowFlags.bBoundingVolume) || !ShowFlags.bBVHBoundingVolume)
	{
		return;
	}

	AActor* Owner = PrimitiveComponent->GetOwner();
	UWorld* World = Owner ? Owner->GetFocusedWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	const FWorldSpatialIndex& SpatialIndex = World->GetSpatialIndex();
	const int32 ObjectIndex = SpatialIndex.FindObjectIndex(PrimitiveComponent);
	if (ObjectIndex == FBVH::INDEX_NONE)
	{
		return;
	}

	const FBVH& BVH = SpatialIndex.GetBVH();
	const TArray<int32>& ObjectToLeafNode = BVH.GetObjectToLeafNode();
	if (ObjectIndex < 0 || ObjectIndex >= static_cast<int32>(ObjectToLeafNode.size()))
	{
		return;
	}

	const int32 LeafNodeIndex = ObjectToLeafNode[ObjectIndex];
	if (LeafNodeIndex == FBVH::INDEX_NONE)
	{
		return;
	}

	const TArray<FBVH::FNode>& Nodes = BVH.GetNodes();
	if (LeafNodeIndex < 0 || LeafNodeIndex >= static_cast<int32>(Nodes.size()))
	{
		return;
	}

	TArray<int32> PathToRoot;
	PathToRoot.reserve(16);

	int32 CurrentNodeIndex = Nodes[LeafNodeIndex].Parent;
	while (CurrentNodeIndex != FBVH::INDEX_NONE)
	{
		if (CurrentNodeIndex < 0 || CurrentNodeIndex >= static_cast<int32>(Nodes.size()))
		{
			break;
		}

		PathToRoot.push_back(CurrentNodeIndex);
		CurrentNodeIndex = Nodes[CurrentNodeIndex].Parent;
	}

	for (int32 PathIndex = 0; PathIndex < static_cast<int32>(PathToRoot.size()); ++PathIndex)
	{
		const int32 NodeIndex = PathToRoot[PathIndex];
		if (!SeenNodeIndices.insert(NodeIndex).second)
		{
			continue;
		}

		const FBVH::FNode& Node = Nodes[NodeIndex];
		if (Node.IsLeaf())
		{
			continue;
		}

		const FColor Color = MakeBVHInternalNodeColor(PathIndex, static_cast<int32>(PathToRoot.size()));
		LineBatcher->AddAABB(Node.Bounds, Color);
	}
}
