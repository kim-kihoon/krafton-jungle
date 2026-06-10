#include "DrawCommandBuilder.h"

#include "Resource/ResourceManager.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Types/FogParams.h"
#include "Render/Types/LODContext.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Proxy/TextRenderSceneProxy.h"
#include "Render/Proxy/DecalSceneProxy.h"
#include "Render/Proxy/ShapeSceneProxy.h"
#include "Render/Proxy/BoneDebugSceneProxy.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Render/Scene/FScene.h"
#include "Render/Types/RenderConstants.h"
#include "Render/Types/ViewModeUtils.h"
#include "Render/RenderPass/PassRenderStateTable.h"
#include "Render/Pipeline/RenderCollector.h"
#include "Materials/Material.h"
#include "Texture/Texture2D.h"
#include "Engine/Runtime/Engine.h"
#include "Profiling/Time/Timer.h"

// UpdateProxyLOD defined in RenderCollector.cpp (shared)
extern void UpdateProxyLOD(FPrimitiveSceneProxy* Proxy, const FLODUpdateContext& LODCtx);

// ============================================================
// Create / Release
// ============================================================

void FDrawCommandBuilder::Create(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, const FPassRenderStateTable* InPassRenderStateTable)
{
	CachedDevice = InDevice;
	CachedContext = InContext;
	PassRenderStateTable = InPassRenderStateTable;

	EditorLines.Create(InDevice);
	GridLines.Create(InDevice);
	DebugBoneLines.Create(InDevice);
	FontGeometry.Create(InDevice);

	FogCB.Create(InDevice, sizeof(FFogConstants), "FogCB");
	OutlineCB.Create(InDevice, sizeof(FOutlinePostProcessConstants), "OutlineCB");
	SceneDepthCB.Create(InDevice, sizeof(FSceneDepthPConstants), "SceneDepthCB");
	DoFCB.Create(InDevice, sizeof(FDoFConstants), "DoFCB");
	FXAACB.Create(InDevice, sizeof(FFXAAConstants), "FXAACB");
	GammaCorrectionCB.Create(InDevice, sizeof(FGammaCorrectionConstants), "GammaCorrectionCB");

	CameraFadeCB.Create(InDevice, sizeof(FCameraFadeConstants), "CameraFadeCB");
	CameraVignetteCB.Create(InDevice, sizeof(FCameraVignetteConstants), "CameraVignetteCB");
	CameraLetterboxCB.Create(InDevice, sizeof(FCameraLetterboxConstants), "CameraLetterboxCB");
	MeshScalarOverlayCB.Create(InDevice, sizeof(FMeshScalarOverlayConstants), "MeshScalarOverlayCB");
	MeshScalarOverlayWireCB.Create(InDevice, sizeof(FMeshScalarOverlayConstants), "MeshScalarOverlayWireCB");
}

void FDrawCommandBuilder::Release()
{
	EditorLines.Release();
	GridLines.Release();
	DebugBoneLines.Release();
	FontGeometry.Release();

	for (auto& Pair : PerSceneObjectCBPool)
	{
		for (FConstantBuffer& CB : Pair.second)
		{
			CB.Release();
		}
		Pair.second.clear();
	}
	PerSceneObjectCBPool.clear();

	FogCB.Release();
	OutlineCB.Release();
	SceneDepthCB.Release();
	DoFCB.Release();
	FXAACB.Release();
	GammaCorrectionCB.Release();
	
	CameraFadeCB.Release();
	CameraVignetteCB.Release();
	CameraLetterboxCB.Release();
	MeshScalarOverlayCB.Release();
	MeshScalarOverlayWireCB.Release();
}

// ============================================================
// BeginCollect — DrawCommandList + 동적 지오메트리 초기화
// ============================================================
void FDrawCommandBuilder::BeginCollect(const FFrameContext& Frame)
{
	DrawCommandList.Reset();
	CollectViewMode = Frame.RenderOptions.ViewMode;
	bCollectWeightBoneHeatMap = Frame.RenderOptions.bWeightBoneHeatMap;
	CollectWeightBoneHeatMapBoneIndex = Frame.RenderOptions.WeightBoneHeatMapBoneIndex;
	CollectWeightBoneHeatMapOverlayAlpha = Frame.RenderOptions.WeightBoneHeatMapOverlayAlpha;
	bCollectClothMaxDistanceOverlay = Frame.RenderOptions.bClothMaxDistanceOverlay;
	CollectClothOverlayLODIndex = Frame.RenderOptions.ClothOverlayLODIndex;
	CollectClothOverlayIndex = Frame.RenderOptions.ClothOverlayIndex;
	CollectClothMaxDistanceOverlayAlpha = Frame.RenderOptions.ClothMaxDistanceOverlayAlpha;
	bCollectEditorIcons = Frame.RenderOptions.ShowFlags.bEditorIcons;
	bCollectSelectionOutline = Frame.RenderOptions.ShowFlags.bSelectionOutline;
	CollectCameraPosition = Frame.CameraPosition;

	bHasSelectionMaskCommands = false;

	// 동적 지오메트리 초기화
	EditorLines.Clear();
	GridLines.Clear();
	DebugBoneLines.Clear();
	FontGeometry.Clear();
	FontGeometry.ClearScreen();

	if (const FFontResource* FontRes = FResourceManager::Get().FindFont(FName("Default")))
		FontGeometry.EnsureCharInfoMap(FontRes);
}

static EUberLitDefines::ELightingModel GetLightingModelForViewMode(EViewMode ViewMode)
{
	switch (ViewMode)
	{
	case EViewMode::Unlit:       return EUberLitDefines::ELightingModel::Unlit;
	case EViewMode::Lit_Gouraud: return EUberLitDefines::ELightingModel::Gouraud;
	case EViewMode::Lit_Lambert: return EUberLitDefines::ELightingModel::Lambert;
	case EViewMode::Lit_Phong:
	case EViewMode::LightCulling:
	default:                     return EUberLitDefines::ELightingModel::Phong;
	}
}

static FShader* GetUberTransparentShader(EViewMode ViewMode, EUberLitDefines::EVertexFactory VertexFactory)
{
	const char* VSEntry = VertexFactory == EUberLitDefines::EVertexFactory::SkeletalMesh
		? EUberLitDefines::EntryPoint::SkeletalMeshVS
		: EUberLitDefines::EntryPoint::StaticMeshVS;
	const EUberLitDefines::ELightingModel LightingModel = GetLightingModelForViewMode(ViewMode);
	const D3D_SHADER_MACRO* Defines = EUberLitDefines::GetDefines(LightingModel, VertexFactory);
	return FShaderManager::Get().GetOrCreate(
		FShaderKey(EShaderPath::UberTransparent, Defines, VSEntry, EUberLitDefines::EntryPoint::PS));
}

// ============================================================
// SelectEffectiveShader — ViewMode에 따른 UberLit 셰이더 변형 선택
// ============================================================
FShader* FDrawCommandBuilder::SelectEffectiveShader(FShader* ProxyShader, EViewMode ViewMode, bool bUseSkeletalVertexFactory)
{
	if (ProxyShader != FShaderManager::Get().GetOrCreate(EShaderPath::UberLit))
		return ProxyShader;

	const EUberLitDefines::EVertexFactory VertexFactory = bUseSkeletalVertexFactory
		? EUberLitDefines::EVertexFactory::SkeletalMesh
		: EUberLitDefines::EVertexFactory::StaticMesh;

	switch (ViewMode)
	{
	case EViewMode::Unlit:
		return FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Unlit, VertexFactory, EShaderErrorMode::Notification);
	case EViewMode::Lit_Gouraud:
		return FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Gouraud, VertexFactory, EShaderErrorMode::Notification);
	case EViewMode::Lit_Lambert:
		return FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Lambert, VertexFactory, EShaderErrorMode::Notification);
	case EViewMode::Lit_Phong:
	case EViewMode::LightCulling:
		return FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Phong, VertexFactory, EShaderErrorMode::Notification);
	default:
		return bUseSkeletalVertexFactory
			? FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Default, VertexFactory, EShaderErrorMode::Notification)
			: ProxyShader;
	}
}

