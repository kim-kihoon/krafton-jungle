#pragma once
#include "Core/CoreMinimal.h"
#include "Viewport/ViewportRect.h"
#include "Render/Common/ViewTypes.h"

class UMovementComponent;

// Editor Widget에서 공통적으로 사용될 수 있는 잡다한 UI 관련 상수들을 정의합니다.
namespace UIConstants
{
	constexpr float XButtonSize     = 20.0f;
	constexpr float TreeRightMargin = 24.0f; // X버튼이 위치할 우측 여백
	constexpr float ClipMargin      = 28.0f; // 버튼(20) + 여백(8)
	constexpr float MinScrollHeight = 50.0f;
}

// Editor Widget에서 공통적으로 사용될 수 있는 잡다한 UI 함수들을 정의합니다.
namespace EditorUIUtils
{
    bool DrawXButton(const char* id, float size = UIConstants::XButtonSize);
    void MakeXButtonId(char* OutBuf, size_t BufSize, const void* Ptr);
    bool RenderStringComboOrInput(const char* Label, FString& Value, const TArray<FString>& Options);
    FString GetMovementComponentDisplayName(UMovementComponent* MoveComp);
}

// 뷰포트별 PIE 재생 상태를 나타냅니다.
// UEditorEngine::GetEditorState() / SetEditorState() 는 포커스된 뷰포트의 이 값을 읽고 씁니다.
enum class EViewportPlayState : uint8
{
    Editing,  // 편집 모드
    Playing,  // PIE 실행 중
    Paused,   // PIE 일시정지
};

enum class EStatType
{
	FPS,
	Memory,
	NameTable,
	LightCull,
	Shadow,
	ShadowAtlas,
	Count
};

struct FEditorViewportState
{
	EViewMode ViewMode = EViewMode::Lit;
	bool bHovered = false;

	// Stat Overlay (뷰포트별 독립 제어)
	bool bShowStatFPS        = false;
	bool bShowStatMemory     = false;
	bool bShowStatNameTable  = false;
	bool bShowStatLightCull  = false;
	bool bShowStatShadow     = false;
	bool bShowStatShadowAtlas = false;

	// 활성화된 통계의 순서를 기록 (최대 4개 유지용)
	std::vector<EStatType> ActiveStatOrder;

	void UpdateStatOrder(EStatType Type, bool bEnabled)
	{
		auto it = std::find(ActiveStatOrder.begin(), ActiveStatOrder.end(), Type);
		if (bEnabled)
		{
			if (it == ActiveStatOrder.end())
			{
				ActiveStatOrder.push_back(Type);
			}

			// 4개 초과 시 가장 예전에 켠 것(가장 처음 것)을 끔
			if (ActiveStatOrder.size() > 4)
			{
				EStatType ToDisable = ActiveStatOrder.front();
				ActiveStatOrder.erase(ActiveStatOrder.begin());
				SetStatEnabled(ToDisable, false);
			}
		}
		else
		{
			if (it != ActiveStatOrder.end())
			{
				ActiveStatOrder.erase(it);
			}
		}
	}

	void SetStatEnabled(EStatType Type, bool bEnabled)
	{
		switch (Type)
		{
		case EStatType::FPS:         bShowStatFPS = bEnabled; break;
		case EStatType::Memory:      bShowStatMemory = bEnabled; break;
		case EStatType::NameTable:   bShowStatNameTable = bEnabled; break;
		case EStatType::LightCull:   bShowStatLightCull = bEnabled; break;
		case EStatType::Shadow:      bShowStatShadow = bEnabled; break;
		case EStatType::ShadowAtlas: bShowStatShadowAtlas = bEnabled; break;
		}
	}

	// NameTable 오버레이 스크롤 오프셋 (휠로 조작)
	int32 NameTableScrollLine = 0;
};
