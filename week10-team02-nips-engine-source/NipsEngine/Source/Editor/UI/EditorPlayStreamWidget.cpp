#include "EditorPlayStreamWidget.h"
#include "Core/ResourceManager.h"
#include "Editor/EditorEngine.h"
#include "ImGui/imgui.h"
#include "Render/Resource/Texture.h"

namespace
{
	bool RenderIconButton(const char* Id, UTexture* Texture, const ImVec2& ButtonSize, const ImVec2& IconSize, const ImVec4& IconTint)
	{
		if (!Texture || !Texture->GetSRV())
		{
			return false;
		}

		ImGui::InvisibleButton(Id, ButtonSize);
		const bool bClicked = ImGui::IsItemClicked();
		const ImVec2 ButtonMin = ImGui::GetItemRectMin();
		const ImVec2 ButtonMax = ImGui::GetItemRectMax();
		const ImVec2 ButtonCenter(
			(ButtonMin.x + ButtonMax.x) * 0.5f,
			(ButtonMin.y + ButtonMax.y) * 0.5f
		);
		const ImVec2 IconMin(
			ButtonCenter.x - IconSize.x * 0.5f,
			ButtonCenter.y - IconSize.y * 0.5f
		);
		const ImVec2 IconMax(
			ButtonCenter.x + IconSize.x * 0.5f,
			ButtonCenter.y + IconSize.y * 0.5f
		);

		ImVec4 DrawTint = IconTint;
		if (ImGui::IsItemHovered())
		{
			DrawTint.x = (DrawTint.x + 0.12f < 1.0f) ? DrawTint.x + 0.12f : 1.0f;
			DrawTint.y = (DrawTint.y + 0.12f < 1.0f) ? DrawTint.y + 0.12f : 1.0f;
			DrawTint.z = (DrawTint.z + 0.12f < 1.0f) ? DrawTint.z + 0.12f : 1.0f;
		}
		if (ImGui::IsItemActive())
		{
			DrawTint.x *= 0.85f;
			DrawTint.y *= 0.85f;
			DrawTint.z *= 0.85f;
		}

		ImGui::GetWindowDrawList()->AddImage(
			reinterpret_cast<ImTextureID>(Texture->GetSRV()),
			IconMin,
			IconMax,
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f),
			ImGui::ColorConvertFloat4ToU32(DrawTint)
		);

		return bClicked;
	}

	void ShowLastItemTooltip(const char* Title, const char* Description)
	{
		if (!Title || !ImGui::IsItemHovered())
		{
			return;
		}

		ImGui::BeginTooltip();
		ImGui::TextUnformatted(Title);
		if (Description && Description[0] != '\0')
		{
			ImGui::Separator();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
			ImGui::TextUnformatted(Description);
			ImGui::PopTextWrapPos();
		}
		ImGui::EndTooltip();
	}
}

void FEditorPlayStreamWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	PlayIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Play.png");
	PauseIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Pause.png");
	StopIconTexture = FResourceManager::Get().LoadTexture("Asset/Editor/ToolIcons/Stop.png");
}

