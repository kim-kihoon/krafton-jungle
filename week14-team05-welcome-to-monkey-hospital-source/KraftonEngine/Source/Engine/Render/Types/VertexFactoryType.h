#pragma once

#include "Core/Types/CoreTypes.h"  // uint8

// =============================================================================
// EVertexFactoryType — 드로우 시점의 지오메트리(정점 팩토리) 종류.
//
//   셰이더 도출(FDrawCommandBuilder::ResolveSectionShader)의 축이다. 머티리얼은
//   셰이더를 모르고(shader-agnostic), 엔진이 이 타입 + Domain + Pass + ViewMode 로
//   셰이더를 고른다.
//
//   주의: 메시의 셰이더 정점 팩토리(Static/Skeletal VS)는 실제론 bGPUSkinning 으로
//   결정된다(CPU 스키닝은 이미 static-layout 으로 스킨됨 → VS_StaticMesh). 따라서 이
//   enum 의 StaticMesh/SkeletalMesh 구분은 서술용이고, 핵심 역할은 파티클/빌보드 구분이다.
// =============================================================================
enum class EVertexFactoryType : uint8
{
	Auto = 0,        // 미지정 — 메시 경로에서 bSkeletal/bGPUSkinning 로 로컬 결정
	StaticMesh,
	SkeletalMesh,
	Billboard,
	ParticleSprite,
	ParticleMesh,
	ParticleBeam,
	ParticleRibbon,
	MAX
};
