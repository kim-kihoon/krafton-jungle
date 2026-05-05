#include "PrimitiveRenderCollector.h"

#include "Component/BillboardComponent.h"
#include "Component/DecalComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/SkyAtmosphereComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/Light/LightComponent.h"
#include "Core/ResourceManager.h"
#include "Engine/Asset/StaticMesh.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Geometry/OBB.h"
#include "Render/LineBatcher.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Runtime/Stats/ScopeCycleCounter.h"
#include <algorithm>
#include <cmath>

namespace
{
	FMatrix MakeViewBillboardMatrix(const UPrimitiveComponent* Primitive, const FRenderBus& RenderBus)
	{
		const FMatrix WorldMatrix = Primitive->GetWorldMatrix();
		return UBillboardComponent::MakeBillboardWorldMatrix(
			WorldMatrix.GetOrigin(),
			WorldMatrix.GetScaleVector(),
			RenderBus.GetCameraForward(),
			RenderBus.GetCameraRight(),
			RenderBus.GetCameraUp());
	}

	int32 SelectLODLevel(const FVector& CameraPos, const FAABB& Bounds, const FMatrix& ProjMatrix, int32 ValidLODCount)
	{
		bool IsOrthoGraphic = (std::abs(ProjMatrix.M[3][3] - 1.0f) < 1e-4f);
		if (ValidLODCount <= 1 || IsOrthoGraphic) return 0;

		const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
		const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;
		const float SphereRadius = std::sqrt(Extent.X * Extent.X + Extent.Y * Extent.Y + Extent.Z * Extent.Z);

		const FVector Diff = Center - CameraPos;
		const float Dist = std::sqrt(Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z);

		if (Dist <= 1e-4f) return 0;

		const float ProjectedRadius = (SphereRadius / Dist) * ProjMatrix.M[2][1];
		const float ScreenCoverage = ProjectedRadius;

		static constexpr float Thresholds[] = { 0.06f, 0.03f, 0.02f, 0.008f };
		static constexpr int32 ThresholdCount = static_cast<int32>(sizeof(Thresholds) / sizeof(Thresholds[0]));

		const int32 MaxLOD = ValidLODCount - 1;
		for (int32 LOD = 0; LOD < MaxLOD; ++LOD)
		{
			float Threshold = (LOD < ThresholdCount) ? Thresholds[LOD] : 0.0f;
			if (ScreenCoverage >= Threshold)
				return LOD;
		}

		return MaxLOD;
	}

	// ShapeComponent에 충돌 정보가 저장되므로 Actor 단위로 ShapeComponent를 순회해서 확인한다.
	bool ShouldHighlightDebugCollision(const UPrimitiveComponent* Primitive, EViewMode ViewMode)
	{
		if (ViewMode != EViewMode::DebugCollision || Primitive == nullptr)
			return false;

		const AActor* Owner = Primitive->GetOwner();
		if (Owner == nullptr)
			return false;

		for (UPrimitiveComponent* Comp : Owner->GetPrimitiveComponents())
		{
			if (Cast<UShapeComponent>(Comp) && (Comp->GetOverlapInfos().size() || Comp->GetBlockingInfos().size()))
				return true;
		}
		return false;
	}
}

void FPrimitiveRenderCollector::CollectFromActor(
	AActor* Actor,
	const FShowFlags& ShowFlags,
	EViewMode ViewMode,
	FRenderBus& RenderBus,
	EWorldType WorldType,
	FRenderCollectionStats& LastStats,
	FLineBatcher* LineBatcher)
{
	if (Actor == nullptr || !Actor->IsVisible()) return;

	for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
	{
		CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, WorldType, LastStats, LineBatcher);
	}
}

