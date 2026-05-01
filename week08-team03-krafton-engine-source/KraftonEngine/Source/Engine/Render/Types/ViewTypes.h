#pragma once

#include "Core/CoreTypes.h"

enum class EViewMode : int32
{
	Lit_Phong = 0,
	Unlit,
	Lit_Gouraud,
	Lit_Lambert,
	Lit_Toon,
	Wireframe,
	SceneDepth,
	WorldNormal,
	LightCulling,
	Count
};

enum class ELightCullingMode : uint32
{
	Off = 0,
	Tile = 1,
	Cluster = 2
};

enum class EShadowFilterMode : uint32
{
	None = 0,		// Hard Shadow
	PCF = 1,		// Percentage-Closer Filtering
	VSM = 2			// Variance Shadow Mapping
};

enum class EShadowMethod : uint32
{
	Standard = 0,
	PSM = 1,
	CSM = 2
};

struct FShowFlags
{
	bool bPrimitives = true;
	bool bGrid = true;
	bool bWorldAxis = true;
	bool bGizmo = true;
	bool bBillboardText = true;
	bool bBoundingVolume = false;
	bool bDebugDraw = true;
	bool bOctree = false;
	bool bFog = true;
	bool bFXAA = false;
	bool bViewLightCulling = false;
	bool bVisualize25DCulling = false;
	bool bShadows = true;
	bool bPSMDebugLines = false;
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

struct FShadowAtlasPrioritySettings
{
	float ScreenCoverageWeight = 0.35f;
	float LightContributionWeight = 0.25f;
	float ProximityWeight = 0.20f;
	float CasterReceiverWeight = 0.15f;
	float StabilityWeight = 0.05f;
	float SpotMustCoverageThreshold = 0.15f;
	float SpotMustProximityThreshold = 0.65f;
	float HysteresisFactor = 1.25f;
	float ProjectedResolutionScale = 1.5f;
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
	int32 SceneDepthVisMode = 1;
	float Exponent = 128.0f;
	float Range = 1000.0f;

	// FXAA 전용 설정
	float EdgeThreshold = 0.125f;
	float EdgeThresholdMin = 0.0625f;

	// Light Culling 뷰모드 전용 설정
	ELightCullingMode LightCullingMode = ELightCullingMode::Cluster;
	float HeatMapMax = 20.0f;
	bool Enable25DCulling = true;

	// Shadow debug/control 설정
	bool bOverrideCameraWithSelectedLight = false;
	uint32 PointLightPreviewFaceIndex = 0;
	int32 ShadowMapResolution = 2048;
	EShadowFilterMode ShadowFilterMode = EShadowFilterMode::PCF;
	EShadowMethod ShadowMethod = EShadowMethod::Standard;
	FShadowAtlasPrioritySettings ShadowAtlasPriority;
};
