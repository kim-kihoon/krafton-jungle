#pragma once

#include "Game/UI/GameUISystem.h"
#include <string>
#include <queue>

struct FDialogueEntry
{
    std::string Speaker;
    std::string Text;
};

// -------------------------------------------------------
// DialoguePanel
//   - 뷰포트 하단 고정 대화창
//   - 타이핑 효과 (글자 하나씩 표시)
//   - 대화 큐 (순서대로 자동 진행)
//   - SPACE: 타이핑 스킵 / 다음 대사 진행
// -------------------------------------------------------
class DialoguePanel
{
public:
    static void Render(EUIRenderMode Mode);
    static void Tick(float DeltaTime, EUIRenderMode Mode);

    // 즉시 표시 (기존 큐 초기화)
    static void Show(const char* Speaker, const char* Text);
    // 큐에 추가 (현재 표시 중이면 순서 대기)
    static void Enqueue(const char* Speaker, const char* Text);
    // 대화창 닫기 (큐 포함)
    static void Hide();
    static bool IsActive();
    static bool AdvanceOrSkip();
    static bool IsTextComplete();
    static const std::string& GetSpeaker();
    static std::string GetVisibleText();

private:
    static void StartEntry(const FDialogueEntry& Entry);
    static void AdvanceQueue();
    static int  TotalCharCount(const std::string& Str);
    static int  CharCountToByteOffset(const std::string& Str, int CharCount);

    static std::queue<FDialogueEntry> PendingQueue;
    static std::string                CurrentSpeaker;
    static std::string                CurrentFullText;
    static int                        VisibleChars;
    static float                      TypeTimer;
    static bool                       bActive;

    // 글자 하나 나타나는 간격 (초)
    static constexpr float CharInterval = 0.04f;
};