void FPrimitiveRenderCollector::CollectFromComponent(
	UPrimitiveComponent* Primitive,
	const FShowFlags& ShowFlags,
	EViewMode ViewMode,
	FRenderBus& RenderBus,
	EWorldType WorldType,
	FRenderCollectionStats& LastStats,
	FLineBatcher* LineBatcher)
{
	if (Primitive == nullptr || MeshBufferManager == nullptr) return;
	if (!Primitive->IsVisible()) return;
	if (Primitive->IsEditorOnly() && WorldType != EWorldType::Editor) return;

	EPrimitiveType PrimType = Primitive->GetPrimitiveType();
	if (PrimType == EPrimitiveType::EPT_Billboard && WorldType != EWorldType::Editor)
	{
		return;
	}

	static const FMaterial EngineDefaultMaterial{};

	switch (PrimType)
	{
	case EPrimitiveType::EPT_StaticMesh:
	{
		if (!ShowFlags.bPrimitives) return;

		UStaticMeshComponent* StaticMeshComp = static_cast<UStaticMeshComponent*>(Primitive);
		const UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();

		if (!StaticMesh || !StaticMesh->HasValidMeshData()) return;

		FVector CameraPos = RenderBus.GetCameraPosition();
		FMatrix ProjMatrix = RenderBus.GetProj();
		FAABB Bounds = StaticMeshComp->GetWorldAABB();
		const int32 ValidLODCount = StaticMesh->GetValidLODCount();

		int32 SelectedLOD = 0;
		if (ShowFlags.bEnableLOD)
		{
			SelectedLOD = SelectLODLevel(CameraPos, Bounds, ProjMatrix, ValidLODCount);
		}

		FMeshBuffer* MeshBuffer = MeshBufferManager->GetStaticMeshBuffer(StaticMesh, SelectedLOD);
		if (!MeshBuffer) return;

		const FStaticMesh* MeshData = StaticMesh->GetMeshData(SelectedLOD);
		const TArray<FStaticMeshSection>& Sections = MeshData->Sections;
		const bool bDebugCollisionHighlight = ShouldHighlightDebugCollision(Primitive, ViewMode);
		const FVector4 PrimitiveColor = FColor::White().ToVector4();

		for (int32 SectionIdx = 0; SectionIdx < static_cast<int32>(Sections.size()); ++SectionIdx)
		{
			const FStaticMeshSection& Section = Sections[SectionIdx];
			UMaterialInterface* Material = bDebugCollisionHighlight
				? FResourceManager::Get().GetMaterial("DefaultRed")
				: Cast<UMaterialInterface>(StaticMeshComp->GetMaterial(SectionIdx));

			if (Material == nullptr)
			{
				Material = FResourceManager::Get().GetMaterial("DefaultWhite");
				if (Material == nullptr)
				{
					continue;
				}
			}

			FRenderCommand Cmd = {};
			Cmd.PerObjectConstants = FPerObjectConstants{ Primitive->GetWorldMatrix(), PrimitiveColor };
			Cmd.Type = ERenderCommandType::StaticMesh;
			Cmd.MeshBuffer = MeshBuffer;

			Cmd.SectionIndexStart = Section.StartIndex;
			Cmd.SectionIndexCount = Section.IndexCount;
			Cmd.Material = Material;

			RenderBus.AddCommand(ERenderPass::Opaque, Cmd);

			if (Material->GetEffectiveLightingModel() == ELightingModel::Toon)
			{
				FRenderCommand OutlineCmd = {};
				OutlineCmd.Type = ERenderCommandType::ToonOutline;
				OutlineCmd.MeshBuffer = MeshBuffer;
				OutlineCmd.PerObjectConstants = FPerObjectConstants{
					Primitive->GetWorldMatrix()
				};
				OutlineCmd.SectionIndexStart = Section.StartIndex;
				OutlineCmd.SectionIndexCount = Section.IndexCount;
				OutlineCmd.Material = Material;

				RenderBus.AddCommand(ERenderPass::ToonOutline, OutlineCmd);
			}
		}

		break;
	}

	case EPrimitiveType::EPT_Text:
	{
		if (!ShowFlags.bBillboardText) return;

		UTextRenderComponent* TextComp = static_cast<UTextRenderComponent*>(Primitive);
		const FFontResource* Font = TextComp->GetFont();
		if (!Font || !Font->IsLoaded()) return;

		const FString& Text = TextComp->GetText();
		if (Text.empty()) return;

		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Font;
		Cmd.PerObjectConstants = FPerObjectConstants{ TextComp->GetWorldMatrix(), TextComp->GetColor() };
		Cmd.Constants.Font.Text = &Text;
		Cmd.Constants.Font.Font = Font;
		Cmd.Constants.Font.Scale = TextComp->GetFontSize();

		RenderBus.AddCommand(ERenderPass::Font, Cmd);
		break;
	}

	case EPrimitiveType::EPT_SubUV:
	{
		USubUVComponent* SubUVComp = static_cast<USubUVComponent*>(Primitive);
		const FParticleResource* Particle = SubUVComp->GetParticle();
		if (!Particle || !Particle->IsLoaded()) return;

		FRenderCommand Cmd = {};
		Cmd.PerObjectConstants = FPerObjectConstants{
			MakeViewBillboardMatrix(Primitive, RenderBus),
			SubUVComp->GetTintColor()
		};
		Cmd.Type = ERenderCommandType::SubUV;
		Cmd.Constants.SubUV.Particle = Particle;
		Cmd.Constants.SubUV.FrameIndex = SubUVComp->GetFrameIndex();
		Cmd.Constants.SubUV.Width = SubUVComp->GetWidth();
		Cmd.Constants.SubUV.Height = SubUVComp->GetHeight();
		Cmd.Constants.SubUV.TintColor = SubUVComp->GetTintColor();

		RenderBus.AddCommand(ERenderPass::SubUV, Cmd);
		break;
	}

	case EPrimitiveType::EPT_Billboard:
	{
		UBillboardComponent* BillboardComp = static_cast<UBillboardComponent*>(Primitive);
		UTexture* Texture = BillboardComp->GetTexture();

		UMaterial* BillboardMat = FResourceManager::Get().GetMaterial("BillboardMat");
		BillboardMat->DepthStencilType = EDepthStencilType::Default;
		BillboardMat->BlendType = EBlendType::AlphaBlend;
		BillboardMat->RasterizerType = ERasterizerType::SolidNoCull;
		BillboardMat->SamplerType = ESamplerType::EST_Linear;

		const FMatrix BillboardMatrix = UBillboardComponent::MakeBillboardWorldMatrix(
			BillboardComp->GetWorldLocation(),
			FVector(0.01f, BillboardComp->GetWidth(), BillboardComp->GetHeight()),
			RenderBus.GetCameraForward(),
			RenderBus.GetCameraRight(),
			RenderBus.GetCameraUp());

		FVector4 LightColor = FColor::White().ToVector4();
		if (ULightComponent* LightComponent = Cast<ULightComponent>(BillboardComp->GetParent()))
		{
			LightColor = LightComponent->GetLightColor().ToVector4();
		}

		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Billboard;
		Cmd.MeshBuffer = &MeshBufferManager->GetMeshBuffer(EPrimitiveType::EPT_Billboard);
		Cmd.PerObjectConstants = FPerObjectConstants{ BillboardMatrix, LightColor };
		Cmd.Material = BillboardMat;
		Cmd.Constants.Billboard.Texture = Texture;

		RenderBus.AddCommand(ERenderPass::Billboard, Cmd);
		break;
	}

	case EPrimitiveType::EPT_Decal:
	{
		if (!ShowFlags.bDecals) return;

		FScopeCycleCounter RenderDecalScope({});

		UDecalComponent* DecalComp = static_cast<UDecalComponent*>(Primitive);
		UMaterialInterface* Material = Cast<UMaterialInterface>(DecalComp->GetMaterial());

		UWorld* World = DecalComp->GetOwner() ? DecalComp->GetOwner()->GetFocusedWorld() : nullptr;
		if (World == nullptr) return;

		FOBB DecalOBB = FOBB::FromAABB(DecalComp->GetWorldAABB(), DecalComp->GetWorldMatrix());

		TArray<UPrimitiveComponent*> VisiblePrimitiveScratch;
		World->GetSpatialIndex().OBBQueryPrimitives(DecalOBB, VisiblePrimitiveScratch, OBBQueryScratch);

		for (UPrimitiveComponent* Prim : VisiblePrimitiveScratch)
		{
			if (Prim->GetPrimitiveType() != EPrimitiveType::EPT_StaticMesh) continue;

			UStaticMeshComponent* StaticMeshComp = static_cast<UStaticMeshComponent*>(Prim);
			const UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();

			if (!StaticMesh || !StaticMesh->HasValidMeshData()) continue;

			FVector CameraPos = RenderBus.GetCameraPosition();
			FMatrix ProjMatrix = RenderBus.GetProj();
			FAABB Bounds = StaticMeshComp->GetWorldAABB();
			const int32 ValidLODCount = StaticMesh->GetValidLODCount();

			int32 SelectedLOD = 0;
			if (ShowFlags.bEnableLOD)
			{
				SelectedLOD = SelectLODLevel(CameraPos, Bounds, ProjMatrix, ValidLODCount);
			}

			FMeshBuffer* MeshBuffer = MeshBufferManager->GetStaticMeshBuffer(StaticMesh, SelectedLOD);
			if (!MeshBuffer) return;

			const FStaticMesh* MeshData = StaticMesh->GetMeshData(SelectedLOD);
			const TArray<FStaticMeshSection>& Sections = MeshData->Sections;

			for (int32 SectionIdx = 0; SectionIdx < static_cast<int32>(Sections.size()); ++SectionIdx)
			{
				const FStaticMeshSection& Section = Sections[SectionIdx];

				FRenderCommand Cmd = {};
				Cmd.Type = ERenderCommandType::Decal;
				Cmd.PerObjectConstants = FPerObjectConstants{ Prim->GetWorldMatrix(), DecalComp->GetDecalColor().ToVector4() };
				Cmd.MeshBuffer = MeshBuffer;

				Cmd.SectionIndexStart = Section.StartIndex;
				Cmd.SectionIndexCount = Section.IndexCount;

				Cmd.Material = Material;
				Cmd.Constants.Decal.InvDecalWorld = DecalComp->GetDecalMatrix().GetInverse();
				const bool bCleanCompleteEffectActive = DecalComp->IsCleanCompleteEffectActive();
				Cmd.Constants.Decal.MaskInfluence = bCleanCompleteEffectActive ? 0.0f : 1.0f;
				Cmd.Constants.Decal.WhiteOverride = bCleanCompleteEffectActive ? 1.0f : 0.0f;

				Cmd.MaskSRV = DecalComp->GetMaskSRV();

				RenderBus.AddCommand(ERenderPass::Decal, Cmd);
			}
		}

		if (WorldType == EWorldType::Editor && LineBatcher != nullptr && DecalComp->ShouldDrawDebugBounds(ShowFlags.bBoundingVolume))
		{
			LineBatcher->AddOBB(DecalOBB, FColor::Green());
		}

		LastStats.Decal.TotalDecalCount += 1;
		LastStats.Decal.CollectTimeMS += static_cast<int32>(RenderDecalScope.Finish());
		break;
	}

	case EPrimitiveType::EPT_FOG:
	{
		if (!ShowFlags.bFog)
			return;
		UHeightFogComponent* HeightFogComp = static_cast<UHeightFogComponent*>(Primitive);

		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Primitive;
		Cmd.Constants.Fog.FogDensity = HeightFogComp->GetFogDensity();
		Cmd.Constants.Fog.FogColor = HeightFogComp->GetFogInscatteringColor();
		Cmd.Constants.Fog.HeightFalloff = HeightFogComp->GetHeightFalloff();
		Cmd.Constants.Fog.FogHeight = HeightFogComp->GetFogHeight();
		Cmd.Constants.Fog.FogStartDistance = HeightFogComp->GetFogStartDistance();
		Cmd.Constants.Fog.FogMaxOpacity = HeightFogComp->GetFogMaxOpacity();
		Cmd.Constants.Fog.FogCutoffDistance = HeightFogComp->GetFogCutoffDistance();

		RenderBus.AddCommand(ERenderPass::Fog, Cmd);
		break;
	}
	case EPrimitiveType::EPT_SKY:
	{
		if (!RenderBus.GetCommands(ERenderPass::Sky).empty())
		{
			return;
		}

		USkyAtmosphereComponent* SkyComponent = static_cast<USkyAtmosphereComponent*>(Primitive);
		SkyComponent->RefreshSkyStateFromWorld();

		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Sky;
		SkyComponent->FillSkyConstants(RenderBus, Cmd.Constants.Sky);
		RenderBus.AddCommand(ERenderPass::Sky, Cmd);
		break;
	}
	default:
		if (PrimType == EPrimitiveType::EPT_TransGizmo || PrimType == EPrimitiveType::EPT_RotGizmo || PrimType == EPrimitiveType::EPT_ScaleGizmo)
		{
			return;
		}
		return;
	}
}