// ============================================================
// ResolveSectionShader — shader-agnostic 셰이더 도출
//   graph/custom override 우선 → particle VF 전용 → surface scene/Transparent shader.
//   머티리얼은 셰이더를 모르고, 엔진이 (Domain × VertexFactory × Pass × ViewMode)로 고른다.
// ============================================================
FShader* FDrawCommandBuilder::ResolveSectionShader(UMaterial* Mat, EVertexFactoryType VFType, ERenderPass SecPass, EViewMode ViewMode, bool bGPUSkinning)
{
	const bool bDerivableSurfaceTransparent =
		Mat &&
		Mat->GetSourceKind() != EMaterialSourceKind::Graph &&
		SecPass == ERenderPass::Transparent &&
		Mat->GetShaderPathForSerialize() == EShaderPath::UberLit;

	// 1. 빌보드 컴포넌트는 UI용 텍스처 쿼드라 조명 셰이더를 타지 않는다.
	if (VFType == EVertexFactoryType::Billboard)
	{
		return FShaderManager::Get().GetOrCreate(EShaderPath::Billboard);
	}

    // 2. Graph material first-class path. Runtime-compiled graph materials carry their
    //    generated shader as the material template/custom shader, and must beat the
    //    generic particle/default shader fallback.
    if (Mat && Mat->GetSourceKind() == EMaterialSourceKind::Graph)
    {
        if (Mat->HasCustomShader()) return Mat->GetCustomShader();
        if (FShader* GraphShader = Mat->GetShader()) return GraphShader;
    }

    // 3. custom override 강제 (CreateTransient: Gizmo/Decal/Text/SubUV, 비표준 셰이더 .mat)
	if (Mat && Mat->HasCustomShader() && !bDerivableSurfaceTransparent)
		return Mat->GetCustomShader();

    // 4. 파티클 정점 팩토리 → 전용 셰이더 (FParticleVertexFactory 가 만들던 것과 동일 키)
	switch (VFType)
	{
	case EVertexFactoryType::ParticleSprite: return FShaderManager::Get().GetOrCreate(EShaderPath::ParticleSprite);
	case EVertexFactoryType::ParticleMesh:   return FShaderManager::Get().GetOrCreate(EShaderPath::ParticleMesh);
	case EVertexFactoryType::ParticleBeam:
	case EVertexFactoryType::ParticleRibbon: return FShaderManager::Get().GetOrCreate(EShaderPath::ParticleBeamTrail);
	default: break;
	}

    // 5. Surface 메시 → pass별 scene shader. 셰이더 정점 팩토리는 bGPUSkinning 으로 결정
	//    (CPU 스키닝은 static-layout VS).
	const EUberLitDefines::EVertexFactory UVF = bGPUSkinning
		? EUberLitDefines::EVertexFactory::SkeletalMesh
		: EUberLitDefines::EVertexFactory::StaticMesh;

	if (SecPass == ERenderPass::Transparent)
		return GetUberTransparentShader(ViewMode, UVF);

	switch (ViewMode)
	{
	case EViewMode::Unlit:        return FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Unlit,   UVF, EShaderErrorMode::Notification);
	case EViewMode::Lit_Gouraud:  return FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Gouraud, UVF, EShaderErrorMode::Notification);
	case EViewMode::Lit_Lambert:  return FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Lambert, UVF, EShaderErrorMode::Notification);
	case EViewMode::Lit_Phong:
	case EViewMode::LightCulling: return FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Phong,   UVF, EShaderErrorMode::Notification);
	default:
		return bGPUSkinning
			? FShaderManager::Get().GetOrCreateUberLitPermutation(EUberLitDefines::ELightingModel::Default, UVF, EShaderErrorMode::Notification)
			: FShaderManager::Get().GetOrCreate(EShaderPath::UberLit);  // base UberLit (기존 ProxyShader 통과와 동일)
	}
}

// ============================================================
// ApplyMaterialRenderState — Material 렌더 상태 오버라이드 (Wireframe 우선)
// ============================================================
void FDrawCommandBuilder::BuildCameraPostProcessMaterialCommand(const FFrameContext& Frame, const FDrawCommandRenderState& BaseState)
{
	UMaterial* Material = Frame.CameraPostProcessMaterial;
	if (!IsValid(Material) || Material->GetDomain() != EMaterialDomain::PostProcess)
	{
		return;
	}
	if (!Frame.SceneColorCopyTexture || !Frame.ViewportRenderTexture || !Frame.SceneColorCopySRV)
	{
		return;
	}

	FShader* Shader = Material->HasCustomShader() ? Material->GetCustomShader() : Material->GetShader();
	if (!Shader || !Shader->IsValid())
	{
		return;
	}

	if (GEngine)
	{
		if (const FTimer* Timer = GEngine->GetTimer())
		{
			Material->SetScalarParameter(FString("Time"), static_cast<float>(Timer->GetTotalTime()));
		}
	}

	Material->FlushDirtyBuffers(CachedDevice, CachedContext);

	FDrawCommand& Cmd = DrawCommandList.AddCommand();
	Cmd.InitFullscreenTriangle(Shader, ERenderPass::PostProcess, BaseState);
	ApplyMaterialRenderState(Cmd.RenderState, Material, BaseState);
	Cmd.Bindings.PerShaderCB[0] = Material->GetGPUBufferBySlot(ECBSlot::PerShader0);
	Cmd.Bindings.PerShaderCB[1] = Material->GetGPUBufferBySlot(ECBSlot::PerShader1);

	const ID3D11ShaderResourceView* const* MaterialSRVs = Material->GetCachedSRVs();
	for (int Slot = 0; Slot < (int)EMaterialTextureSlot::Max; ++Slot)
	{
		Cmd.Bindings.SRVs[Slot] = const_cast<ID3D11ShaderResourceView*>(MaterialSRVs[Slot]);
	}

	Cmd.SortKey = (static_cast<uint64>(ERenderPass::PostProcess) & 0x1F) << 59;
}

void FDrawCommandBuilder::ApplyMaterialRenderState(FDrawCommandRenderState& OutState, const UMaterial* Mat, const FDrawCommandRenderState& BaseState)
{
	OutState.Blend = Mat->GetBlendState();
	OutState.DepthStencil = Mat->GetDepthStencilState();
	if (BaseState.Rasterizer != ERasterizerState::WireFrame)
		OutState.Rasterizer = Mat->GetRasterizerState();
}

// 섹션의 효과 패스 — PassOverride(MAX=없음) 우선, 아니면 머티리얼 GetRenderPass(), 둘 다 없으면 Opaque.
static UMaterial* GetValidSectionMaterial(const FMeshSectionDraw& Section)
{
	return IsValid(Section.Material) ? Section.Material : nullptr;
}

static ERenderPass SectionRenderPass(const FMeshSectionDraw& Section)
{
	if (Section.PassOverride != ERenderPass::MAX) return Section.PassOverride;
	if (UMaterial* Material = GetValidSectionMaterial(Section)) return Material->GetRenderPass();
	return ERenderPass::Opaque;
}

static bool IsGizmoPass(ERenderPass Pass)
{
	return Pass == ERenderPass::GizmoOuter || Pass == ERenderPass::GizmoInner;
}

static bool ProxyHasGizmoPass(const FPrimitiveSceneProxy& Proxy)
{
	if (IsGizmoPass(Proxy.GetRenderPass()))
		return true;

	for (const FMeshSectionDraw& Section : Proxy.GetSectionDraws())
	{
		if (Section.IndexCount == 0) continue;
		if (IsGizmoPass(SectionRenderPass(Section)))
			return true;
	}

	return false;
}

static bool IsEditorHelperProxy(const FPrimitiveSceneProxy& Proxy)
{
	return Proxy.HasProxyFlag(EPrimitiveProxyFlags::BoneDebug) ||
		Proxy.HasProxyFlag(EPrimitiveProxyFlags::WireShape) ||
		Proxy.HasProxyFlag(EPrimitiveProxyFlags::FontBatched);
}

static bool ShouldSuppressPassForViewMode(EViewMode ViewMode, ERenderPass Pass, bool bEditorIcons)
{
	return (!bEditorIcons && Pass == ERenderPass::EditorIcon) ||
		(ViewModeUtils::SuppressesEditorOverlays(ViewMode) && Pass == ERenderPass::EditorIcon);
}

