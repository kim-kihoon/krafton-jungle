#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"

// Skinning Mode — CPU/GPU 스키닝 렌더링 경로 선택
enum class ESkinningMode : uint32
{
	CPU = 0,
	GPU = 1,
};

/*
 * 	FRenderFeatureSettings — 렌더 기능 런타임 설정.
 *	콘솔 커맨드 등에서 값을 변경하고, 렌더 파이프라인은 프레임 생성 시
 *	FFrameContext에 스냅샷을 저장한다. 
 */
class FRenderFeatureSettings : public TSingleton<FRenderFeatureSettings>
{
	friend class TSingleton<FRenderFeatureSettings>;

public:
	ESkinningMode GetSkinningMode() const { return SkinningMode; }
	void SetSkinningMode(ESkinningMode InMode) { SkinningMode = InMode; }

private:
	ESkinningMode SkinningMode = ESkinningMode::GPU;
};
