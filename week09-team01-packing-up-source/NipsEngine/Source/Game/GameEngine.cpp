#include "GameEngine.h"

#include "Game/Settings/GameSettings.h"
#include "Game/Systems/GameContext.h"
#include "Game/Systems/GameItemDataLoader.h"
#include "Game/Systems/CleaningToolSystem.h"
#include "Game/Systems/ItemSystem.h"
#include "Game/Viewport/GameViewportClient.h"
#include "Game/Render/GameRenderPipeline.h"
#include "Engine/Audio/AudioSystem.h"
#include "Engine/Input/InputRouter.h"
#include "Engine/Serialization/SceneSaveManager.h"
#include "Engine/Core/Paths.h"
#include "Engine/Settings/EngineSettings.h"
#include "Engine/GameFramework/World.h"
#include "Engine/Runtime/WindowsWindow.h"

#include "Game/UI/GameUISystem.h"
#include "Render/Renderer/Renderer.h"

DEFINE_CLASS(UGameEngine, UEngine)
REGISTER_FACTORY(UGameEngine)

UGameEngine::UGameEngine() = default;
UGameEngine::~UGameEngine() = default;

// GameEngine 전역 Logger
static void GameLog(const char* Msg)
{
	OutputDebugStringA("[GameEngine] ");
	OutputDebugStringA(Msg);
	OutputDebugStringA("\n");
}

namespace
{
	const FName GameWorldHandle("GameWorld");
	const FString GameWorldName = "GameWorld";

	void ApplyDefaultContext(FWorldContext& Context)
	{
		Context.WorldType = EWorldType::Game;
		Context.ContextHandle = GameWorldHandle;
		Context.ContextName = GameWorldName;

		if (Context.World)
		{
			Context.World->SetWorldType(EWorldType::Game);
			FWorldSpatialIndex::FMaintenancePolicy& Policy = Context.World->GetSpatialIndex().GetMaintenancePolicy();
			FEngineSettings::Get().ApplyToSpatialPolicy(Policy);
		}
	}

}

void UGameEngine::Init(FWindowsWindow* InWindow)
{
	UEngine::Init(InWindow);

	// DefaultRenderPipeline → GameRenderPipeline 교체 (UI 렌더 포함)
	SetRenderPipeline(std::make_unique<FGameRenderPipeline>(this, GetRenderer()));

	// GameUISystem 초기화 (ImGui 컨텍스트)
	GameUISystem::Get().Init(
		InWindow->GetHWND(),
		GetRenderer().GetFD3DDevice().GetDevice(),
		GetRenderer().GetFD3DDevice().GetDeviceContext()
	);

	FItemSystem& Items = FItemSystem::Get();
	Items.ClearItemData();
	FCleaningToolSystem::Get().ClearToolData();
	FGameItemDataLoader::LoadFromFile("Asset/Data/Items.json", Items);
	GGameContext::Get().Reset();
	FItemSystem::Get().ResetRuntimeState();
	GameUISystem::Get().ResetGameData();
	GameUISystem::Get().SetState(EGameUIState::StartMenu);
	GameUISystem::Get().SetStartGameCallback([this]() { StartMainGame(); });
	GameUISystem::Get().SetExitToTitleCallback([this]() { ExitToTitle(); });
	Items.ResetRuntimeState();

	LoadStartupScene();

	GameViewport = std::make_unique<FGameViewportClient>();
	GameViewport->Initialize(InWindow);
	GameViewport->SetWorld(GetWorld());
	GameUISystem::Get().SetMouseSensitivityChangedCallback([this](float Scale)
	{
		if (GameViewport)
		{
			GameViewport->GetPlayerController().SetRotateSensitivity(0.15f * Scale);
		}
	});
}

void UGameEngine::LoadStartupScene()
{
	const FString ScenePath = FPaths::ToString(FPaths::Combine(FPaths::SceneDir(), GameSettings::StartupSceneName));

	if (LoadGameScene(ScenePath))
	{
		return;
	}

	FWorldContext& DefaultCtx = CreateWorldContext(EWorldType::Game, GameWorldHandle, GameWorldName);
	ApplyDefaultContext(DefaultCtx);
	SetActiveWorld(DefaultCtx.ContextHandle);
}

bool UGameEngine::LoadGameScene(const FString& ScenePath)
{
	FWorldContext Ctx;
	FSceneSaveManager::Load(ScenePath, Ctx, nullptr);

	if (!Ctx.World)
	{
		return false;
	}

	if (GetWorldContextFromHandle(GameWorldHandle))
	{
		DestroyWorldContext(GameWorldHandle);
	}

	ApplyDefaultContext(Ctx);
	WorldList.push_back(Ctx);
	SetActiveWorld(Ctx.ContextHandle);
	return true;
}

void UGameEngine::StartMainGame()
{
	const FString ScenePath = FPaths::ToString(FPaths::Combine(FPaths::SceneDir(), GameSettings::MainSceneName));
	if (!LoadGameScene(ScenePath))
	{
		return;
	}

	if (GameViewport)
	{
		GameViewport->SetWorld(GetWorld());
	}

	GameUISystem::Get().ResetGameData();
	GameUISystem::Get().SetState(EGameUIState::InGame);
	BeginPlay();
	GGameContext::Get().RegisterMapDecals(GetWorld());
	GameUISystem::Get().SetProgress(GGameContext::Get().GetCleanProgress());
}

void UGameEngine::ExitToTitle()
{
	FAudioSystem::Get().StopAll();
	GGameContext::Get().Reset();
	FItemSystem::Get().ResetRuntimeState();
	GameUISystem::Get().ResetGameData();

	LoadStartupScene();
	if (GameViewport)
	{
		GameViewport->SetWorld(GetWorld());
	}

	GameUISystem::Get().SetState(EGameUIState::StartMenu);
	BeginPlay();
}

void UGameEngine::Tick(float DeltaTime)
{
	FInputRouter::TickInputSystem();
	UpdateInputWorldType();
	GameViewport->Tick(DeltaTime);
	WorldTick(DeltaTime);
	if (GameUISystem::Get().GetState() == EGameUIState::InGame)
	{
		GGameContext::Get().RefreshCleanProgressFromDecals();
		GameUISystem::Get().SetProgress(GGameContext::Get().GetCleanProgress());
	}
	GameViewport->LateTick(DeltaTime);
	FAudioSystem::Get().Tick(DeltaTime);
	Render(DeltaTime);
}

void UGameEngine::Shutdown()
{
	FItemSystem::Get().ResetRuntimeState();
	GGameContext::Get().Reset();
	GameUISystem::Get().SetStartGameCallback(nullptr);
	GameUISystem::Get().ClearExitToTitleCallback();
	GameUISystem::Get().SetMouseSensitivityChangedCallback(nullptr);
	GameUISystem::Get().Shutdown();
	GameViewport.reset();
	UEngine::Shutdown();
}

void UGameEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	UEngine::OnWindowResized(Width, Height);

	if (GameViewport)
	{
		GameViewport->SetViewportSize(static_cast<float>(Width), static_cast<float>(Height));
	}
}
	