static FShader* GetViewModeMeshShader(bool bUseSkeletalVertexFactory)
{
	const char* VSEntry = bUseSkeletalVertexFactory ? "VS_SkeletalMesh" : "VS_StaticMesh";
	return FShaderManager::Get().GetOrCreate(FShaderKey(EShaderPath::ViewModeMesh, nullptr, VSEntry, "PS"));
}

// ============================================================
// BuildCommandForProxy — Proxy → FDrawCommand 변환
// ============================================================
void FDrawCommandBuilder::BuildCommandForProxy(FScene& Scene, const FPrimitiveSceneProxy& Proxy, ERenderPass Pass)
{
	// if (!Proxy.GetMeshBuffer() || !Proxy.GetMeshBuffer()->IsValid()) return;
	ID3D11DeviceContext* Ctx = CachedContext;

	const bool bSkeletal = Proxy.HasProxyFlag(EPrimitiveProxyFlags::SkeletalMesh);
	const FSkeletalMeshSceneProxy* SkeletalProxy = bSkeletal
		? static_cast<const FSkeletalMeshSceneProxy*>(&Proxy)
		: nullptr;
	const bool bGPUSkinning = SkeletalProxy && SkeletalProxy->GetEffectiveSkinningMode() == ESkinningMode::GPU;
	const bool bBoneHeatMapOverlay =
		(Pass == ERenderPass::ViewModeMesh) &&
		!ViewModeUtils::IsPureMeshDebugViewMode(CollectViewMode) &&
		bSkeletal &&
		bCollectWeightBoneHeatMap &&
		CollectWeightBoneHeatMapBoneIndex >= 0;
	const bool bClothMaxDistanceOverlay =
		(Pass == ERenderPass::ViewModeMesh) &&
		!ViewModeUtils::IsPureMeshDebugViewMode(CollectViewMode) &&
		bSkeletal &&
		!bGPUSkinning &&
		bCollectClothMaxDistanceOverlay &&
		CollectClothOverlayLODIndex >= 0 &&
		CollectClothOverlayIndex >= 0;
	const bool bMeshScalarOverlay = bClothMaxDistanceOverlay || bBoneHeatMapOverlay;

	FDrawCommandBuffer ProxyBuffer;
	uint32 ClothOverlayFirstIndex = 0;
	uint32 ClothOverlayIndexCount = 0;
	if (bClothMaxDistanceOverlay)
	{
		if (!SkeletalProxy ||
			!SkeletalProxy->PrepareCpuClothMaxDistanceOverlayDrawBuffer(
				CachedDevice,
				Ctx,
				CollectClothOverlayLODIndex,
				CollectClothOverlayIndex,
				ProxyBuffer,
				ClothOverlayFirstIndex,
				ClothOverlayIndexCount)) return;
	}
	else if (bBoneHeatMapOverlay && !bGPUSkinning)
	{
		if (!SkeletalProxy || !SkeletalProxy->PrepareCpuBoneHeatMapDrawBuffer(CachedDevice, Ctx, CollectWeightBoneHeatMapBoneIndex, ProxyBuffer)) return;
	}
	else if (bGPUSkinning)
	{
		if (!SkeletalProxy || !SkeletalProxy->PrepareGpuSkinningDrawBuffer(CachedDevice, Ctx, ProxyBuffer)) return;
	}
	else if (!Proxy.PrepareDrawBuffer(CachedDevice, Ctx, ProxyBuffer))
	{
		return;
	}
	// ProxyBuffer가 비어 있어도 section-level BufferOverride가 있을 수 있음 (Particle 멀티 emitter).
	// → early return 제거, 루프 안에서 effective buffer 체크.

	// PassState → RenderState 변환 (Wireframe 오버라이드 포함)
	const FDrawCommandRenderState BaseRenderState = PassRenderStateTable->ToDrawCommandState(Pass, CollectViewMode);

	// Transparent depth-first 정렬용 거리² (Pass != Transparent면 SortKey에서 무시)
	const FVector& ObjPos = Proxy.GetCachedWorldPos();
	const FVector  ToCam  = CollectCameraPosition - ObjPos;
	const float    DistSq = ToCam.Dot(ToCam);

	// PerObjectCB 업데이트
	FConstantBuffer* PerObjCB = GetPerObjectCBForProxy(&Scene, Proxy);
	if (PerObjCB && Proxy.NeedsPerObjectCBUpload())
	{
		PerObjCB->Update(Ctx, &Proxy.GetPerObjectConstants(), sizeof(FPerObjectConstants));
		Proxy.ClearPerObjectCBDirty();
	}

	// SelectionMask 커맨드 존재 추적
	if (Pass == ERenderPass::SelectionMask)
		bHasSelectionMaskCommands = true;

	const bool bDepthOnly = (Pass == ERenderPass::PreDepth);
	const bool bViewModeMeshReplace = (Pass == ERenderPass::ViewModeMesh) && ViewModeUtils::IsPureMeshDebugViewMode(CollectViewMode);
	const bool bViewModeMeshOverlay = (Pass == ERenderPass::ViewModeMesh) && bMeshScalarOverlay;
	const bool bUsesViewModeMeshShader = bViewModeMeshReplace || bViewModeMeshOverlay;

	if (bUsesViewModeMeshShader)
	{
		FMeshScalarOverlayConstants MeshScalarOverlayConstants = {};
		if (bClothMaxDistanceOverlay)
		{
			MeshScalarOverlayConstants.Mode = static_cast<int32>(EMeshScalarOverlayMode::ClothMaxDistance);
			MeshScalarOverlayConstants.OverlayAlpha = CollectClothMaxDistanceOverlayAlpha;

			FMeshScalarOverlayConstants WireOverlayConstants = {};
			WireOverlayConstants.Mode = static_cast<int32>(EMeshScalarOverlayMode::ClothMaxDistanceWire);
			WireOverlayConstants.OverlayAlpha = 1.0f;
			MeshScalarOverlayWireCB.Update(Ctx, &WireOverlayConstants, sizeof(FMeshScalarOverlayConstants));
		}
		else if (bBoneHeatMapOverlay)
		{
			MeshScalarOverlayConstants.Mode = static_cast<int32>(EMeshScalarOverlayMode::BoneWeight);
			MeshScalarOverlayConstants.SelectedBoneIndex = CollectWeightBoneHeatMapBoneIndex;
			MeshScalarOverlayConstants.OverlayAlpha = CollectWeightBoneHeatMapOverlayAlpha;
		}
		MeshScalarOverlayCB.Update(Ctx, &MeshScalarOverlayConstants, sizeof(FMeshScalarOverlayConstants));
	}

	// 섹션당 1개 커맨드 (per-section 셰이더)
 	for (const FMeshSectionDraw& Section : Proxy.GetSectionDraws())
	{
		if (Section.IndexCount == 0) continue;

		// per-section 패스 라우팅: PreDepth 는 Opaque 섹션만, 머티리얼-home 패스는 섹션 패스 일치만 빌드.
		// SelectionMask 같은 유틸 패스는 (선택된) 모든 섹션을 그려야 하므로 필터하지 않는다.
		const ERenderPass SecPass = SectionRenderPass(Section);
		if (bDepthOnly) { if (SecPass != ERenderPass::Opaque) continue; }
		else if (bViewModeMeshReplace) { if (SecPass != ERenderPass::Opaque) continue; }
		else if (bViewModeMeshOverlay)
		{
			if (bClothMaxDistanceOverlay)
			{
				if (Section.FirstIndex != ClothOverlayFirstIndex ||
					Section.IndexCount != ClothOverlayIndexCount) continue;
			}
			else if (SecPass != ERenderPass::Opaque)
			{
				continue;
			}
		}
		else if (Pass != ERenderPass::SelectionMask && SecPass != Pass) continue;

		// Section의 BufferOverride 있으면 사용, 없으면 proxy 공유 ProxyBuffer.
		const FDrawCommandBuffer& EffBuffer = Section.BufferOverride.HasBuffers()
			? Section.BufferOverride
			: ProxyBuffer;
		if (!EffBuffer.IB) continue;

			// 셰이더 도출: custom override 우선, 아니면 (Domain × VertexFactory × Pass × ViewMode).
			UMaterial* SectionMaterial = GetValidSectionMaterial(Section);
			FShader* EffectiveShader;
			if (bUsesViewModeMeshShader)
			{
				EffectiveShader = GetViewModeMeshShader(bGPUSkinning);
			}
			else if (SectionMaterial)
			{
				const EVertexFactoryType VFType =
					(Section.VertexFactory != EVertexFactoryType::Auto) ? Section.VertexFactory
					: (bSkeletal ? EVertexFactoryType::SkeletalMesh : EVertexFactoryType::StaticMesh);
					EffectiveShader = ResolveSectionShader(SectionMaterial, VFType, SecPass, CollectViewMode, bGPUSkinning);
			}
			else
			{
				// 머티리얼 없는 섹션(예외) — 기존 Proxy 셰이더 경로 보존.
				EffectiveShader = SelectEffectiveShader(Proxy.GetShader(), CollectViewMode, bGPUSkinning);
			}

		const bool bClothOverlayOnTransparentSection =
			bClothMaxDistanceOverlay && SecPass == ERenderPass::Transparent;
		const ERenderPass EffectiveCommandPass = bClothOverlayOnTransparentSection
			? ERenderPass::Transparent
			: Pass;

		FDrawCommand& Cmd = DrawCommandList.AddCommand();
		Cmd.Pass = EffectiveCommandPass;
		Cmd.Shader = EffectiveShader;
		Cmd.RenderState = BaseRenderState;
		Cmd.Buffer = EffBuffer;
		Cmd.PerObjectCB = PerObjCB;
		Cmd.bIsSkeletal = bSkeletal;
		Cmd.bIsGpuSkinned = bGPUSkinning;
		Cmd.Buffer.FirstIndex = Section.FirstIndex;
		Cmd.Buffer.IndexCount = Section.IndexCount;
		Cmd.Bindings.SkinMatrixSRV = bGPUSkinning && SkeletalProxy
			? SkeletalProxy->GetSkinMatrixSRV(CachedDevice, Ctx)
			: nullptr;
		Cmd.Bindings.MeshScalarOverlayCB = bUsesViewModeMeshShader ? &MeshScalarOverlayCB : nullptr;

		if (bViewModeMeshOverlay)
		{
			Cmd.RenderState.DepthStencil = EDepthStencilState::DepthReadOnly;
			Cmd.RenderState.Blend = EBlendState::AlphaBlend;
		}
	
		if (!bDepthOnly && SectionMaterial)
		{
			UMaterial* Mat = SectionMaterial;

			// dirty CB 업로드 (ConstantBufferMap + PerShaderOverride)
			Mat->FlushDirtyBuffers(CachedDevice, Ctx);

			Cmd.Bindings.PerShaderCB[0] = Mat->GetGPUBufferBySlot(ECBSlot::PerShader0);
			Cmd.Bindings.PerShaderCB[1] = Mat->GetGPUBufferBySlot(ECBSlot::PerShader1);

			// CachedSRVs에서 직접 복사 (map lookup 회피)
			const ID3D11ShaderResourceView* const* MatSRVs = Mat->GetCachedSRVs();
			for (int s = 0; s < (int)EMaterialTextureSlot::Max; s++)
				Cmd.Bindings.SRVs[s] = const_cast<ID3D11ShaderResourceView*>(MatSRVs[s]);

			// 섹션별 Material의 RenderPass가 현재 Pass와 일치할 때만 렌더 상태 오버라이드
			if (Pass == Mat->GetRenderPass())
				ApplyMaterialRenderState(Cmd.RenderState, Mat, BaseRenderState);
		}

		// 섹션이 자체 sort 위치를 제공하면(입자 emitter) 섹션별 depth, 아니면 proxy 거리(기본).
		float SectionDistSq = DistSq;
		if (Section.bHasSortPos)
		{
			const FVector ToCamSec = CollectCameraPosition - Section.SortWorldPos;
			SectionDistSq = ToCamSec.Dot(ToCamSec);
		}
		const float OverlaySortDistSq = bClothOverlayOnTransparentSection ? 0.0f : SectionDistSq;
		Cmd.BuildSortKey(Section.SortPriority, OverlaySortDistSq);

		if (bClothMaxDistanceOverlay)
		{
			FDrawCommand& WireCmd = DrawCommandList.AddCommand();
			WireCmd = Cmd;
			WireCmd.Bindings.MeshScalarOverlayCB = &MeshScalarOverlayWireCB;
			WireCmd.RenderState.Rasterizer = ERasterizerState::WireFrame;
			WireCmd.BuildSortKey(1, OverlaySortDistSq);
		}
	}
}

