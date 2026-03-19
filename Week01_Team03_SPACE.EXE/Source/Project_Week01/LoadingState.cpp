#include "LoadingState.h"
#include "InGameReadyState.h"
#include "UManager.h"
#include "ImGui/imgui.h"

#include "StageSelectionState.h"

void LoadingState::OnEnter(UManager* manager)
{
    m_currentTimer = 0.0f;
    uiManager = new UIManager();

    //  Loading은 Background를 가지지 않습니다.
    UIFrame & loadingFrame = uiManager->CreateFrame("Loading_load")
        .Position(ImVec2(0, 0))
        .Size(ImVec2(1400, 1050))
        .NoTitleBar(true)
		.BackgroundColor(ImVec4(0, 0, 0, 0));

	LoadingFrame = &loadingFrame;

    auto appendText = manager->GetResourceManager()->GetRandomTips();
    loadingFrame.AddText("[Tips] " + appendText,
        ImVec2(WindowWidth / 2, WindowHeight - 150),
        manager->GetResourceManager()->FontInfoRegular
    );

    loadingFrame.AddSelectableText("LoadingText", "Loading...",
        ImVec2(WindowWidth / 2, WindowHeight / 2),
        manager->GetResourceManager()->FontInfoBold
		);
}

IState* LoadingState::Update(float deltaTime, UManager* manager)
{
    m_currentTimer += deltaTime;
    m_displayTimer += deltaTime;

    if (m_currentTimer >= TOTAL_LOADING_TIME)
    {
        if (manager->GetCurStage() == EStage::ES_None)
        {
			return new StageSelectionState();
        }
        else
        {
            return new InGameReadyState();
        }
    }

    if (m_displayTimer > 0.3f)
    {
        m_displayTimer = 0.0f;
        auto textInfo = LoadingFrame->GetSelectableText("LoadingText");
        if (textInfo)
        {
            if (textInfo->text == "Loading")
            {
                textInfo->text = "Loading.";
            }
            else if (textInfo->text == "Loading.")
            {
                textInfo->text = "Loading..";
            }
			else if (textInfo->text == "Loading..")
            {
                textInfo->text = "Loading...";
            }
            else 
            {
				textInfo->text = "Loading";
            }
		}
    }

    return this;
}

void LoadingState::Render(URenderer* renderer, UManager* manager)
{
    if (uiManager)
    {
        //ImGui::SetNextWindowPos(ImVec2(412, 462));
        //ImGui::Begin("Loading", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);

        //float progress = m_currentTimer / TOTAL_LOADING_TIME;
        //ImGui::Text("Loading Stage...");
        //ImGui::ProgressBar(progress, ImVec2(200, 30), "Please Wait");

        //ImGui::End();

        uiManager->Render();
    }
}

void LoadingState::OnExit(UManager* manager)
{
    if (uiManager)
    {
        delete uiManager;
        uiManager = nullptr;
    }
}