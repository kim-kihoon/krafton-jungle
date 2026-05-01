#include "Editor/UI/EditorConsoleWidget.h"
#include "Editor/EditorEngine.h"
#include "Editor/Subsystem/OverlayStatSystem.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Object/Object.h"

#include <algorithm>

// ============================================================
// FConsoleLogOutputDevice
// ============================================================

void FConsoleLogOutputDevice::Write(const char* Msg)
{
	Messages.push_back(_strdup(Msg));
	if (AutoScroll) ScrollToBottom = true;
}

void FConsoleLogOutputDevice::Clear()
{
	for (int32 i = 0; i < Messages.Size; i++) free(Messages[i]);
	Messages.clear();
}

// ============================================================
// FEditorConsoleWidget
// ============================================================

// 기존 코드 호환용 static 래퍼 — UE_LOG로 위임
void FEditorConsoleWidget::AddLog(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	FLogManager::Get().LogV(fmt, args);
	va_end(args);
}

void FEditorConsoleWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);

	// 에디터 콘솔을 로그 출력 디바이스로 등록
	FLogManager::Get().AddOutputDevice(&ConsoleDevice);

	RegisterCommand("clear", [this](const TArray<FString>& Args)
		{
			(void)Args;
			Clear();
		});

	RegisterCommand("obj", [this](const TArray<FString>& Args)
		{
			if (Args.size() < 2)
			{
				AddLog("Usage: obj list [ClassName]\n");
				return;
			}

			if (Args[1] == "list")
			{
				const FString ClassFilter = (Args.size() >= 3) ? Args[2] : "";

				// 클래스별 카운트 + 인스턴스 크기 집계
				struct FClassEntry
				{
					const char* Name;
					size_t      ClassSize;
					uint32      Count;
				};
				TMap<const char*, FClassEntry> ClassMap;

				for (UObject* Obj : GUObjectArray)
				{
					if (!Obj) continue;
					UClass* Cls = Obj->GetClass();
					if (!Cls) continue;

					const char* ClassName = Cls->GetName();
					if (!ClassFilter.empty())
					{
						// 대소문자 무시 부분 매칭
						FString Name(ClassName);
						FString Filter(ClassFilter);
						std::transform(Name.begin(), Name.end(), Name.begin(), ::tolower);
						std::transform(Filter.begin(), Filter.end(), Filter.begin(), ::tolower);
						if (Name.find(Filter) == FString::npos)
							continue;
					}

					auto It = ClassMap.find(ClassName);
					if (It == ClassMap.end())
						ClassMap[ClassName] = { ClassName, Cls->GetSize(), 1 };
					else
						It->second.Count++;
				}

				// 카운트 내림차순 정렬
				TArray<FClassEntry> Sorted;
				for (auto& Pair : ClassMap)
					Sorted.push_back(Pair.second);
				std::sort(Sorted.begin(), Sorted.end(),
					[](const FClassEntry& A, const FClassEntry& B) { return A.Count > B.Count; });

				uint32 TotalCount = 0;
				size_t TotalBytes = 0;

				AddLog("%-35s %8s %10s\n", "Class", "Count", "Size(KB)");
				AddLog("-------------------------------------------------------------\n");
				for (auto& E : Sorted)
				{
					size_t Bytes = E.ClassSize * E.Count;
					TotalCount += E.Count;
					TotalBytes += Bytes;
					AddLog("%-35s %8u %10.1f\n", E.Name, E.Count, Bytes / 1024.0);
				}
				AddLog("-------------------------------------------------------------\n");
				AddLog("%-35s %8u %10.1f\n", "TOTAL", TotalCount, TotalBytes / 1024.0);
				AddLog("GUObjectArray capacity: %zu\n", GUObjectArray.capacity());
			}
			else
			{
				AddLog("[ERROR] Unknown obj subcommand: '%s'\n", Args[1].c_str());
				AddLog("Usage: obj list [ClassName]\n");
			}
		});

	RegisterCommand("stat", [this](const TArray<FString>& Args)
		{
			if (EditorEngine == nullptr)
			{
				AddLog("[ERROR] EditorEngine is null.\n");
				return;
			}

			if (Args.size() < 2)
			{
				AddLog("Usage: stat fps | stat memory | stat light | stat psm [on|off|toggle] | stat none\n");
				return;
			}

			FOverlayStatSystem& StatSystem = EditorEngine->GetOverlayStatSystem();
			const FString& SubCommand = Args[1];

			if (SubCommand == "fps")
			{
				StatSystem.ShowFPS(true);
				AddLog("Overlay stat enabled: fps\n");
			}
			else if (SubCommand == "memory")
			{
				StatSystem.ShowMemory(true);
				AddLog("Overlay stat enabled: memory\n");
			}
			else if (SubCommand == "light") 
			{
				StatSystem.ShowLight(true);
				AddLog("Overlay stat enabled: light\n");
			}
			else if (SubCommand == "psm")
			{
				FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport();
				if (ActiveViewport == nullptr)
				{
					AddLog("[ERROR] No active viewport.\n");
					return;
				}

				FViewportRenderOptions& RenderOptions = ActiveViewport->GetRenderOptions();
				bool bEnable = !RenderOptions.ShowFlags.bPSMDebugLines;
				if (Args.size() >= 3)
				{
					FString Value = Args[2];
					std::transform(Value.begin(), Value.end(), Value.begin(), ::tolower);
					if (Value == "on" || Value == "1" || Value == "true")
					{
						bEnable = true;
					}
					else if (Value == "off" || Value == "0" || Value == "false")
					{
						bEnable = false;
					}
					else if (Value != "toggle")
					{
						AddLog("[ERROR] Unknown stat psm value: '%s'\n", Args[2].c_str());
						AddLog("Usage: stat psm [on|off|toggle]\n");
						return;
					}
				}

				RenderOptions.ShowFlags.bPSMDebugLines = bEnable;
				AddLog("PSM debug lines %s.\n", bEnable ? "enabled" : "disabled");
			}
			else if (SubCommand == "none")
			{
				StatSystem.HideAll();
				AddLog("Overlay stat disabled: all\n");
			}
			else
			{
				AddLog("[ERROR] Unknown stat command: '%s'\n", SubCommand.c_str());
				AddLog("Usage: stat fps | stat memory | stat light | stat psm [on|off|toggle] | stat none\n");
			}
		});

	RegisterCommand("shadow_filter", [this](const TArray<FString>& Args)
		{
			if (EditorEngine == nullptr)
			{
				AddLog("[ERROR] EditorEngine is null.\n");
				return;
			}

			if (Args.size() < 2)
			{
				AddLog("Usage: shadow_filter None | shadow_filter PCF | shadow_filter VSM\n");
				return;
			}

			FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport();
			if (ActiveViewport == nullptr)
			{
				AddLog("[ERROR] No active viewport.\n");
				return;
			}

			FString FilterMode = Args[1];
			std::transform(FilterMode.begin(), FilterMode.end(), FilterMode.begin(), ::toupper);

			FViewportRenderOptions& RenderOptions = ActiveViewport->GetRenderOptions();
			if (FilterMode == "NONE")
			{
				RenderOptions.ShadowFilterMode = EShadowFilterMode::None;
				AddLog("Shadow filter set to None.\n");
			}
			else if (FilterMode == "PCF")
			{
				RenderOptions.ShadowFilterMode = EShadowFilterMode::PCF;
				AddLog("Shadow filter set to PCF.\n");
			}
			else if (FilterMode == "VSM")
			{
				RenderOptions.ShadowFilterMode = EShadowFilterMode::VSM;
				AddLog("Shadow filter set to VSM.\n");
			}
			else
			{
				AddLog("[ERROR] Unknown shadow filter: '%s'\n", Args[1].c_str());
				AddLog("Usage: shadow_filter None | shadow_filter PCF | shadow_filter VSM\n");
			}
		});

	RegisterCommand("shadow_mode", [this](const TArray<FString>& Args)
		{
			if (EditorEngine == nullptr)
			{
				AddLog("[ERROR] EditorEngine is null.\n");
				return;
			}

			if (Args.size() < 2)
			{
				AddLog("Usage: shadow_mode standard | shadow_mode psm | shadow_mode csm\n");
				return;
			}

			FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport();
			if (ActiveViewport == nullptr)
			{
				AddLog("[ERROR] No active viewport.\n");
				return;
			}

			FString ShadowMode = Args[1];
			std::transform(ShadowMode.begin(), ShadowMode.end(), ShadowMode.begin(), ::tolower);

			FViewportRenderOptions& RenderOptions = ActiveViewport->GetRenderOptions();
			if (ShadowMode == "standard")
			{
				RenderOptions.ShadowMethod = EShadowMethod::Standard;
				AddLog("Shadow mode set to Standard.\n");
			}
			else if (ShadowMode == "psm")
			{
				RenderOptions.ShadowMethod = EShadowMethod::PSM;
				AddLog("Shadow mode set to PSM.\n");
			}
			else if (ShadowMode == "csm")
			{
				RenderOptions.ShadowMethod = EShadowMethod::CSM;
				AddLog("Shadow mode set to CSM.\n");
			}
			else
			{
				AddLog("[ERROR] Unknown shadow mode: '%s'\n", Args[1].c_str());
				AddLog("Usage: shadow_mode standard | shadow_mode psm | shadow_mode csm\n");
			}
		});
}

