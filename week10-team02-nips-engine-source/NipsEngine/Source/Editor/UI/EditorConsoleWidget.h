#pragma once
#include "Core/CoreMinimal.h"
#include "Core/Logger.h"
#include <cstdarg>
#include <functional>
#include <sstream>

#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include "Editor/UI/EditorWidget.h"

struct FCompletionCandidate
{
	FString CommandName;
	FString DisplayText;
};

class FEditorConsoleWidget : public FEditorWidget
{
public:
	FEditorConsoleWidget();
	~FEditorConsoleWidget();

	static void AddLog(const char* fmt, ...);
	static void AddMessage(const char* Message);

	virtual void Render(float DeltaTime) override;

	void SetOpen(bool bInOpen);
	bool IsOpen() const { return bOpen; }
	void ToggleOpen();
	bool ConsumeOpenRequest();
	bool ConsumeCompactOpenRequest();
	bool ShouldRender() const;
	void CloseImmediately();
	void OpenFromDrawerTakeover(float InDrawerHeight);
	float GetReservedBottomHeight() const;
	void RenderCompactInput(float Width);

	void Clear()
	{
		for (int32 i = 0; i < Messages.Size; i++) free(Messages[i]);
		Messages.clear();
	}
	static void ClearHistory()
	{
		for (int32 i = 0; i < History.Size; i++) free(History[i]);
		History.clear();
	}

private:
	bool bOpen = false;
	bool bOpenedThisFrame = false;
	float DrawerHeight = 0.0f;
	float DrawerAnimationAlpha = 0.0f;
	char InputBuf[256]{};
	static ImVector<char*> Messages;
	static ImVector<char*> History;
	int32 HistoryPos = -1;
	ImGuiTextFilter Filter;
	static bool AutoScroll;
	static bool ScrollToBottom;
	TArray<FCompletionCandidate> CompletionCandidates;
	int32 SelectedCompletionIndex = 0;
	bool bCompletionSelectionActive = false;
	FString CompletionInputSnapshot;

	// 백틱(`) 키로 포커스 요청 시 true — 다음 InputText 렌더링 직전에 SetKeyboardFocusHere 호출
	bool bRequestFocusInput = false;
	bool bRequestFocusCompactInput = false;
	bool bCompactInputActive = false;
	bool bCompactOpenRequested = false;

	//Command Dispatch System
	using CommandFn = std::function<void(const TArray<FString>& args)>;
	TMap<FString, CommandFn> Commands;

	void RegisterCommand(const FString& Name, CommandFn Fn);
	void ExecCommand(const char* CommandLine);
	void SubmitInputBuffer();
	bool RenderCommandInput(const char* Label, const char* Hint, bool bUseHint);
	static int32 TextEditCallback(ImGuiInputTextCallbackData* Data);
	void UpdateCompletionCandidates();
	TArray<FCompletionCandidate> GetCompletionCandidates(const FString& Input) const;
	void RenderCompletionCandidates();
	bool CompleteSelectedCandidateInBuffer();
	bool CompleteSelectedCandidateInBuffer(ImGuiInputTextCallbackData* Data);
	void MoveCompletionSelection(int32 Delta);

private:
	void CmdStat(const TArray<FString>& Args);
	void CmdShadowFilter(const TArray<FString>& Args);
};

