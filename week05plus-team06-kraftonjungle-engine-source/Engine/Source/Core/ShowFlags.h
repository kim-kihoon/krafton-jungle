#pragma once
#include "CoreMinimal.h"
#include "EngineAPI.h"
enum class EEngineShowFlags : uint64
{
	SF_Primitives = 1 << 0,
	SF_UUID = 1 << 1,
	SF_DebugDraw = 1 <<2,
	SF_WorldAxis = 1 <<3,
	SF_Collision =1<<4,
	SF_Billboard = 1 << 5,
	SF_Text = 1 << 6,
	SF_Grid = 1<<7,
	 // SF_Grid        = 1 << 3,
	 // SF_Fog         = 1 << 4,
	/** 에디터에서만 보이는 액터 시각화 (카메라 아이콘, 방향 화살표 등).
	 *  FGameViewportClient에서는 비활성화해 PIE에서는 렌더링하지 않는다. */
	SF_EditorActorVisualization = 1 << 8,
};
class ENGINE_API FShowFlags
{
public:
	FShowFlags()
		: Flags(
			static_cast<uint64>(EEngineShowFlags::SF_Primitives) |
			static_cast<uint64>(EEngineShowFlags::SF_UUID) |
			static_cast<uint64>(EEngineShowFlags::SF_Billboard) |
			static_cast<uint64>(EEngineShowFlags::SF_Text) | 
			static_cast<uint64>(EEngineShowFlags::SF_EditorActorVisualization)) {
	}
	void SetFlag(EEngineShowFlags InFlag, bool bEnabled);
	bool HasFlag(EEngineShowFlags InFlag)const;
	void ToggleFlag(EEngineShowFlags InFlag);
private:
	uint64 Flags;
};