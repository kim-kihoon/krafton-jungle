#include "Editor/UI/EditorConsoleWidget.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "Editor/EditorEngine.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewport/ViewportLayout.h"
#include "Engine/Object/FName.h"

namespace
{
	constexpr float EditorBottomBarHeight = 28.0f;
	constexpr float ConsoleDrawerAnimationSpeed = 12.0f;

	float EaseOutCubic(float Value)
	{
		Value = std::clamp(Value, 0.0f, 1.0f);
		const float Inv = 1.0f - Value;
		return 1.0f - Inv * Inv * Inv;
	}
}

FEditorConsoleWidget::FEditorConsoleWidget()
{
	FLogger::SetMessage(&FEditorConsoleWidget::AddMessage);

	// 임의의 명령어 문자열이 들어왔을 때 뒤의 함수를 실행하도록 분기한다.
	RegisterCommand("clear", [this](const TArray<FString>& Args) { (void)Args; Clear(); });
	RegisterCommand("stat", [this](const TArray<FString>& Args) { CmdStat(Args); });
	RegisterCommand("shadow_filter", [this](const TArray<FString>& Args) { CmdShadowFilter(Args); });
	RegisterCommand("shader_filter", [this](const TArray<FString>& Args) { CmdShadowFilter(Args); });
}

FEditorConsoleWidget::~FEditorConsoleWidget()
{
	FLogger::ClearMessage(&FEditorConsoleWidget::AddMessage);
	Clear();
	ClearHistory();
}

void FEditorConsoleWidget::AddLog(const char* fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	AddMessage(buf);
}

void FEditorConsoleWidget::AddMessage(const char* Message)
{
	Messages.push_back(_strdup(Message));
	if (AutoScroll)
	{
		ScrollToBottom = true;
	}
}

void FEditorConsoleWidget::SetOpen(bool bInOpen)
{
	if (bInOpen && !bOpen)
	{
		bOpenedThisFrame = true;
		bRequestFocusInput = true;
	}
	bOpen = bInOpen;
}

void FEditorConsoleWidget::ToggleOpen()
{
	SetOpen(!bOpen);
}

bool FEditorConsoleWidget::ConsumeOpenRequest()
{
	const bool bResult = bOpenedThisFrame;
	bOpenedThisFrame = false;
	return bResult;
}

bool FEditorConsoleWidget::ConsumeCompactOpenRequest()
{
	const bool bResult = bCompactOpenRequested;
	bCompactOpenRequested = false;
	return bResult;
}

bool FEditorConsoleWidget::ShouldRender() const
{
	return bOpen || DrawerAnimationAlpha > 0.0f;
}

float FEditorConsoleWidget::GetReservedBottomHeight() const
{
	float ReservedDrawerHeight = DrawerHeight;
	if (ReservedDrawerHeight <= 0.0f)
	{
		if (const ImGuiViewport* Viewport = ImGui::GetMainViewport())
		{
			const float AvailableHeight = std::max(120.0f, Viewport->WorkSize.y - EditorBottomBarHeight);
			ReservedDrawerHeight = AvailableHeight * 0.35f;
		}
	}

	const float EasedAlpha = EaseOutCubic(DrawerAnimationAlpha);
	return EditorBottomBarHeight + ReservedDrawerHeight * EasedAlpha;
}

void FEditorConsoleWidget::CloseImmediately()
{
	bOpen = false;
	bOpenedThisFrame = false;
	DrawerAnimationAlpha = 0.0f;
}

void FEditorConsoleWidget::OpenFromDrawerTakeover(float InDrawerHeight)
{
	bOpen = true;
	bOpenedThisFrame = true;
	if (InDrawerHeight > 0.0f)
	{
		DrawerHeight = InDrawerHeight;
	}
	DrawerAnimationAlpha = 1.0f;
	bRequestFocusInput = true;
}

