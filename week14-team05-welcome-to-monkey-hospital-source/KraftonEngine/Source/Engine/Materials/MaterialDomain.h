#pragma once

#include "Render/Types/RenderTypes.h"       // ERenderPass
#include "Render/Types/RenderStateTypes.h"  // EBlendState, EDepthStencilState, ERasterizerState (uint8 포함)

// =============================================================================
// 머티리얼 고수준 의도 (UE 스타일 Domain / BlendMode)
//
//   머티리얼은 저수준 렌더상태(Pass/Blend/Depth/Raster)를 직접 들지 않고,
//   Domain + BlendMode 만 선언한다. 엔진이 ResolveMaterialRenderState() 로
//   저수준 상태를 도출한다 — 단일 진실(single source of truth).
//
//   ※ Phase 1: enum + 도출 함수만 추가. 아직 런타임에 연결하지 않는다(동작 불변).
//   ※ Phase 2: UMaterial 이 RenderPass/BlendState 필드 대신 이 도출을 사용하도록 전환.
//
//   도출 규칙은 현재 FMaterialManager::StringTo{BlendState,DepthStencilState,
//   RasterizerState}(Pass 기반 기본값)와 일치시켜 Phase 2 전환 시 동작이 보존되게 한다.
//   (Materials/MaterialManager.cpp:249-317 참조)
// =============================================================================

enum class EMaterialDomain : uint8
{
	Surface,      // 일반 지오메트리 표면 (Opaque/Transparent/Additive/Modulate 는 BlendMode 가 결정)
	PostProcess,  // 풀스크린 후처리
	UI,           // 스크린 공간 UI
	Decal,        // 데칼
	MAX
};

enum class EBlendMode : uint8
{
	Opaque,       // 불투명
	Masked,       // 알파 클립 — 불투명 패스 + 셰이더 clip() (셰이더 측 대응 필요)
	Transparent,  // 알파 블렌드
	Additive,     // 가산 (광원 누적)
	Modulate,     // 곱셈
	MAX
};

// 도출된 저수준 렌더상태 묶음.
struct FMaterialRenderState
{
	ERenderPass        Pass         = ERenderPass::Opaque;
	EBlendState        Blend        = EBlendState::Opaque;
	EDepthStencilState DepthStencil = EDepthStencilState::Default;
	ERasterizerState   Rasterizer   = ERasterizerState::SolidBackCull;
};

