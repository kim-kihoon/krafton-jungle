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
#include "Core/Types/CoreTypes.h"
#include "Render/Types/RenderStateTypes.h"

// 셰이더 텍스처 바인딩 리플렉션 결과 (머티리얼 슬롯 t0~t7).
// Name = HLSL 텍스처 변수명(예: "DiffuseTexture"), BindPoint = register t#.
struct FShaderTextureBinding
{
	FString Name;
	uint32  BindPoint = 0;
};

//	Mesh Shape Enum — MeshBufferManager 조회용 (순수 기하 형상)
enum class EMeshShape
{
	Cube,
	Sphere,
	Plane,
	Quad,
	TexturedQuad,
	TransGizmo,
	RotGizmo,
	ScaleGizmo,
};

enum class ERenderPass : uint32
{
	PreDepth,		// Depth-only 프리패스 (color write 없음, Early-Z용)
	LightCulling,	// 라이트 컬링 CS 디스패치 (Tile/Cluster)
	ShadowMap,		// 라이트별 Shadow Depth 렌더링
	Opaque,			// 불투명 지오메트리 (StaticMesh 등)
	Decal,			// 데칼 (DepthReadOnly)
	AdditiveDecal,	// Additive 빌보드 등
	ViewModeMesh,	// 순수 mesh debug viewmode 및 debug overlay
	Fog,			// HeightFog 풀스크린 — Transparent 前(불투명/하늘만 fog, Transparent는 fog 위에 그려져 안 덮임)
	DoFSetup,		// Depth -> CoC setup — Transparent CoC 보정 전 불투명 CoC 생성
	Transparent,	// 통합 Transparent 패스 (Font, SubUV, Billboard, Particle) — Blend는 per-DrawCommand가 결정
	DoFBackgroundBlur, // SceneColor + positive CoC background layer
	DoFForegroundBlur, // SceneColor + negative CoC foreground layer
	DoFBokehScatter, // Bright large-CoC highlight bokeh layer
	DoF,			// SceneColor + CoC composite
	DebugViewModeResolve, // 순수 fullscreen debug viewmode resolve
	SelectionMask,	// 선택 스텐실 마스크
	EditorLines,	// 디버그 라인 + 그리드 (LINELIST)
	PostProcess,	// 아웃라인 등 일반 fullscreen postprocess
	FXAA,			// FXAA 안티앨리어싱 (SceneColor 복사 후 실행)
	EditorIcon,		// 에디터 아이콘 빌보드 오버레이 (포스트프로세스 이후, NoDepth/AlphaBlend — 항상 위)
	GizmoOuter,		// 기즈모 외곽 (깊이 테스트 O)
	GizmoInner,		// 기즈모 내부 (깊이 무시)
	OverlayFont,	// 스크린 공간 텍스트 (깊이 무시)
	UI,				// RmlUi 기반 게임 UI
	GammaCorrection,// 최종 선형 SceneColor를 디스플레이용 감마 공간으로 변환
	MAX
};

inline const char* GetRenderPassName(ERenderPass Pass)
{
	static const char* Names[] = {
		"RenderPass::PreDepth",
		"RenderPass::LightCulling",
		"RenderPass::ShadowMap",
		"RenderPass::Opaque",
		"RenderPass::Decal",
		"RenderPass::AdditiveDecal",
		"RenderPass::ViewModeMesh",
		"RenderPass::Fog",
		"RenderPass::DoFSetup",
		"RenderPass::Transparent",
		"RenderPass::DoFBackgroundBlur",
		"RenderPass::DoFForegroundBlur",
		"RenderPass::DoFBokehScatter",
		"RenderPass::DoF",
		"RenderPass::DebugViewModeResolve",
		"RenderPass::SelectionMask",
		"RenderPass::EditorLines",
		"RenderPass::PostProcess",
		"RenderPass::FXAA",
		"RenderPass::EditorIcon",
		"RenderPass::GizmoOuter",
		"RenderPass::GizmoInner",
		"RenderPass::OverlayFont",
		"RenderPass::UI",
		"RenderPass::GammaCorrection",
	};
    static_assert(std::size(Names) == static_cast<uint32>(ERenderPass::MAX), "Names must match ERenderPass entries");
	return Names[(uint32)Pass];
}

namespace RenderStateStrings
{
	inline constexpr FEnumEntry RenderPassMap[] =
	{
		{ "PreDepth",      (int)ERenderPass::PreDepth },
		{ "LightCulling",  (int)ERenderPass::LightCulling },
		{ "ShadowMap",     (int)ERenderPass::ShadowMap },
		{ "Opaque",        (int)ERenderPass::Opaque },
		{ "Decal",         (int)ERenderPass::Decal },
		{ "AdditiveDecal", (int)ERenderPass::AdditiveDecal },
		{ "ViewModeMesh",  (int)ERenderPass::ViewModeMesh },
		{ "Fog",           (int)ERenderPass::Fog },
		{ "DoFSetup",      (int)ERenderPass::DoFSetup },
		{ "Transparent",   (int)ERenderPass::Transparent },
		{ "DoFBackgroundBlur", (int)ERenderPass::DoFBackgroundBlur },
		{ "DoFForegroundBlur", (int)ERenderPass::DoFForegroundBlur },
		{ "DoFBokehScatter", (int)ERenderPass::DoFBokehScatter },
		{ "DoF",           (int)ERenderPass::DoF },
		{ "DebugViewModeResolve", (int)ERenderPass::DebugViewModeResolve },
		{ "SelectionMask", (int)ERenderPass::SelectionMask },
		{ "EditorLines",   (int)ERenderPass::EditorLines },
		{ "PostProcess",   (int)ERenderPass::PostProcess },
		{ "FXAA",          (int)ERenderPass::FXAA },
		{ "EditorIcon",    (int)ERenderPass::EditorIcon },
		{ "GizmoOuter",    (int)ERenderPass::GizmoOuter },
		{ "GizmoInner",    (int)ERenderPass::GizmoInner },
		{ "OverlayFont",   (int)ERenderPass::OverlayFont },
		{ "UI",            (int)ERenderPass::UI },
		{ "GammaCorrection",(int)ERenderPass::GammaCorrection },
	};

    static_assert(std::size(RenderPassMap) == static_cast<int>(ERenderPass::MAX), "RenderPassMap must match ERenderPass entries");
}