void FEditorConsoleWidget::Shutdown()
{
	FLogManager::Get().RemoveOutputDevice(&ConsoleDevice);
}

void FEditorConsoleWidget::Clear()
{
	ConsoleDevice.Clear();
}

void FEditorConsoleWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console"))
	{
		ImGui::End();
		return;
	}

	if (ImGui::SmallButton("Clear")) { Clear(); }

	ImGui::Separator();

	//// Options menu
	if (ImGui::BeginPopup("Options"))
	{
		ImGui::Checkbox("Auto-scroll", &ConsoleDevice.AutoScroll);
		ImGui::EndPopup();
	}

	// Options, Filter
	ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_Tooltip);
	if (ImGui::Button("Options"))
		ImGui::OpenPopup("Options");
	ImGui::SameLine();
	Filter.Draw("Filter (\"incl,-excl\") (\"error\")", 180);
	ImGui::Separator();

	const float FooterHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -FooterHeight), false, ImGuiWindowFlags_HorizontalScrollbar)) {
		for (int32 i = 0; i < ConsoleDevice.GetMessageCount(); ++i) {
			char* Item = ConsoleDevice.GetMessageAt(i);
			if (!Filter.PassFilter(Item)) continue;

			ImVec4 Color;
			bool bHasColor = false;
			if (strncmp(Item, "[ERROR]", 7) == 0) {
				Color = ImVec4(1, 0.4f, 0.4f, 1);
				bHasColor = true;
			}
			else if (strncmp(Item, "[WARN]", 6) == 0) {
				Color = ImVec4(1, 0.8f, 0.2f, 1);
				bHasColor = true;
			}
			else if (strncmp(Item, "#", 1) == 0) {
				Color = ImVec4(1, 0.8f, 0.6f, 1);
				bHasColor = true;
			}

			if (bHasColor) {
				ImGui::PushStyleColor(ImGuiCol_Text, Color);
			}
			ImGui::TextUnformatted(Item);
			if (bHasColor) {
				ImGui::PopStyleColor();
			}
		}

		if (ConsoleDevice.ScrollToBottom || (ConsoleDevice.AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
			ImGui::SetScrollHereY(1.0f);
		}
		ConsoleDevice.ScrollToBottom = false;
	}
	ImGui::EndChild();
	ImGui::Separator();

	// Input line
	bool bReclaimFocus = false;
	ImGuiInputTextFlags Flags = ImGuiInputTextFlags_EnterReturnsTrue
		| ImGuiInputTextFlags_EscapeClearsAll
		| ImGuiInputTextFlags_CallbackHistory
		| ImGuiInputTextFlags_CallbackCompletion;
	if (ImGui::InputText("Input", InputBuf, sizeof(InputBuf), Flags, &TextEditCallback, this)) {
		ExecCommand(InputBuf);
		strcpy_s(InputBuf, "");
		bReclaimFocus = true;
	}

	ImGui::SetItemDefaultFocus();
	if (bReclaimFocus) {
		ImGui::SetKeyboardFocusHere(-1);
	}

	ImGui::End();
}

