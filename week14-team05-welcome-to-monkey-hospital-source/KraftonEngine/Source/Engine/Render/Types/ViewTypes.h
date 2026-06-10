#pragma once

#include "Core/Types/CoreTypes.h"

enum class EViewMode : int32
{
	Lit_Phong = 0,
	Unlit,
	Lit_Gouraud,
	Lit_Lambert,
	Wireframe,
	SceneDepth,
	WorldNormal,
	LightCulling,
	DoFCoC,
	Count
};

enum class ELightCullingMode : uint32
{
	Off = 0,
	Tile = 1,
	Cluster = 2
};

enum class ESkinningMode : uint32
{
	CPU = 0,
	GPU = 1,
};

namespace SkinningModeRuntime
{
	inline ESkinningMode Current = ESkinningMode::GPU;

	inline ESkinningMode Get()
	{
		return Current;
	}

	inline void Set(ESkinningMode InMode)
	{
		Current = InMode;
	}
}

struct FShowFlags
{
	bool bStaticMesh = true;
	bool bSkeletalMesh = true;
	bool bParticles = true;             // ParticleSystemComponent 표시 토글
	bool bParticleBounds = false;       // 입자 시스템 AABB 디버그 표시
	bool bGrid = true;
	bool bWorldAxis = true;
	bool bGizmo = true;
	bool bEditorIcons = true;
	bool bSelectionOutline = true;
	bool bBillboardText = true;
	bool bBoundingVolume = false;
	bool bDebugDraw = true;
	bool bOctree = false;
	bool bFog = true;
	bool bDoF = false;
	bool bFXAA = false;
	bool bGammaCorrection = true;
	bool bViewLightCulling = false;
	bool bVisualize25DCulling = false;
	bool bShowShadowFrustum = false;
	bool bCollision = true;
	bool bShowCollisionShape = false;	// PIE/Game에서 콘솔로 콜리전 shape 와이어프레임 강제 표시
	bool bPhysicsAssetShapes = true;	// Physics Asset Editor shape/body preview
	bool bPhysicsAssetConstraints = true;	// Physics Asset Editor constraint preview
	bool bPhysicsAssetBodySkeleton = false;	// Physics Asset Editor body-to-body skeleton preview
};

// 뷰포트 카메라 프리셋 (Perspective / 6방향 Orthographic)
enum class ELevelViewportType : uint8
{
	Perspective,
	Top,		// +Z → -Z
	Bottom,		// -Z → +Z
	Left,		// -Y → +Y
	Right,		// +Y → -Y
	Front,		// +X → -X
	Back,		// -X → +X
	FreeOrthographic	// 자유 각도 Orthographic
};

// 뷰포트별 렌더 옵션 — 각 뷰포트 클라이언트가 독립적으로 소유
struct FViewportRenderOptions
{
	EViewMode ViewMode = EViewMode::Lit_Phong;
	FShowFlags ShowFlags;

	float GridSpacing = 1.0f;
	int32 GridHalfLineCount = 100;

	float CameraMoveSensitivity = 1.0f;
	float CameraRotateSensitivity = 1.0f;
	ELevelViewportType ViewportType = ELevelViewportType::Perspective;

	// Scene Depth 전용 설정
	int32 SceneDepthVisMode = 0;
	float Exponent = 128.0f;
	float Range = 1000.0f;

	// FXAA 전용 설정
	float EdgeThreshold = 0.125f;
	float EdgeThresholdMin = 0.0625f;

	// Gamma Correction 전용 설정
	float Gamma = 2.4f;

	// Depth of Field 전용 설정
	float DoFFocusDistance = 500.0f;
	float DoFFocusRange = 200.0f;
	float DoFMaxBlurRadius = 4.0f;
	float DoFBokehRadiusThreshold = 2.5f;
	float DoFBokehLumaThreshold = 0.45f;
	float DoFBokehIntensity = 0.65f;

	// Light Culling 뷰모드 전용 설정
	ELightCullingMode LightCullingMode = ELightCullingMode::Cluster;
	float HeatMapMax = 20.0f;
	bool Enable25DCulling = true;

	// Particle editor vector field debug visualization
	bool bParticleVectorFieldDebug = false;

	// Mesh editor bone weight visualization
	bool bWeightBoneHeatMap = false;
	int32 WeightBoneHeatMapBoneIndex = -1;
	float WeightBoneHeatMapOverlayAlpha = 0.8f;

	// Mesh editor cloth paint visualization
	bool bClothMaxDistanceOverlay = false;
	int32 ClothOverlayLODIndex = -1;
	int32 ClothOverlayIndex = -1;
	float ClothMaxDistanceOverlayAlpha = 0.8f;
};