// ============================================================
// BuildDecalCommandForReceiver
// ============================================================
void FDrawCommandBuilder::BuildDecalCommandForReceiver(FScene& Scene, const FPrimitiveSceneProxy& ReceiverProxy, const FPrimitiveSceneProxy& DecalProxy)
{
	if (!ReceiverProxy.GetMeshBuffer() || !ReceiverProxy.GetMeshBuffer()->IsValid()) return;

	// Decal Material은 SectionDraws[0]에 저장됨. GC/Destroy 중인 material은 즉시 제외한다.
	UMaterial* DecalMat = DecalProxy.GetSectionDraws().empty() ? nullptr : GetValidSectionMaterial(DecalProxy.GetSectionDraws()[0]);
	if (!DecalMat || !DecalMat->GetShader()) return;
	const FDecalSceneProxy* DecalSceneProxy = static_cast<const FDecalSceneProxy*>(&DecalProxy);
	UMaterial* SourceDecalMat = DecalSceneProxy ? DecalSceneProxy->GetSourceMaterial() : nullptr;

	ID3D11DeviceContext* Ctx = CachedContext;
	const ERenderPass DecalPass = DecalProxy.GetRenderPass();
	const FDrawCommandRenderState BaseRenderState = PassRenderStateTable->ToDrawCommandState(DecalPass, CollectViewMode);

	FConstantBuffer* ReceiverPerObjCB = GetPerObjectCBForProxy(&Scene, ReceiverProxy);
	if (ReceiverPerObjCB && ReceiverProxy.NeedsPerObjectCBUpload())
	{
		ReceiverPerObjCB->Update(Ctx, &ReceiverProxy.GetPerObjectConstants(), sizeof(FPerObjectConstants));
		ReceiverProxy.ClearPerObjectCBDirty();
	}

	// Decal Material의 CB 업로드 (PerShaderOverride 포함)
	DecalMat->FlushDirtyBuffers(CachedDevice, Ctx);
	if (SourceDecalMat)
		SourceDecalMat->FlushDirtyBuffers(CachedDevice, Ctx);

	FDrawCommandBuffer ReceiverBuffer;
	ReceiverBuffer.VB = ReceiverProxy.GetMeshBuffer()->GetVertexBuffer().GetBuffer();
	ReceiverBuffer.VBStride = ReceiverProxy.GetMeshBuffer()->GetVertexBuffer().GetStride();
	ReceiverBuffer.IB = ReceiverProxy.GetMeshBuffer()->GetIndexBuffer().GetBuffer();

	auto AddDraw = [&](uint32 FirstIndex, uint32 IndexCount)
		{
			if (IndexCount == 0) return;

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.Pass = DecalPass;
			Cmd.Shader = DecalMat->GetShader();
			Cmd.RenderState = BaseRenderState;

			// 머티리얼 기반 렌더 상태 오버라이드
			ApplyMaterialRenderState(Cmd.RenderState, DecalMat, BaseRenderState);

			Cmd.Buffer = ReceiverBuffer;
			Cmd.Buffer.FirstIndex = FirstIndex;
			Cmd.Buffer.IndexCount = IndexCount;
			Cmd.PerObjectCB = ReceiverPerObjCB;
			Cmd.Bindings.PerShaderCB[0] = DecalMat->GetGPUBufferBySlot(ECBSlot::PerShader0);
			Cmd.Bindings.PerShaderCB[1] = SourceDecalMat ? SourceDecalMat->GetGPUBufferBySlot(ECBSlot::PerShader1) : nullptr;

			// Material의 CachedSRVs에서 텍스처 바인딩
			const ID3D11ShaderResourceView* const* MatSRVs = DecalMat->GetCachedSRVs();
			for (int s = 0; s < (int)EMaterialTextureSlot::Max; s++)
				Cmd.Bindings.SRVs[s] = const_cast<ID3D11ShaderResourceView*>(MatSRVs[s]);

			Cmd.BuildSortKey();
		};

	if (!ReceiverProxy.GetSectionDraws().empty())
	{
		for (const FMeshSectionDraw& Section : ReceiverProxy.GetSectionDraws())
		{
			AddDraw(Section.FirstIndex, Section.IndexCount);
		}
	}
	else if (ReceiverBuffer.IB)
	{
		AddDraw(0, ReceiverProxy.GetMeshBuffer()->GetIndexBuffer().GetIndexCount());
	}
}