void FEditorConsoleWidget::Render(float DeltaTime)
{
	if (bOpen && ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
	{
		SetOpen(false);
	}

	const float TargetAlpha = bOpen ? 1.0f : 0.0f;
	const float AlphaStep = std::clamp(DeltaTime * ConsoleDrawerAnimationSpeed, 0.0f, 1.0f);
	DrawerAnimationAlpha += (TargetAlpha - DrawerAnimationAlpha) * AlphaStep;

	if (!bOpen && DrawerAnimationAlpha < 0.001f)
	{
		DrawerAnimationAlpha = 0.0f;
	}
	else if (bOpen && DrawerAnimationAlpha > 0.999f)
	{
		DrawerAnimationAlpha = 1.0f;
	}

	if (!ShouldRender())
	{
		return;
	}

	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	const ImVec2 WorkPos = Viewport->WorkPos;
	const ImVec2 WorkSize = Viewport->WorkSize;
	const float AvailableHeight = std::max(120.0f, WorkSize.y - EditorBottomBarHeight);
	const float MinHeight = std::min(220.0f, AvailableHeight * 0.8f);
	const float MaxHeight = std::max(MinHeight, AvailableHeight * 0.82f);

	if (DrawerHeight <= 0.0f)
	{
		DrawerHeight = AvailableHeight * 0.35f;
	}
	DrawerHeight = std::clamp(DrawerHeight, MinHeight, MaxHeight);

	const float EasedAlpha = EaseOutCubic(DrawerAnimationAlpha);
	const float ClosedY = WorkPos.y + WorkSize.y - EditorBottomBarHeight;
	const float OpenY = ClosedY - DrawerHeight;
	const float AnimatedY = ClosedY + (OpenY - ClosedY) * EasedAlpha;

	ImGui::SetNextWindowViewport(Viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(WorkPos.x, AnimatedY), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(WorkSize.x, DrawerHeight), ImGuiCond_Always);
	if (bRequestFocusInput)
	{
		ImGui::SetNextWindowFocus();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

	constexpr ImGuiWindowFlags WindowFlags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings;

	bool bWindowOpen = bOpen;
	if (!ImGui::Begin("Console", &bWindowOpen, WindowFlags))
	{
		ImGui::End();
		ImGui::PopStyleVar(2);
		if (bOpen && !bWindowOpen)
		{
			SetOpen(false);
		}
		return;
	}

	if (ImGui::BeginPopup("Options"))
	{
		ImGui::Checkbox("Auto-scroll", &AutoScroll);
		ImGui::EndPopup();
	}

	ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_Tooltip);
	if (ImGui::Button("Options"))
	{
		ImGui::OpenPopup("Options");
	}
	ImGui::SameLine();
	Filter.Draw("Filter (\"incl,-excl\") (\"error\")", 180);
	ImGui::Separator();

	const float WindowBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
	const float BottomBarTop = Viewport->WorkPos.y + Viewport->WorkSize.y - EditorBottomBarHeight;
	const float BottomSafePadding = WindowBottom > BottomBarTop ? EditorBottomBarHeight : 0.0f;
	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() + BottomSafePadding;
	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -FooterHeight), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		for (auto& Item : Messages)
		{
			if (!Filter.PassFilter(Item))
			{
				continue;
			}

			ImVec4 Color;
			bool bHasColor = false;
			if (strncmp(Item, "[ERROR]", 7) == 0)
			{
				Color = ImVec4(1, 0.4f, 0.4f, 1);
				bHasColor = true;
			}
			else if (strncmp(Item, "[WARN]", 6) == 0)
			{
				Color = ImVec4(1, 0.8f, 0.2f, 1);
				bHasColor = true;
			}
			else if (strncmp(Item, "#", 1) == 0)
			{
				Color = ImVec4(1, 0.8f, 0.6f, 1);
				bHasColor = true;
			}

			if (bHasColor)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, Color);
			}
			ImGui::TextUnformatted(Item);
			if (bHasColor)
			{
				ImGui::PopStyleColor();
			}
		}

		if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
		{
			ImGui::SetScrollHereY(1.0f);
		}
		ScrollToBottom = false;
	}
	ImGui::EndChild();
	ImGui::Separator();

	if (bRequestFocusInput)
	{
		ImGui::SetKeyboardFocusHere(0);
		bRequestFocusInput = false;
	}

	RenderCommandInput("Input", nullptr, false);

	ImGui::SetItemDefaultFocus();

	ImGui::End();
	ImGui::PopStyleVar(2);

	if (bOpen && !bWindowOpen)
	{
		SetOpen(false);
	}
}

