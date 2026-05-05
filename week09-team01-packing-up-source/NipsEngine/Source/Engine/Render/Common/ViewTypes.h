#pragma once

#include "Core/CoreTypes.h"

// 에디터 UI와 렌더러가 공유하는 view mode 정의다.
// 새 view mode를 추가할 때는 enum만 늘리지 말고 아래 helper 규칙도 함께 확장해야 한다.

enum class EViewMode : int32
{
	Lit = 0,
	Unlit = 1,
	Wireframe = 2,
	SceneDepth = 3,
	WorldNormal = 4,
	CascadeShadow = 5,
	DebugCollision = 6,
	Count = 7
};

enum class EDebugDrawVisibility : int32
{
	UseGlobal = 0,
	ForceShow,
	ForceHide,
	Count
};

inline bool ResolveDebugDrawVisibility(int32 Visibility, bool bGlobalEnabled)
{
	switch (static_cast<EDebugDrawVisibility>(Visibility))
	{
	case EDebugDrawVisibility::ForceShow:
		return true;
	case EDebugDrawVisibility::ForceHide:
		return false;
	case EDebugDrawVisibility::UseGlobal:
	default:
		return bGlobalEnabled;
	}
}

// 버퍼 기반 시각화 모드 분기는 여기로 모아둔다.
// SceneDepth / WorldNormal 외 다른 buffer visualization을 추가할 때 함께 확장하는 지점이다.
inline bool IsBufferVisualizationViewMode(EViewMode ViewMode)
{
	return ViewMode == EViewMode::SceneDepth || ViewMode == EViewMode::WorldNormal;
}

// composite pass 우회 규칙도 공용 helper에서 관리한다.
// 새 view mode가 decal/fog/fxaa를 건너뛰어야 하면 각 패스를 따로 늘리지 말고 여기서 정의한다.
inline bool ShouldBypassSceneCompositePasses(EViewMode ViewMode)
{
	return ViewMode == EViewMode::Wireframe || IsBufferVisualizationViewMode(ViewMode);
}

struct FShowFlags
{
	bool bPrimitives = true;
	bool bGrid = false;
	bool bAxis = false;
	bool bGizmo = false;
	bool bBillboardText = false;
	bool bBoundingVolume = false;
	bool bBVHBoundingVolume = false;
	bool bAudioRange = false;
	bool bAudioComponentRange = false;
	bool bAudioZoneRange = false;
	bool bEnableLOD = true;
	bool bDecals = true;
	bool bFog = true;
	bool bShadow = true;
};