// ============================================================
// AddWorldText — Font 프록시 배칭
// ============================================================
void FDrawCommandBuilder::AddWorldText(const FTextRenderSceneProxy* TextProxy, const FFrameContext& Frame)
{
	FontGeometry.AddWorldText(
		TextProxy->CachedText,
		TextProxy->CachedBillboardMatrix.GetLocation(),
		Frame.CameraRight,
		Frame.CameraUp,
		TextProxy->CachedBillboardMatrix.GetScale(),
		TextProxy->CachedFontScale
	);
}

// ============================================================
// BuildCommands — 프록시 커맨드 + 동적 커맨드 일괄 생성
// ============================================================
void FDrawCommandBuilder::BuildCommands(const FFrameContext& Frame, FScene* Scene, const FCollectOutput& Output)
{
	if (Scene)
	{
		EnsurePerObjectCBPoolCapacity(Scene, Scene->GetProxyCount());
		BuildProxyCommands(Frame, *Scene, Output);
	}

	BuildDynamicCommands(Frame, Scene);
}

// ============================================================
// BuildProxyCommands — RenderableProxies → DrawCommand
// ============================================================
void FDrawCommandBuilder::BuildProxyCommands(const FFrameContext& Frame, FScene& Scene, const FCollectOutput& Output)
{
	const bool bShowBoundingVolume = Frame.RenderOptions.ShowFlags.bBoundingVolume;
	const bool bIsEditor = (Frame.WorldType == EWorldType::Editor);
	const bool bShowCollision = bIsEditor
		? Frame.RenderOptions.ShowFlags.bCollision
		: Frame.RenderOptions.ShowFlags.bShowCollisionShape;
	const bool bPureDebugView = ViewModeUtils::IsPureDebugViewMode(Frame.RenderOptions.ViewMode);
	const bool bSuppressEditorOverlays = ViewModeUtils::SuppressesEditorOverlays(Frame.RenderOptions.ViewMode);

	for (FPrimitiveSceneProxy* Proxy : Output.RenderableProxies)
	{
		if ((bSuppressEditorOverlays && IsEditorHelperProxy(*Proxy)) ||
			(bPureDebugView && Proxy->HasProxyFlag(EPrimitiveProxyFlags::Decal)))
		{
			// Debug visualizers keep their output plus selection/gizmo, not editor helper overlays.
		}
		else if (Proxy->HasProxyFlag(EPrimitiveProxyFlags::BoneDebug))
		{
			const FBoneDebugSceneProxy* BoneProxy = static_cast<const FBoneDebugSceneProxy*>(Proxy);
			for (const FWireLine& Line : BoneProxy->GetCachedLines())
			{
				DebugBoneLines.AddLine(Line.Start, Line.End, BoneProxy->GetBoneColor());
			}
			for (const FWireLine& Line : BoneProxy->GetCachedParentBoneLines())
			{
				DebugBoneLines.AddLine(Line.Start, Line.End, BoneProxy->GetParentBoneColor());
			}
			for (const FWireLine& Line : BoneProxy->GetCachedSocketLines())
			{
				DebugBoneLines.AddLine(Line.Start, Line.End, BoneProxy->GetSocketColor());
			}
		}
		else if (Proxy->HasProxyFlag(EPrimitiveProxyFlags::WireShape))
		{
			if (bShowCollision)
			{
				const FShapeSceneProxy* ShapeProxy = static_cast<const FShapeSceneProxy*>(Proxy);
				const FVector4& Color = ShapeProxy->GetWireColor();
				for (const FWireLine& Line : ShapeProxy->GetCachedLines())
				{
					EditorLines.AddLine(Line.Start, Line.End, Color);
				}
			}
		}
		else if (Proxy->HasProxyFlag(EPrimitiveProxyFlags::FontBatched))
		{
			const FTextRenderSceneProxy* TextProxy = static_cast<const FTextRenderSceneProxy*>(Proxy);
			if (!TextProxy->CachedText.empty())
				AddWorldText(TextProxy, Frame);
		}
		else if (Proxy->HasProxyFlag(EPrimitiveProxyFlags::Decal))
			BuildDecalCommands(Scene, Proxy, Frame, Output);
		else if (ProxyHasGizmoPass(*Proxy))
			BuildGizmoCommands(Scene, Proxy);
		else
			BuildMeshCommands(Scene, Proxy);

		if (bCollectSelectionOutline && Proxy->IsSelected())
			BuildSelectionCommands(Proxy, bShowBoundingVolume, Scene);
	}
}

// ============================================================
// BuildDecalCommands — Decal → Receiver 순회 + 커맨드 생성
// ============================================================
void FDrawCommandBuilder::BuildDecalCommands(FScene& Scene, FPrimitiveSceneProxy* Proxy, const FFrameContext& Frame, const FCollectOutput& Output)
{
	FDecalSceneProxy* DecalProxy = static_cast<FDecalSceneProxy*>(Proxy);

	for (FPrimitiveSceneProxy* ReceiverProxy : DecalProxy->GetReceiverProxies())
	{
		if (!ReceiverProxy || Output.VisibleProxySet.find(ReceiverProxy) == Output.VisibleProxySet.end())
			continue;

		UpdateProxyLOD(ReceiverProxy, Frame.LODContext);

		if (ReceiverProxy->HasProxyFlag(EPrimitiveProxyFlags::PerViewportUpdate))
			ReceiverProxy->UpdatePerViewport(Frame);

		BuildDecalCommandForReceiver(Scene, *ReceiverProxy, *DecalProxy);
	}
}

// ============================================================
// BuildGizmoCommands — editor transform gizmo overlay
// ============================================================
void FDrawCommandBuilder::BuildGizmoCommands(FScene& Scene, const FPrimitiveSceneProxy* Proxy)
{
	bool bPassSeen[(int)ERenderPass::MAX] = {};

	for (const FMeshSectionDraw& Section : Proxy->GetSectionDraws())
	{
		if (Section.IndexCount == 0) continue;
		const ERenderPass Pass = SectionRenderPass(Section);
		if ((int)Pass < (int)ERenderPass::MAX && IsGizmoPass(Pass))
			bPassSeen[(int)Pass] = true;
	}

	const ERenderPass ProxyPass = Proxy->GetRenderPass();
	if ((int)ProxyPass < (int)ERenderPass::MAX && IsGizmoPass(ProxyPass))
		bPassSeen[(int)ProxyPass] = true;

	for (int p = 0; p < (int)ERenderPass::MAX; ++p)
	{
		if (bPassSeen[p])
			BuildCommandForProxy(Scene, *Proxy, static_cast<ERenderPass>(p));
	}
}

