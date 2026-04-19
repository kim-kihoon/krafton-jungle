#include <UI/Panels/SConsolePanel.h>
#include <imgui.h>
#include <cstring>

namespace UI
{
    bool SConsolePanel::Initialize(const FEditorContext& InContext)
    {
        (void)InContext;
        return true;
    }

    void SConsolePanel::Update(const FEditorContext& InContext, float InDeltaTime)
    {
        (void)InContext;
        (void)InDeltaTime;
    }

    void SConsolePanel::Draw(const FEditorContext& InContext)
    {
        if (!ImGui::Begin(GetPanelName()))
        {
            ImGui::End();
            return;
        }

        if (!InContext.ConsoleState)
        {
            ImGui::TextUnformatted("Console unavailable.");
            ImGui::End();
            return;
        }

        if (ImGui::Button("Clear"))
        {
            InContext.ConsoleState->ClearMessages();
        }

        ImGui::Separator();
        if (ImGui::BeginChild("##ConsoleLog", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 8.0f), ImGuiChildFlags_Borders))
        {
            const uint32_t MessageCount = InContext.ConsoleState->MessageCount;
            const uint32_t StartIndex = (InContext.ConsoleState->NextWriteIndex + FConsoleState::MAX_MESSAGES - MessageCount) % FConsoleState::MAX_MESSAGES;
            for (uint32_t MessageOffset = 0; MessageOffset < MessageCount; ++MessageOffset)
            {
                const uint32_t MessageIndex = (StartIndex + MessageOffset) % FConsoleState::MAX_MESSAGES;
                const FConsoleMessage& Message = InContext.ConsoleState->Messages[MessageIndex];

                ImVec4 Color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                if (Message.Severity == EConsoleMessageSeverity::Warning)
                {
                    Color = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);
                }
                else if (Message.Severity == EConsoleMessageSeverity::Error)
                {
                    Color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
                }

                ImGui::TextColored(Color, "%s", Message.Text.data());
            }

            if (InContext.ConsoleState->bAutoScroll)
            {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();

        static char InputBuffer[128] = {};
        if (ImGui::InputText("Input", InputBuffer, IM_ARRAYSIZE(InputBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (InputBuffer[0] != '\0')
            {
                InContext.ConsoleState->PushMessage(InputBuffer, EConsoleMessageSeverity::Info);

                if (std::strcmp(InputBuffer, "clear") == 0)
                {
                    InContext.ConsoleState->ClearMessages();
                }
                else if (std::strcmp(InputBuffer, "stat") == 0)
                {
                    if (InContext.ViewportState)
                    {
                        InContext.ViewportState->bShowStats = true;
                    }
                    InContext.ConsoleState->PushMessage("Stat window shown.", EConsoleMessageSeverity::Info);
                }
                else if (std::strcmp(InputBuffer, "stat toggle") == 0)
                {
                    if (InContext.ViewportState)
                    {
                        InContext.ViewportState->bShowStats = !InContext.ViewportState->bShowStats;
                    }
                    InContext.ConsoleState->PushMessage("Stat window toggled.", EConsoleMessageSeverity::Info);
                }
                else if (std::strcmp(InputBuffer, "stat none") == 0)
                {
                    if (InContext.ViewportState)
                    {
                        InContext.ViewportState->bShowStats = false;
                    }
                    InContext.ConsoleState->PushMessage("Stat window hidden.", EConsoleMessageSeverity::Info);
                }
                else if (std::strcmp(InputBuffer, "stat fps") == 0)
                {
                    if (InContext.ViewportState)
                    {
                        InContext.ViewportState->bShowFPS = !InContext.ViewportState->bShowFPS;                    
                    }
                }
                else
                {
                    InContext.ConsoleState->PushMessage("Unknown command.", EConsoleMessageSeverity::Warning);
                }
            }
            InputBuffer[0] = '\0';
        }

        ImGui::End();
    }

    EEditorPanelType SConsolePanel::GetPanelType() const
    {
        return EEditorPanelType::Console;
    }

    const char* SConsolePanel::GetPanelName() const
    {
        return "Console";
    }
}