void FEditorPlayStreamWidget::Render(float DeltaTime)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 0));

	// 그려지는 텍스트의 크기를 1.25배로 키웁니다.
	// (원하는 크기에 맞춰 1.3f, 1.5f 등으로 조절 가능합니다)
	ImGui::SetWindowFontScale(1.1f);

	// 포커스된 뷰포트의 PIE 상태를 기준으로 버튼을 표시합니다.
	EViewportPlayState CurrentState = EditorEngine->GetEditorState();
	bool bIsEditing = (CurrentState == EViewportPlayState::Editing);
	bool bIsPlaying = (CurrentState == EViewportPlayState::Playing);
	bool bIsPaused  = (CurrentState == EViewportPlayState::Paused);

	const char* CurrentPlayPauseLabel = bIsPlaying ? PauseLabel : (bIsPaused ? ResumeLabel : PlayLabel);

	ImVec2 IconSize(24.0f, 24.0f);
	UTexture* CurrentPlayPauseIconTexture = bIsPlaying ? PauseIconTexture : PlayIconTexture;
	const bool bHasPlayPauseIcon = CurrentPlayPauseIconTexture && CurrentPlayPauseIconTexture->GetSRV();
	const bool bHasStopIcon = StopIconTexture && StopIconTexture->GetSRV();
	const ImVec2 IconButtonSize = ImVec2(34.0f, 27.0f);
	const ImVec2 TextButtonSize = ImVec2(86.0f, 27.0f);
	ImVec2 PlayBtnSize = bHasPlayPauseIcon ? IconButtonSize : TextButtonSize;
	ImVec2 StopBtnSize = bHasStopIcon ? IconButtonSize : TextButtonSize;

	float Spacing = ImGui::GetStyle().ItemSpacing.x;
	float TotalWidth = PlayBtnSize.x + StopBtnSize.x + Spacing;

	float screenWidth = ImGui::GetIO().DisplaySize.x;
	if (screenWidth > TotalWidth)
	{
		ImGui::SetCursorPosX((screenWidth - TotalWidth) * 0.5f);
	}

	// --- 1번 버튼 (Play / Pause / Resume) ---
	if (bIsPlaying)
	{
		const ImVec4 IconTint(0.753f, 0.753f, 0.753f, 1.0f);       // #c0c0c0
		ImGui::PushStyleColor(ImGuiCol_Button, IconTint);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.830f, 0.830f, 0.830f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.620f, 0.620f, 0.620f, 1.0f));
		
		bool bPlayPauseClicked = bHasPlayPauseIcon
			? RenderIconButton("PlayPauseBtn", CurrentPlayPauseIconTexture, PlayBtnSize, IconSize, IconTint)
			: ImGui::Button(CurrentPlayPauseLabel, PlayBtnSize);
		ShowLastItemTooltip("Pause", "Pause the current play session and keep the simulation state.");

		if (bPlayPauseClicked)
		{
			EditorEngine->PausePlaySession();
			ImGui::SetWindowFocus(nullptr);
		}
		ImGui::PopStyleColor(3);
	}
	else
	{
		const ImVec4 IconTint(0.545f, 0.761f, 0.290f, 1.0f);       // #8bc24a
		ImGui::PushStyleColor(ImGuiCol_Button, IconTint);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.620f, 0.820f, 0.365f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.420f, 0.620f, 0.220f, 1.0f));
		
		bool bPlayPauseClicked = bHasPlayPauseIcon
			? RenderIconButton("PlayPauseBtn", CurrentPlayPauseIconTexture, PlayBtnSize, IconSize, IconTint)
			: ImGui::Button(CurrentPlayPauseLabel, PlayBtnSize);
		ShowLastItemTooltip(
			bIsPaused ? "Resume" : "Play",
			bIsPaused ? "Resume the paused play session." : "Start playing the focused viewport scene.");

		if (bPlayPauseClicked)
		{
			EditorEngine->StartPlaySession();
		}
		ImGui::PopStyleColor(3);
	}
	
	ImGui::SameLine();

	// --- 2번 버튼 (Stop) ---
	if (bIsEditing) ImGui::BeginDisabled();
	
	const ImVec4 StopIconTint = bIsEditing
		? ImVec4(0.86f, 0.86f, 0.86f, 1.0f)
		: ImVec4(1.0f, 0.251f, 0.251f, 1.0f);       // #ff4040
	ImGui::PushStyleColor(ImGuiCol_Button, StopIconTint);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bIsEditing ? ImVec4(0.92f, 0.92f, 0.92f, 1.0f) : ImVec4(1.0f, 0.333f, 0.333f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, bIsEditing ? ImVec4(0.72f, 0.72f, 0.72f, 1.0f) : ImVec4(0.82f, 0.18f, 0.18f, 1.0f));
	
	bool bStopClicked = bHasStopIcon
		? RenderIconButton("StopBtn", StopIconTexture, StopBtnSize, IconSize, StopIconTint)
		: ImGui::Button(StopLabel, StopBtnSize);
	ShowLastItemTooltip("Stop", "Stop the current play session and return to editing.");

	if (bStopClicked)
	{
		EditorEngine->StopPlaySession();
	}
	ImGui::PopStyleColor(3);

	if (bIsEditing) ImGui::EndDisabled();

	ImGui::SetWindowFontScale(1.0f);
	ImGui::PopStyleVar();
}