// ============================================================
// BuildMeshCommands — 일반 메시 (PreDepth + 메인 패스)
// ============================================================
void FDrawCommandBuilder::BuildMeshCommands(FScene& Scene, const FPrimitiveSceneProxy* Proxy)
{
	// 섹션별 머티리얼/override 가 정한 패스로 라우팅 (멀티 슬롯에서 슬롯마다 다른 패스 지원).
	bool bPassSeen[(int)ERenderPass::MAX] = {};
	bool bAnyOpaque = false;
	for (const FMeshSectionDraw& Section : Proxy->GetSectionDraws())
	{
		if (Section.IndexCount == 0) continue;
		const ERenderPass P = SectionRenderPass(Section);
		if (ShouldSuppressPassForViewMode(CollectViewMode, P, bCollectEditorIcons)) continue;
		if ((int)P < (int)ERenderPass::MAX) bPassSeen[(int)P] = true;
		if (P == ERenderPass::Opaque) bAnyOpaque = true;
	}

	// 동적/lazy 프록시(파티클 등)는 PrepareDrawBuffer 전까지 실제 섹션이 placeholder(IndexCount=0)뿐이라
	// 위 사전스캔에서 안 잡힌다. 프록시 단위 GetRenderPass()로 보강 — 기존 per-proxy 라우팅을 복원해
	// BuildCommandForProxy 안에서 PrepareDrawBuffer 가 실제 섹션을 채우게 한다(정적 멀티슬롯엔 무영향: 중복 표시).
	const ERenderPass ProxyPass = Proxy->GetRenderPass();
	if ((int)ProxyPass < (int)ERenderPass::MAX && !ShouldSuppressPassForViewMode(CollectViewMode, ProxyPass, bCollectEditorIcons))
	{
		bPassSeen[(int)ProxyPass] = true;
	}
	if (ProxyPass == ERenderPass::Opaque) bAnyOpaque = true;

	const bool bNeedsClothMaxDistanceOverlay =
		bCollectClothMaxDistanceOverlay &&
		CollectClothOverlayLODIndex >= 0 &&
		CollectClothOverlayIndex >= 0 &&
		Proxy->HasProxyFlag(EPrimitiveProxyFlags::SkeletalMesh) &&
		static_cast<const FSkeletalMeshSceneProxy*>(Proxy)->GetEffectiveSkinningMode() == ESkinningMode::CPU;
	const bool bNeedsBoneHeatMapOverlay =
		bCollectWeightBoneHeatMap &&
		CollectWeightBoneHeatMapBoneIndex >= 0 &&
		Proxy->HasProxyFlag(EPrimitiveProxyFlags::SkeletalMesh);

	// Opaque 섹션이 있으면 PreDepth 선행 (BuildCommandForProxy 가 Opaque 섹션만 필터).
	if (bAnyOpaque)
		BuildCommandForProxy(Scene, *Proxy, ERenderPass::PreDepth);

	if (ViewModeUtils::IsPureMeshDebugViewMode(CollectViewMode))
	{
		const bool bDebugMesh =
			Proxy->HasProxyFlag(EPrimitiveProxyFlags::StaticMesh) ||
			Proxy->HasProxyFlag(EPrimitiveProxyFlags::SkeletalMesh);

		if (bDebugMesh && bAnyOpaque)
		{
			BuildCommandForProxy(Scene, *Proxy, ERenderPass::ViewModeMesh);
		}
		return;
	}

	for (int p = 0; p < (int)ERenderPass::MAX; ++p)
		if (bPassSeen[p])
			BuildCommandForProxy(Scene, *Proxy, static_cast<ERenderPass>(p));

	if (!ViewModeUtils::IsPureDebugViewMode(CollectViewMode) &&
		(bNeedsBoneHeatMapOverlay || bNeedsClothMaxDistanceOverlay))
	{
		BuildCommandForProxy(Scene, *Proxy, ERenderPass::ViewModeMesh);
	}
}

// ============================================================
// BuildSelectionCommands — 아웃라인 + AABB
// ============================================================
void FDrawCommandBuilder::BuildSelectionCommands(FPrimitiveSceneProxy* Proxy, bool bShowBoundingVolume, FScene& Scene)
{
	if (Proxy->HasProxyFlag(EPrimitiveProxyFlags::SupportsOutline))
		BuildCommandForProxy(Scene, *Proxy, ERenderPass::SelectionMask);

	if (bShowBoundingVolume && Proxy->HasProxyFlag(EPrimitiveProxyFlags::ShowAABB))
		Scene.AddDebugAABB(Proxy->GetCachedBounds().Min, Proxy->GetCachedBounds().Max, FColor::White());
}

// ============================================================
// BuildDynamicCommands — Scene 경량 데이터 → 동적 지오메트리 → FDrawCommand
// ============================================================
void FDrawCommandBuilder::BuildDynamicCommands(const FFrameContext& Frame, const FScene* Scene)
{
	PrepareDynamicGeometry(Frame, Scene);
	BuildDynamicDrawCommands(Frame, Scene);
}

// ============================================================
// PrepareDynamicGeometry — FScene의 경량 데이터 → 라인/폰트 지오메트리
// ============================================================
void FDrawCommandBuilder::PrepareDynamicGeometry(const FFrameContext& Frame, const FScene* Scene)
{
	if (!Scene) return;

	// --- Editor 패스: AABB 디버그 박스 + DebugDraw 라인 ---
	for (const auto& AABB : Scene->GetDebugAABBs())
	{
		EditorLines.AddAABB(FBoundingBox{ AABB.Min, AABB.Max }, AABB.Color);
	}
	for (const auto& Line : Scene->GetDebugLines())
	{
		EditorLines.AddLine(Line.Start, Line.End, Line.Color.ToVector4());
	}

	// --- Grid 패스: 월드 그리드 + 축 ---
	if (Scene->HasGrid())
	{
		const FVector CameraPos = Frame.View.GetInverseFast().GetLocation();
		FVector CameraFwd = Frame.CameraRight.Cross(Frame.CameraUp);
		CameraFwd.Normalize();

		GridLines.AddWorldHelpers(
			Frame.RenderOptions.ShowFlags,
			Scene->GetGridSpacing(),
			Scene->GetGridHalfLineCount(),
			CameraPos, CameraFwd, Frame.IsFixedOrtho());
	}

	// --- OverlayFont 패스: 스크린 공간 텍스트 ---
	for (const auto& Text : Scene->GetOverlayTexts())
	{
		if (!Text.Text.empty())
		{
			FontGeometry.AddScreenText(
				Text.Text,
				Text.Position.X,
				Text.Position.Y,
				Frame.ViewportWidth,
				Frame.ViewportHeight,
				Text.Scale
			);
		}
	}
}

// ============================================================
// BuildDynamicDrawCommands — 오케스트레이터
// ============================================================
void FDrawCommandBuilder::BuildDynamicDrawCommands(const FFrameContext& Frame, const FScene* Scene)
{
	EViewMode ViewMode = Frame.RenderOptions.ViewMode;
	BuildEditorLineCommands(ViewMode);
	BuildPostProcessCommands(Frame, Scene);
	BuildFontCommands(ViewMode);
}

// ============================================================
// EmitLineCommand — 라인 지오메트리 → FDrawCommand 공통 헬퍼
// ============================================================
void FDrawCommandBuilder::EmitLineCommand(FLineGeometry& Lines, FShader* Shader, const FDrawCommandRenderState& RS)
{
	if (Lines.GetLineCount() > 0 && Lines.UploadBuffers(CachedContext))
	{
		FDrawCommand& Cmd = DrawCommandList.AddCommand();
		Cmd.Pass = ERenderPass::EditorLines;
		Cmd.Shader = Shader;
		Cmd.RenderState = RS;
		Cmd.Buffer = { Lines.GetVBBuffer(), Lines.GetVBStride(), Lines.GetIBBuffer() };
		Cmd.Buffer.IndexCount = Lines.GetIndexCount();
		Cmd.BuildSortKey();
	}
}

// ============================================================
// BuildEditorLineCommands — EditorLines + GridLines
// ============================================================
void FDrawCommandBuilder::BuildEditorLineCommands(EViewMode ViewMode)
{
	if (ViewModeUtils::SuppressesEditorOverlays(ViewMode))
		return;

	FShader* EditorShader = FShaderManager::Get().GetOrCreate(EShaderPath::Editor);
	const FDrawCommandRenderState EditorLinesRS = PassRenderStateTable->ToDrawCommandState(ERenderPass::EditorLines, ViewMode);

	FDrawCommandRenderState EditorLinesNoDepthRS = EditorLinesRS;
	EditorLinesNoDepthRS.DepthStencil = EDepthStencilState::NoDepth;

	EmitLineCommand(EditorLines, EditorShader, EditorLinesNoDepthRS);
	EmitLineCommand(GridLines, EditorShader, EditorLinesRS);

	FDrawCommandRenderState BoneLinesRS = EditorLinesRS;
	BoneLinesRS.DepthStencil = EDepthStencilState::NoDepth;

	EmitLineCommand(DebugBoneLines, EditorShader, BoneLinesRS);
}