// (Domain, BlendMode) → 저수준 렌더상태 도출.
//   Phase 1 에서는 호출되지 않는 dormant 함수다. Depth/Rasterizer 는 결과 Pass 기준으로
//   기존 StringTo* 의 Pass 별 기본값과 동일하게 매핑한다.
inline FMaterialRenderState ResolveMaterialRenderState(EMaterialDomain Domain, EBlendMode Blend)
{
	FMaterialRenderState S;

	// 1. Pass — Domain (+ Blend) 으로 결정
	switch (Domain)
	{
	case EMaterialDomain::PostProcess:
		S.Pass = ERenderPass::PostProcess;
		break;
	case EMaterialDomain::UI:
		S.Pass = ERenderPass::UI;
		break;
	case EMaterialDomain::Decal:
		S.Pass = (Blend == EBlendMode::Additive) ? ERenderPass::AdditiveDecal : ERenderPass::Decal;
		break;
	case EMaterialDomain::Surface:
	default:
		// 불투명/마스크 = Opaque 패스, 그 외 블렌드 = Transparent 패스
		S.Pass = (Blend == EBlendMode::Opaque || Blend == EBlendMode::Masked)
			? ERenderPass::Opaque
			: ERenderPass::Transparent;
		break;
	}

	// 2. Blend — BlendMode 직접 매핑 (Masked 는 불투명 블렌드 + 셰이더 clip())
	switch (Blend)
	{
	case EBlendMode::Transparent: S.Blend = EBlendState::AlphaBlend; break;
	case EBlendMode::Additive:    S.Blend = EBlendState::Additive;   break;
	case EBlendMode::Modulate:    S.Blend = EBlendState::Modulate;   break;
	case EBlendMode::Opaque:
	case EBlendMode::Masked:
	default:                      S.Blend = EBlendState::Opaque;     break;
	}

	// 3. DepthStencil — 결과 Pass 기준.
	//    Transparent 는 depth-write 하지 않는 것이 일반적으로 올바르므로 DepthReadOnly 로 도출한다
	//    (파티클 .mat 의 명시적 DepthReadOnly 와 일치 → 별도 override 불필요. 단 기존 에디터 반투명은
	//     Default→DepthReadOnly 로 동작이 바뀐다 — 의도된 교정).
	switch (S.Pass)
	{
	case ERenderPass::Transparent:
	case ERenderPass::Decal:
	case ERenderPass::AdditiveDecal: S.DepthStencil = EDepthStencilState::DepthReadOnly; break;
	case ERenderPass::PostProcess:   S.DepthStencil = EDepthStencilState::NoDepth;       break;
	default:                         S.DepthStencil = EDepthStencilState::Default;       break;
	}

	// 4. Rasterizer — 결과 Pass 기준.
	//    Blended surface는 UE-style preview/authoring 기준으로 기본 NoCull이다.
	//    Opaque/Masked surface만 기본 BackCull을 유지하고, Two Sided는 별도 raster override로 강제 NoCull한다.
	switch (S.Pass)
	{
	case ERenderPass::Transparent:
	case ERenderPass::Decal:
	case ERenderPass::AdditiveDecal:
	case ERenderPass::PostProcess:
	case ERenderPass::UI:             S.Rasterizer = ERasterizerState::SolidNoCull;   break;
	default:                         S.Rasterizer = ERasterizerState::SolidBackCull; break;
	}

	return S;
}

// 전환적 역매핑 — 기존 .mat 의 (RenderPass, BlendState) 문자열에서 Domain/BlendMode 를 추론한다.
//   Phase 2 에서 .mat 이 아직 Domain/BlendMode 키를 갖지 않을 때 사용. CreateTransient(Gizmo/Decal/
//   Text 등 내부 셰이더 경로)는 이 함수를 쓰지 않고 override 슬롯으로 저수준 상태를 직접 보존한다.
inline void DeriveDomainBlend(ERenderPass Pass, EBlendState Blend, EMaterialDomain& OutDomain, EBlendMode& OutBlend)
{
	switch (Pass)
	{
	case ERenderPass::PostProcess:
		OutDomain = EMaterialDomain::PostProcess; OutBlend = EBlendMode::Opaque; return;
	case ERenderPass::UI:
		OutDomain = EMaterialDomain::UI; OutBlend = EBlendMode::Transparent; return;
	case ERenderPass::Decal:
		OutDomain = EMaterialDomain::Decal;
		OutBlend  = (Blend == EBlendState::Additive)   ? EBlendMode::Additive
		          : (Blend == EBlendState::Modulate)   ? EBlendMode::Modulate
		          : (Blend == EBlendState::AlphaBlend) ? EBlendMode::Transparent
		          : EBlendMode::Opaque;
		return;
	case ERenderPass::AdditiveDecal:
		OutDomain = EMaterialDomain::Decal; OutBlend = EBlendMode::Additive; return;
	case ERenderPass::Transparent:
		OutDomain = EMaterialDomain::Surface;
		OutBlend  = (Blend == EBlendState::Additive) ? EBlendMode::Additive
		          : (Blend == EBlendState::Modulate) ? EBlendMode::Modulate
		          : EBlendMode::Transparent;
		return;
	case ERenderPass::Opaque:
	default:
		OutDomain = EMaterialDomain::Surface;
		OutBlend  = (Blend == EBlendState::Additive)   ? EBlendMode::Additive
		          : (Blend == EBlendState::Modulate)   ? EBlendMode::Modulate
		          : (Blend == EBlendState::AlphaBlend) ? EBlendMode::Transparent
		          : EBlendMode::Opaque;
		return;
	}
}
