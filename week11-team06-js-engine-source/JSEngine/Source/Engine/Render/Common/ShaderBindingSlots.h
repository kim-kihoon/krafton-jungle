#pragma once

#include "Core/CoreTypes.h"

namespace ShaderBindingSlots
{
	constexpr uint32 FrameCB = 0;
	constexpr uint32 PerObjectCB = 1;

	// b2는 pass 또는 shader family별 재사용: material, gizmo, outline 등
	constexpr uint32 UberCB = 3;
	constexpr uint32 ShadowCB = 4;
	// b5 ~ b8은 현재 shared VS/PS 경로에서 pass-local 또는 미사용
	constexpr uint32 FogCB = 9;
	// b10 ~ b13은 pass별 재사용: FXAA, post-process, editor picking, overlay, screen overlay 등

	// t0 ~ t4는 pass별 재사용: texture inputs, scene GBuffer inputs, light buffers 등
	// lighting 경로에서는 t4 ~ t6을 light/culling SRV range로 사용
	constexpr uint32 LightsSRV = 4;
	constexpr uint32 CulledLightIndicesSRV = 5;
	constexpr uint32 LightTilesSRV = 6;
	constexpr uint32 LightCullingSRVCount = 3;
	// t7 ~ t9는 pass별 재사용: outline mask, decals, decal texture array 등
	constexpr uint32 ShadowMapSRV = 10;
	constexpr uint32 VSMShadowMapSRV = 11;
	constexpr uint32 PointShadowCubeSRV = 12;
	// t13은 현재 shared VS/PS 경로에서 미사용
	constexpr uint32 LightShadowIndicesSRV = 14;
	constexpr uint32 AtlasShadowDataSRV = 15;
	constexpr uint32 ShadowInfoSRVCount = 2;
	constexpr uint32 BoneMatricesSRV = 16;
}