// ============================================================
// BuildPostProcessCommands — fullscreen scene effects, debug resolves, and final image filters
// ============================================================
void FDrawCommandBuilder::BuildPostProcessCommands(const FFrameContext& Frame, const FScene* CollectScene)
{
	ID3D11DeviceContext* Ctx = CachedContext;
	EViewMode ViewMode = Frame.RenderOptions.ViewMode;
	const FDrawCommandRenderState PPRS = PassRenderStateTable->ToDrawCommandState(ERenderPass::PostProcess, ViewMode);
	const bool bPureDebugView = ViewModeUtils::IsPureDebugViewMode(ViewMode);

	// HeightFog — Transparent 前 전용 Fog 패스로 라우팅(불투명/하늘만 fog, Transparent는 위에 그려져 안 덮임).
	if (!bPureDebugView && Frame.RenderOptions.ShowFlags.bFog && CollectScene && CollectScene->GetEnvironment().HasFog())
	{
		FShader* FogShader = FShaderManager::Get().GetOrCreate(EShaderPath::HeightFog);
		if (FogShader)
		{
			const FDrawCommandRenderState FogRS = PassRenderStateTable->ToDrawCommandState(ERenderPass::Fog, ViewMode);
			const FFogParams& FogParams = CollectScene->GetEnvironment().GetFogParams();
			FFogConstants fogData = {};
			fogData.InscatteringColor = FogParams.InscatteringColor;
			fogData.Density = FogParams.Density;
			fogData.HeightFalloff = FogParams.HeightFalloff;
			fogData.FogBaseHeight = FogParams.FogBaseHeight;
			fogData.StartDistance = FogParams.StartDistance;
			fogData.CutoffDistance = FogParams.CutoffDistance;
			fogData.MaxOpacity = FogParams.MaxOpacity;
			FogCB.Update(Ctx, &fogData, sizeof(FFogConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.InitFullscreenTriangle(FogShader, ERenderPass::Fog, FogRS);
			Cmd.Bindings.PerShaderCB[0] = &FogCB;
			Cmd.BuildSortKey(0);
		}
	}

	const bool bDoFCoCDebug = Frame.RenderOptions.ViewMode == EViewMode::DoFCoC;
	if (!bPureDebugView && (Frame.RenderOptions.ShowFlags.bDoF || bDoFCoCDebug))
	{
		FDoFConstants DoFData = {};
		DoFData.FocusDistance = Frame.RenderOptions.DoFFocusDistance;
		DoFData.FocusRange = Frame.RenderOptions.DoFFocusRange;
		DoFData.MaxBlurRadius = Frame.RenderOptions.DoFMaxBlurRadius;
		DoFData.BokehRadiusThreshold = Frame.RenderOptions.DoFBokehRadiusThreshold;
		DoFData.BokehLumaThreshold = Frame.RenderOptions.DoFBokehLumaThreshold;
		DoFData.BokehIntensity = Frame.RenderOptions.DoFBokehIntensity;
		DoFCB.Update(Ctx, &DoFData, sizeof(FDoFConstants));

		FShader* DoFSetupShader = FShaderManager::Get().GetOrCreate(EShaderPath::DoFSetup);
		if (DoFSetupShader)
		{
			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.InitFullscreenTriangle(DoFSetupShader, ERenderPass::DoFSetup,
				PassRenderStateTable->ToDrawCommandState(ERenderPass::DoFSetup, ViewMode));
			Cmd.Bindings.PerShaderCB[0] = &DoFCB;
			Cmd.BuildSortKey(0);
		}

		if (bDoFCoCDebug)
		{
			FShader* DoFDebugShader = FShaderManager::Get().GetOrCreate(EShaderPath::DoFCoCDebug);
			if (DoFDebugShader)
			{
				FDrawCommand& Cmd = DrawCommandList.AddCommand();
				Cmd.InitFullscreenTriangle(DoFDebugShader, ERenderPass::DoF,
					PassRenderStateTable->ToDrawCommandState(ERenderPass::DoF, ViewMode));
				Cmd.Bindings.PerShaderCB[0] = &DoFCB;
				Cmd.BuildSortKey(0);
			}
		}
		else
		{
			FShader* DoFBackgroundShader = FShaderManager::Get().GetOrCreate(EShaderPath::DoFBackgroundBlur);
			if (DoFBackgroundShader)
			{
				FDrawCommand& Cmd = DrawCommandList.AddCommand();
				Cmd.InitFullscreenTriangle(DoFBackgroundShader, ERenderPass::DoFBackgroundBlur,
					PassRenderStateTable->ToDrawCommandState(ERenderPass::DoFBackgroundBlur, ViewMode));
				Cmd.Bindings.PerShaderCB[0] = &DoFCB;
				Cmd.BuildSortKey(0);
			}

			FShader* DoFForegroundShader = FShaderManager::Get().GetOrCreate(EShaderPath::DoFForegroundBlur);
			if (DoFForegroundShader)
			{
				FDrawCommand& Cmd = DrawCommandList.AddCommand();
				Cmd.InitFullscreenTriangle(DoFForegroundShader, ERenderPass::DoFForegroundBlur,
					PassRenderStateTable->ToDrawCommandState(ERenderPass::DoFForegroundBlur, ViewMode));
				Cmd.Bindings.PerShaderCB[0] = &DoFCB;
				Cmd.BuildSortKey(0);
			}

			FShader* DoFBokehShader = FShaderManager::Get().GetOrCreate(EShaderPath::DoFBokehScatter);
			if (DoFBokehShader)
			{
				FDrawCommand& Cmd = DrawCommandList.AddCommand();
				Cmd.InitFullscreenTriangle(DoFBokehShader, ERenderPass::DoFBokehScatter,
					PassRenderStateTable->ToDrawCommandState(ERenderPass::DoFBokehScatter, ViewMode));
				Cmd.Bindings.PerShaderCB[0] = &DoFCB;
				Cmd.BuildSortKey(0);
			}

			FShader* DoFShader = FShaderManager::Get().GetOrCreate(EShaderPath::DoFComposite);
			if (DoFShader)
			{
				FDrawCommand& Cmd = DrawCommandList.AddCommand();
				Cmd.InitFullscreenTriangle(DoFShader, ERenderPass::DoF,
					PassRenderStateTable->ToDrawCommandState(ERenderPass::DoF, ViewMode));
				Cmd.Bindings.PerShaderCB[0] = &DoFCB;
				Cmd.BuildSortKey(0);
			}
		}
	}

	if (!bPureDebugView)
	{
		BuildCameraPostProcessMaterialCommand(Frame, PPRS);
	}

	// Outline (UserBits=1 → HeightFog 뒤)
	if (bHasSelectionMaskCommands)
	{
		FShader* PPShader = FShaderManager::Get().GetOrCreate(EShaderPath::Outline);
		if (PPShader)
		{
			FOutlinePostProcessConstants ppConstants;
			ppConstants.OutlineColor = FVector4(1.0f, 1.0f, 0.0f, 1.0f);
			ppConstants.OutlineThickness = 3.0f;
			OutlineCB.Update(Ctx, &ppConstants, sizeof(ppConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.InitFullscreenTriangle(PPShader, ERenderPass::PostProcess, PPRS);
			Cmd.Bindings.PerShaderCB[0] = &OutlineCB;
			Cmd.BuildSortKey(1);
		}
	}

	// SceneDepth fullscreen debug resolve
	if (CollectViewMode == EViewMode::SceneDepth)
	{
		FShader* DepthShader = FShaderManager::Get().GetOrCreate(EShaderPath::DebugViewModeResolve);
		if (DepthShader)
		{
			FViewportRenderOptions Opts = Frame.RenderOptions;
			FSceneDepthPConstants depthData = {};
			depthData.Exponent = Opts.Exponent;
			depthData.NearClip = Frame.NearClip;
			depthData.FarClip = Frame.FarClip;
			depthData.Mode = Opts.SceneDepthVisMode;
			SceneDepthCB.Update(Ctx, &depthData, sizeof(FSceneDepthPConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.InitFullscreenTriangle(DepthShader, ERenderPass::DebugViewModeResolve,
				PassRenderStateTable->ToDrawCommandState(ERenderPass::DebugViewModeResolve, ViewMode));
			Cmd.Bindings.PerShaderCB[0] = &SceneDepthCB;
			Cmd.BuildSortKey(0);
		}
	}

	// FXAA
	if (!bPureDebugView && Frame.RenderOptions.ShowFlags.bFXAA)
	{
		FShader* FXAAShader = FShaderManager::Get().GetOrCreate(EShaderPath::FXAA);
		if (FXAAShader)
		{
			FViewportRenderOptions Opts = Frame.RenderOptions;
			FFXAAConstants FXAAData = {};
			FXAAData.EdgeThreshold = Opts.EdgeThreshold;
			FXAAData.EdgeThresholdMin = Opts.EdgeThresholdMin;
			FXAACB.Update(Ctx, &FXAAData, sizeof(FFXAAConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.InitFullscreenTriangle(FXAAShader, ERenderPass::FXAA,
				PassRenderStateTable->ToDrawCommandState(ERenderPass::FXAA, ViewMode));
			Cmd.Bindings.PerShaderCB[0] = &FXAACB;
			Cmd.BuildSortKey(0);
		}
	}

	// Camera Fade
	if (!bPureDebugView && Frame.CameraFade.bEnabled && Frame.CameraFade.Amount > 0.0f)
	{
		FShader* FadeShader = FShaderManager::Get().GetOrCreate(EShaderPath::CameraFade);
		if (FadeShader)
		{
			FCameraFadeConstants FadeData = {};
			FadeData.FadeColor = Frame.CameraFade.Color.ToVector4();
			FadeData.FadeAmount = Frame.CameraFade.Amount;

			CameraFadeCB.Update(Ctx, &FadeData, sizeof(FCameraFadeConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.InitFullscreenTriangle(FadeShader, ERenderPass::PostProcess, PPRS);
			Cmd.Bindings.PerShaderCB[0] = &CameraFadeCB;
			Cmd.BuildSortKey(5);
		}
	}

	// Camera Vignette
	if (!bPureDebugView && Frame.CameraVignette.bEnabled && Frame.CameraVignette.Intensity > 0.0f)
	{
		FShader* VignetteShader = FShaderManager::Get().GetOrCreate(EShaderPath::CameraVignette);
		if (VignetteShader)
		{
			FCameraVignetteConstants VignetteData = {};
			VignetteData.VignetteColor = Frame.CameraVignette.Color.ToVector4();
			VignetteData.VignetteIntensity = Frame.CameraVignette.Intensity;
			VignetteData.VignetteRadius = Frame.CameraVignette.Radius;
			VignetteData.VignetteSoftness = Frame.CameraVignette.Softness;

			CameraVignetteCB.Update(Ctx, &VignetteData, sizeof(FCameraVignetteConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.InitFullscreenTriangle(VignetteShader, ERenderPass::PostProcess, PPRS);
			Cmd.Bindings.PerShaderCB[0] = &CameraVignetteCB;
			Cmd.BuildSortKey(6);
		}
	}

	// Camera Letterbox
	if (!bPureDebugView && Frame.CameraLetterbox.bEnabled && Frame.CameraLetterbox.Amount > 0.0f)
	{
		FShader* LetterboxShader = FShaderManager::Get().GetOrCreate(EShaderPath::CameraLetterbox);
		if (LetterboxShader)
		{
			FCameraLetterboxConstants LetterboxData = {};
			LetterboxData.LetterboxColor = Frame.CameraLetterbox.Color.ToVector4();
			LetterboxData.LetterboxAmount = Frame.CameraLetterbox.Amount;
			LetterboxData.LetterboxThickness = Frame.CameraLetterbox.Thickness;

			CameraLetterboxCB.Update(Ctx, &LetterboxData, sizeof(FCameraLetterboxConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.InitFullscreenTriangle(LetterboxShader, ERenderPass::PostProcess, PPRS);
			Cmd.Bindings.PerShaderCB[0] = &CameraLetterboxCB;
			Cmd.BuildSortKey(7);
		}
	}

	if (!bPureDebugView && Frame.RenderOptions.ShowFlags.bGammaCorrection)
	{
		FShader* GammaShader = FShaderManager::Get().GetOrCreate(EShaderPath::GammaCorrection);
		if (GammaShader)
		{
			FGammaCorrectionConstants GammaData = {};
			GammaData.Gamma = Frame.RenderOptions.Gamma;
			GammaCorrectionCB.Update(Ctx, &GammaData, sizeof(FGammaCorrectionConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.InitFullscreenTriangle(GammaShader, ERenderPass::GammaCorrection,
				PassRenderStateTable->ToDrawCommandState(ERenderPass::GammaCorrection, ViewMode));
			Cmd.Bindings.PerShaderCB[0] = &GammaCorrectionCB;
			Cmd.BuildSortKey(0);
		}
	}
}

// ============================================================
// BuildFontCommands — World text (AlphaBlend) + Screen text (OverlayFont)
// ============================================================
void FDrawCommandBuilder::BuildFontCommands(EViewMode ViewMode)
{
	if (ViewModeUtils::SuppressesEditorOverlays(ViewMode))
		return;

	const FFontResource* FontRes = FResourceManager::Get().FindFont(FName("Default"));
	if (!FontRes || !FontRes->IsLoaded()) return;

	ID3D11DeviceContext* Ctx = CachedContext;

	if (FontGeometry.GetWorldQuadCount() > 0 && FontGeometry.UploadWorldBuffers(Ctx))
	{
		FDrawCommand& Cmd = DrawCommandList.AddCommand();
		Cmd.Pass = ERenderPass::Transparent;
		Cmd.Shader = FShaderManager::Get().GetOrCreate(EShaderPath::Font);
		Cmd.RenderState = PassRenderStateTable->ToDrawCommandState(ERenderPass::Transparent, ViewMode);
		Cmd.Buffer = { FontGeometry.GetWorldVBBuffer(), FontGeometry.GetWorldVBStride(), FontGeometry.GetWorldIBBuffer() };
		Cmd.Buffer.IndexCount = FontGeometry.GetWorldIndexCount();
		Cmd.Bindings.SRVs[(int)EMaterialTextureSlot::Diffuse] = FontRes->SRV;
		Cmd.BuildSortKey();
	}

	if (FontGeometry.GetScreenQuadCount() > 0 && FontGeometry.UploadScreenBuffers(Ctx))
	{
		FDrawCommand& Cmd = DrawCommandList.AddCommand();
		Cmd.Pass = ERenderPass::OverlayFont;
		Cmd.Shader = FShaderManager::Get().GetOrCreate(EShaderPath::OverlayFont);
		Cmd.RenderState = PassRenderStateTable->ToDrawCommandState(ERenderPass::OverlayFont, ViewMode);
		Cmd.Buffer = { FontGeometry.GetScreenVBBuffer(), FontGeometry.GetScreenVBStride(), FontGeometry.GetScreenIBBuffer() };
		Cmd.Buffer.IndexCount = FontGeometry.GetScreenIndexCount();
		Cmd.Bindings.SRVs[(int)EMaterialTextureSlot::Diffuse] = FontRes->SRV;
		Cmd.BuildSortKey();
	}
}

// ============================================================
// PerObjectCB 풀 관리
// ============================================================
void FDrawCommandBuilder::EnsurePerObjectCBPoolCapacity(FScene* Scene, uint32 RequiredCount)
{
	if (!Scene) return;

	TArray<FConstantBuffer>& Pool = PerSceneObjectCBPool[Scene];

	if (Pool.size() >= RequiredCount) return;

	const size_t OldCount = Pool.size();
	Pool.resize(RequiredCount);

	for (size_t Index = OldCount; Index < Pool.size(); ++Index)
	{
		Pool[Index].Create(CachedDevice, sizeof(FPerObjectConstants), "PerObjectCB");
	}
}

FConstantBuffer* FDrawCommandBuilder::GetPerObjectCBForProxy(FScene* Scene, const FPrimitiveSceneProxy& Proxy)
{
	if (!Scene || Proxy.GetProxyId() == UINT32_MAX) return nullptr;

	EnsurePerObjectCBPoolCapacity(Scene, Proxy.GetProxyId() + 1);
	return &PerSceneObjectCBPool[Scene][Proxy.GetProxyId()];
}
