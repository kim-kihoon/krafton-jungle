#pragma once

//	Windows API Include
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>

//	D3D API Include
#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>

#pragma comment(lib, "dxgi")
#include "Core/CoreTypes.h"
#include "Math/Vector.h"

//	Mesh Shape Enum — MeshBufferManager 조회용 (순수 기하 형상)
enum class EMeshShape
{
	Cube,
	Sphere,
	Plane,
	Quad,
	TransGizmo,
	RotGizmo,
	ScaleGizmo,
	Arrow,
};

enum class ERenderPass : uint32
{
	Opaque,
	Decal,
	MeshDecal,
	FireBall,
	Font,
	SubUV,
	Translucent,
	Fog,
	SceneDepth,
	Editor,
	Grid,
	SelectionMask,
	PostProcess,
	Billboard,		// 아이콘 (그리드 위, 기즈모 아래)
	FXAA,
	GizmoOuter,
	GizmoInner,
	OverlayFont,
	MAX
};

inline const char* GetRenderPassName(ERenderPass Pass)
{
	static const char* Names[] = {
		"RenderPass::Opaque",
		"RenderPass::Decal",
		"RenderPass::MeshDecal",
		"RenderPass::FireBall",
		"RenderPass::Font",
		"RenderPass::SubUV",
		"RenderPass::Translucent",
		"RenderPass::Fog",
		"RenderPass::SceneDepth",
		"RenderPass::Editor",
		"RenderPass::Grid",
		"RenderPass::SelectionMask",
		"RenderPass::PostProcess",
		"RenderPass::Billboard",
		"RenderPass::FXAA",
		"RenderPass::GizmoOuter",
		"RenderPass::GizmoInner",
		"RenderPass::OverlayFont",
	};
	static_assert(ARRAYSIZE(Names) == (uint32)ERenderPass::MAX, "Names must match ERenderPass entries");
	return Names[(uint32)Pass];
}
