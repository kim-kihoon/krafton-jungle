#pragma once

/*
    Shader, Constant Buffer 등 렌더링에 필요한 리소스들을 관리하는 Class 입니다.
    Renderer에서 필요한 리소스들을 FRenderResources에 추가하여 관리할 수 있습니다.
*/

#include "Render/Resource/Shader.h"

struct FRenderResources
{
};

enum class ESamplerType
{
	EST_Point,
	EST_Linear,
	EST_Anisotropic,
};

enum class EDepthStencilType
{
	Default,
	DepthReadOnly,
	DepthAlways,
	StencilWrite,
	StencilWriteOnlyEqual,

	// --- 기즈모 전용 ---
	GizmoInside,
	GizmoOutside
};

enum class EBlendType
{
	Opaque,
	AlphaBlend,
	NoColor
};

enum class ERasterizerType
{
	SolidBackCull,
	SolidFrontCull,
	SolidNoCull,
	WireFrame,
	DepthView,
};