void FEditorConsoleWidget::RegisterCommand(const FString& Name, CommandFn Fn) {
	Commands[Name] = Fn;
}

void FEditorConsoleWidget::ExecCommand(const char* CommandLine) {
	AddLog("# %s\n", CommandLine);
	History.push_back(_strdup(CommandLine));
	HistoryPos = -1;

	TArray<FString> Tokens;
	std::istringstream Iss(CommandLine);
	FString Token;
	while (Iss >> Token) Tokens.push_back(Token);
	if (Tokens.empty()) return;

	auto It = Commands.find(Tokens[0]);
	if (It != Commands.end()) {
		It->second(Tokens);
	}
	else {
		AddLog("[ERROR] Unknown command: '%s'\n", Tokens[0].c_str());
	}
}

// History & Tab-Completion Callback____________________________________________________________
int32 FEditorConsoleWidget::TextEditCallback(ImGuiInputTextCallbackData* Data) {
	FEditorConsoleWidget* Console = (FEditorConsoleWidget*)Data->UserData;

	if (Data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
		const int32 PrevPos = Console->HistoryPos;
		if (Data->EventKey == ImGuiKey_UpArrow) {
			if (Console->HistoryPos == -1)
				Console->HistoryPos = Console->History.Size - 1;
			else if (Console->HistoryPos > 0)
				Console->HistoryPos--;
		}
		else if (Data->EventKey == ImGuiKey_DownArrow) {
			if (Console->HistoryPos != -1 &&
				++Console->HistoryPos >= Console->History.Size)
				Console->HistoryPos = -1;
		}
		if (PrevPos != Console->HistoryPos) {
			const char* HistoryStr = (Console->HistoryPos >= 0)
				? Console->History[Console->HistoryPos] : "";
			Data->DeleteChars(0, Data->BufTextLen);
			Data->InsertChars(0, HistoryStr);
		}
	}

	if (Data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
		// Find last word typed
		const char* WordEnd = Data->Buf + Data->CursorPos;
		const char* WordStart = WordEnd;
		while (WordStart > Data->Buf && WordStart[-1] != ' ')
			WordStart--;

		// Collect matches from registered commands
		ImVector<const char*> Candidates;
		for (auto& Pair : Console->Commands) {
			const FString& Name = Pair.first;
			if (strncmp(Name.c_str(), WordStart, WordEnd - WordStart) == 0)
				Candidates.push_back(Name.c_str());
		}

		if (Candidates.Size == 1) {
			Data->DeleteChars(static_cast<int32>(WordStart - Data->Buf), static_cast<int32>(WordEnd - WordStart));
			Data->InsertChars(Data->CursorPos, Candidates[0]);
			Data->InsertChars(Data->CursorPos, " ");
		}
		else if (Candidates.Size > 1) {
			Console->AddLog("Possible completions:\n");
			for (auto& C : Candidates) Console->AddLog("  %s\n", C);
		}
	}

	return 0;
}

ImVector<char*> FEditorConsoleWidget::History;