void FEditorConsoleWidget::RenderCompactInput(float Width)
{
	const ImGuiIO& IO = ImGui::GetIO();
	if (!bOpen && ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
	{
		if (bCompactInputActive)
		{
			bCompactOpenRequested = true;
			bRequestFocusInput = true;
		}
		else if (!IO.WantTextInput)
		{
			bRequestFocusCompactInput = true;
		}
	}

	if (Width > 0.0f)
	{
		ImGui::SetNextItemWidth(Width);
	}

	if (bRequestFocusCompactInput)
	{
		ImGui::SetKeyboardFocusHere(0);
		bRequestFocusCompactInput = false;
	}

	RenderCommandInput("##ConsoleCompactInput", "Enter console command", true);
	bCompactInputActive = ImGui::IsItemActive();
}

void FEditorConsoleWidget::RegisterCommand(const FString& Name, CommandFn Fn)
{
	Commands[Name] = Fn;
}

void FEditorConsoleWidget::SubmitInputBuffer()
{
	if (!CompleteSelectedCandidateInBuffer())
	{
		ExecCommand(InputBuf);
		strcpy_s(InputBuf, "");
		CompletionCandidates.clear();
		bCompletionSelectionActive = false;
		CompletionInputSnapshot.clear();
	}
}

bool FEditorConsoleWidget::RenderCommandInput(const char* Label, const char* Hint, bool bUseHint)
{
	ImGuiInputTextFlags Flags = ImGuiInputTextFlags_EnterReturnsTrue
		| ImGuiInputTextFlags_EscapeClearsAll
		| ImGuiInputTextFlags_CallbackHistory
		| ImGuiInputTextFlags_CallbackCompletion
		| ImGuiInputTextFlags_CallbackCharFilter;

	UpdateCompletionCandidates();

	const bool bSubmitted = bUseHint
		? ImGui::InputTextWithHint(Label, Hint ? Hint : "", InputBuf, sizeof(InputBuf), Flags, &TextEditCallback, this)
		: ImGui::InputText(Label, InputBuf, sizeof(InputBuf), Flags, &TextEditCallback, this);

	if (bSubmitted)
	{
		SubmitInputBuffer();
	}

	UpdateCompletionCandidates();
	RenderCompletionCandidates();
	return bSubmitted;
}

void FEditorConsoleWidget::UpdateCompletionCandidates()
{
	const FString Input(InputBuf);
	if (Input == CompletionInputSnapshot)
	{
		return;
	}

	CompletionInputSnapshot = Input;
	CompletionCandidates = GetCompletionCandidates(Input);
	if (SelectedCompletionIndex >= static_cast<int32>(CompletionCandidates.size()))
	{
		SelectedCompletionIndex = 0;
	}
	if (SelectedCompletionIndex < 0)
	{
		SelectedCompletionIndex = 0;
	}
	bCompletionSelectionActive = !CompletionCandidates.empty();
}

TArray<FCompletionCandidate> FEditorConsoleWidget::GetCompletionCandidates(const FString& Input) const
{
	TArray<FCompletionCandidate> Result;
	const char* InputStart = Input.c_str();
	while (*InputStart == ' ')
	{
		++InputStart;
	}

	const FString Prefix(InputStart);
	if (Prefix.empty() || Prefix.find(' ') != FString::npos)
	{
		return Result;
	}

	if (Commands.find(Prefix) != Commands.end())
	{
		return Result;
	}

	for (const auto& Pair : Commands)
	{
		const FString& Name = Pair.first;
		if (Name.rfind(Prefix, 0) == 0)
		{
			Result.push_back({Name, Name});
		}
	}

	std::sort(Result.begin(), Result.end(), [](const FCompletionCandidate& Left, const FCompletionCandidate& Right)
	{
		return Left.CommandName < Right.CommandName;
	});

	constexpr size_t MaxCompletionCandidates = 5;
	if (Result.size() > MaxCompletionCandidates)
	{
		Result.resize(MaxCompletionCandidates);
	}

	return Result;
}

void FEditorConsoleWidget::RenderCompletionCandidates()
{
	if (!bCompletionSelectionActive || CompletionCandidates.empty())
	{
		return;
	}

	const ImVec2 InputMin = ImGui::GetItemRectMin();
	const ImVec2 InputMax = ImGui::GetItemRectMax();
	const float LineHeight = ImGui::GetTextLineHeightWithSpacing();
	const float PanelHeight = static_cast<float>(CompletionCandidates.size()) * LineHeight + 8.0f;
	const ImVec2 PanelMin(InputMin.x, InputMin.y - PanelHeight - 2.0f);
	const ImVec2 PanelMax(InputMax.x, InputMin.y - 2.0f);

	ImDrawList* DrawList = ImGui::GetForegroundDrawList();
	DrawList->AddRectFilled(PanelMin, PanelMax, IM_COL32(21, 21, 21, 245), 4.0f);
	DrawList->AddRect(PanelMin, PanelMax, IM_COL32(36, 36, 36, 240), 4.0f);

	for (int32 Index = 0; Index < static_cast<int32>(CompletionCandidates.size()); ++Index)
	{
		const bool bSelected = Index == SelectedCompletionIndex;
		const ImVec2 TextPos(PanelMin.x + 8.0f, PanelMin.y + 4.0f + static_cast<float>(Index) * LineHeight);
		if (bSelected)
		{
			DrawList->AddRectFilled(
				ImVec2(PanelMin.x + 2.0f, TextPos.y - 1.0f),
				ImVec2(PanelMax.x - 2.0f, TextPos.y + LineHeight - 1.0f),
				IM_COL32(36, 36, 36, 255),
				3.0f);
		}
		DrawList->AddText(TextPos, IM_COL32(214, 214, 214, 255), CompletionCandidates[Index].DisplayText.c_str());
	}
}

bool FEditorConsoleWidget::CompleteSelectedCandidateInBuffer()
{
	if (!bCompletionSelectionActive || CompletionCandidates.empty())
	{
		return false;
	}

	SelectedCompletionIndex = std::clamp(SelectedCompletionIndex, 0, static_cast<int32>(CompletionCandidates.size()) - 1);
	strcpy_s(InputBuf, CompletionCandidates[SelectedCompletionIndex].CommandName.c_str());
	strcat_s(InputBuf, " ");
	CompletionCandidates.clear();
	bCompletionSelectionActive = false;
	CompletionInputSnapshot = InputBuf;
	return true;
}

bool FEditorConsoleWidget::CompleteSelectedCandidateInBuffer(ImGuiInputTextCallbackData* Data)
{
	if (!bCompletionSelectionActive || CompletionCandidates.empty())
	{
		return false;
	}

	SelectedCompletionIndex = std::clamp(SelectedCompletionIndex, 0, static_cast<int32>(CompletionCandidates.size()) - 1);
	const FString CompletedText = CompletionCandidates[SelectedCompletionIndex].CommandName + " ";
	Data->DeleteChars(0, Data->BufTextLen);
	Data->InsertChars(0, CompletedText.c_str());
	strcpy_s(InputBuf, CompletedText.c_str());
	CompletionCandidates.clear();
	bCompletionSelectionActive = false;
	CompletionInputSnapshot = InputBuf;
	return true;
}

void FEditorConsoleWidget::MoveCompletionSelection(int32 Delta)
{
	if (!bCompletionSelectionActive || CompletionCandidates.empty())
	{
		return;
	}

	const int32 Count = static_cast<int32>(CompletionCandidates.size());
	SelectedCompletionIndex = (SelectedCompletionIndex + Delta + Count) % Count;
}

void FEditorConsoleWidget::ExecCommand(const char* CommandLine)
{
	AddLog("# %s\n", CommandLine);
	History.push_back(_strdup(CommandLine));
	HistoryPos = -1;

	TArray<FString> Tokens;
	std::istringstream Iss(CommandLine);
	FString Token;
	while (Iss >> Token)
	{
		Tokens.push_back(Token);
	}
	if (Tokens.empty())
	{
		return;
	}

	auto It = Commands.find(Tokens[0]);
	if (It != Commands.end())
	{
		It->second(Tokens);
	}
	else
	{
		AddLog("[ERROR] Unknown command: '%s'\n", Tokens[0].c_str());
	}
}

int32 FEditorConsoleWidget::TextEditCallback(ImGuiInputTextCallbackData* Data)
{
	FEditorConsoleWidget* Console = static_cast<FEditorConsoleWidget*>(Data->UserData);

	if (Data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
	{
		if (Data->EventChar == L'`')
		{
			return 1;
		}
		return 0;
	}

	if (Data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
	{
		Console->UpdateCompletionCandidates();
		if (Console->bCompletionSelectionActive && !Console->CompletionCandidates.empty())
		{
			if (Data->EventKey == ImGuiKey_UpArrow)
			{
				Console->MoveCompletionSelection(-1);
				return 0;
			}
			if (Data->EventKey == ImGuiKey_DownArrow)
			{
				Console->MoveCompletionSelection(1);
				return 0;
			}
		}

		const int32 PrevPos = Console->HistoryPos;
		if (Data->EventKey == ImGuiKey_UpArrow)
		{
			if (Console->HistoryPos == -1)
			{
				Console->HistoryPos = Console->History.Size - 1;
			}
			else if (Console->HistoryPos > 0)
			{
				Console->HistoryPos--;
			}
		}
		else if (Data->EventKey == ImGuiKey_DownArrow)
		{
			if (Console->HistoryPos != -1 &&
				++Console->HistoryPos >= Console->History.Size)
			{
				Console->HistoryPos = -1;
			}
		}

		if (PrevPos != Console->HistoryPos)
		{
			const char* HistoryStr = (Console->HistoryPos >= 0)
				? Console->History[Console->HistoryPos] : "";
			Data->DeleteChars(0, Data->BufTextLen);
			Data->InsertChars(0, HistoryStr);
		}
	}

	if (Data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
	{
		Console->UpdateCompletionCandidates();
		Console->CompleteSelectedCandidateInBuffer(Data);
	}

	return 0;
}

void FEditorConsoleWidget::CmdStat(const TArray<FString>& Args)
{
	if (Args.size() < 2)
	{
		AddLog("[WARN] Usage: stat <fps|memory|nametable|lightcull|shadow|none>\n");
		return;
	}

	if (!EditorEngine)
	{
		return;
	}

	FString Target = Args[1];
	std::transform(Target.begin(), Target.end(), Target.begin(), ::tolower);

	FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
	const int32 FocusedIdx  = Layout.GetLastFocusedViewportIndex();

	if (Target == "fps")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatFPS;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::FPS, bFlag);
		AddLog("Stat FPS %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "memory")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatMemory;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::Memory, bFlag);
		AddLog("Stat Memory %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "nametable")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatNameTable;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::NameTable, bFlag);
		AddLog("Stat NameTable %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "lightcull")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatLightCull;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::LightCull, bFlag);
		AddLog("Stat LightCull %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "shadow")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatShadow;
		bFlag = !bFlag;
		Layout.GetViewportState(FocusedIdx).UpdateStatOrder(EStatType::Shadow, bFlag);
		AddLog("Stat Shadow %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "shadowatlas")
	{
		bool& bFlag = Layout.GetViewportState(FocusedIdx).bShowStatShadowAtlas;
		bFlag = !bFlag;
		AddLog("Stat ShadowAtlas %s (viewport %d)\n", bFlag ? "Enabled" : "Disabled", FocusedIdx);
	}
	else if (Target == "none")
	{
		for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
		{
			auto& VS = Layout.GetViewportState(i);
			VS.bShowStatFPS = false;
			VS.bShowStatMemory = false;
			VS.bShowStatNameTable = false;
			VS.bShowStatLightCull = false;
			VS.bShowStatShadow = false;
			VS.bShowStatShadowAtlas = false;
			VS.ActiveStatOrder.clear();
		}
		AddLog("All Stats Disabled\n");
	}
}

void FEditorConsoleWidget::CmdShadowFilter(const TArray<FString>& Args)
{
	if (Args.size() < 2)
	{
		AddLog("[WARN] Usage: shadow_filter <pcf|vsm|esm>\n");
		return;
	}

	if (!EditorEngine)
	{
		return;
	}

	FString Target = Args[1];
	std::transform(Target.begin(), Target.end(), Target.begin(), ::tolower);
	FEditorSettings& Settings = EditorEngine->GetSettings();

	if (Target == "pcf")
	{
		Settings.ShadowFilterType = EShadowFilterType::PCF;
		AddLog("Shadow filter set to PCF\n");
	}
	else if (Target == "vsm")
	{
		Settings.ShadowFilterType = EShadowFilterType::VSM;
		AddLog("Shadow filter set to VSM\n");
	}
	else if (Target == "esm")
	{
		Settings.ShadowFilterType = EShadowFilterType::ESM;
		AddLog("Shadow filter set to ESM\n");
	}
	else
	{
		AddLog("[WARN] Usage: shadow_filter <pcf|vsm|esm>\n");
	}
}

ImVector<char*> FEditorConsoleWidget::Messages;
ImVector<char*> FEditorConsoleWidget::History;

bool FEditorConsoleWidget::AutoScroll = true;
bool FEditorConsoleWidget::ScrollToBottom = true;
