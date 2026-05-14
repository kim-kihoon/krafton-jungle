#include "Editor/UI/EditorContentDrawerWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/UI/EditorConsoleWidget.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Viewport/ViewportLayout.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "ImGui/imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace
{
	constexpr float ContentDrawerAnimationSpeed = 12.0f;
	constexpr float ContentDrawerBottomBarHeight = 28.0f;

	constexpr const char* AssetKindLabels[] = {
		"All",
		"StaticMesh",
		"SkeletalMesh",
		"Material",
		"Texture",
		"Particle",
		"Font",
		"Scene",
		"Other"
	};

	static_assert(IM_ARRAYSIZE(AssetKindLabels) == static_cast<int32>(EEditorAssetKind::Other) + 1);

	FString NormalizeAssetPath(FString Path)
	{
		std::replace(Path.begin(), Path.end(), '\\', '/');
		while (Path.rfind("./", 0) == 0)
		{
			Path.erase(0, 2);
		}
		return Path;
	}

	FString ToLowerString(FString Value)
	{
		std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Ch)
		{
			return static_cast<char>(std::tolower(Ch));
		});
		return Value;
	}

	FString ToLowerPathKey(const FString& Path)
	{
		return ToLowerString(NormalizeAssetPath(Path));
	}

	std::unordered_set<FString> MakePathSet(const TArray<FString>& Paths)
	{
		std::unordered_set<FString> Result;
		Result.reserve(Paths.size());
		for (const FString& Path : Paths)
		{
			Result.insert(ToLowerPathKey(Path));
		}
		return Result;
	}

	FString MakeRelativeAssetPath(const std::filesystem::path& Path)
	{
		return NormalizeAssetPath(FPaths::ToRelativeString(Path.generic_wstring()));
	}

	FString GetDirectoryPath(const FString& RelativePath)
	{
		const FString NormalizedPath = NormalizeAssetPath(RelativePath);
		const size_t SlashPos = NormalizedPath.find_last_of('/');
		if (SlashPos == FString::npos)
		{
			return "Asset";
		}
		return NormalizedPath.substr(0, SlashPos);
	}

	FString GetParentFolderPath(const FString& FolderPath)
	{
		const FString NormalizedPath = NormalizeAssetPath(FolderPath);
		const size_t SlashPos = NormalizedPath.find_last_of('/');
		if (SlashPos == FString::npos)
		{
			return "";
		}
		return NormalizedPath.substr(0, SlashPos);
	}

	FString GetDisplayNameFromPath(const std::filesystem::path& Path)
	{
		return FPaths::ToUtf8(Path.stem().wstring());
	}

	FString GetExtensionFromPath(const std::filesystem::path& Path)
	{
		return ToLowerString(FPaths::ToUtf8(Path.extension().wstring()));
	}

	bool IsImageExtension(const FString& Extension)
	{
		return Extension == ".png" || Extension == ".jpg" || Extension == ".jpeg" || Extension == ".dds";
	}

	bool IsMaterialExtension(const FString& Extension)
	{
		return Extension == ".mat" || Extension == ".matinst";
	}

	bool ShouldSkipAssetFile(const std::filesystem::path& Path, const FString& Extension)
	{
		if (Extension == ".meta")
		{
			return true;
		}

		const FString FileName = ToLowerString(FPaths::ToUtf8(Path.filename().wstring()));
		return FileName.ends_with(".skelmat.json");
	}

	bool IsHiddenAssetFolder(const FString& RelativeFolderPath)
	{
		const FString PathKey = ToLowerPathKey(RelativeFolderPath);
		return PathKey == "asset/data" ||
			PathKey.starts_with("asset/data/") ||
			PathKey == "asset/editor" ||
			PathKey.starts_with("asset/editor/") ||
			PathKey == "asset/font" ||
			PathKey.starts_with("asset/font/") ||
			PathKey == "asset/shadercache" ||
			PathKey.starts_with("asset/shadercache/") ||
			PathKey == "asset/shadercash" ||
			PathKey.starts_with("asset/shadercash/");
	}

	const char* ToAssetKindLabel(EEditorAssetKind Kind)
	{
		const int32 Index = static_cast<int32>(Kind);
		if (Index < 0 || Index >= static_cast<int32>(IM_ARRAYSIZE(AssetKindLabels)))
		{
			return "Other";
		}
		return AssetKindLabels[Index];
	}

	ImU32 GetAssetKindColor(EEditorAssetKind Kind)
	{
		switch (Kind)
		{
		case EEditorAssetKind::StaticMesh:
			return IM_COL32(70, 125, 210, 255);
		case EEditorAssetKind::SkeletalMesh:
			return IM_COL32(165, 88, 190, 255);
		case EEditorAssetKind::Material:
			return IM_COL32(198, 142, 54, 255);
		case EEditorAssetKind::Texture:
			return IM_COL32(72, 154, 108, 255);
		case EEditorAssetKind::Particle:
			return IM_COL32(204, 95, 60, 255);
		case EEditorAssetKind::Font:
			return IM_COL32(102, 116, 206, 255);
		case EEditorAssetKind::Scene:
			return IM_COL32(55, 155, 172, 255);
		default:
			return IM_COL32(95, 101, 112, 255);
		}
	}

	FString TrimToLength(const FString& Value, size_t MaxLength)
	{
		if (Value.length() <= MaxLength)
		{
			return Value;
		}

		if (MaxLength <= 3)
		{
			return Value.substr(0, MaxLength);
		}

		return Value.substr(0, MaxLength - 3) + "...";
	}

	float EaseOutCubic(float Value)
	{
		Value = std::clamp(Value, 0.0f, 1.0f);
		const float Inv = 1.0f - Value;
		return 1.0f - Inv * Inv * Inv;
	}

	EEditorAssetKind ClassifyAsset(
		const FString& RelativePath,
		const FString& Extension,
		const std::unordered_set<FString>& StaticMeshPaths,
		const std::unordered_set<FString>& SkeletalMeshPaths,
		const std::unordered_set<FString>& TexturePaths,
		const std::unordered_set<FString>& MaterialPaths,
		const std::unordered_set<FString>& FontPaths,
		const std::unordered_set<FString>& ParticlePaths)
	{
		const FString PathKey = ToLowerPathKey(RelativePath);

		if (StaticMeshPaths.contains(PathKey))
		{
			return EEditorAssetKind::StaticMesh;
		}
		if (SkeletalMeshPaths.contains(PathKey))
		{
			return EEditorAssetKind::SkeletalMesh;
		}
		if (FontPaths.contains(PathKey))
		{
			return EEditorAssetKind::Font;
		}
		if (ParticlePaths.contains(PathKey))
		{
			return EEditorAssetKind::Particle;
		}
		if (MaterialPaths.contains(PathKey) || IsMaterialExtension(Extension))
		{
			return EEditorAssetKind::Material;
		}
		if (TexturePaths.contains(PathKey) || IsImageExtension(Extension))
		{
			return EEditorAssetKind::Texture;
		}
		if (Extension == ".scene")
		{
			return EEditorAssetKind::Scene;
		}

		return EEditorAssetKind::Other;
	}
}

void FEditorContentDrawerWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	RefreshAssetTree();
}

void FEditorContentDrawerWidget::SetOpen(bool bInOpen)
{
	if (bInOpen && !bOpen)
	{
		bOpenedThisFrame = true;
		if (ConsoleWidget)
		{
			ConsoleWidget->SetOpen(false);
		}
	}
	bOpen = bInOpen;
	if (bOpen && bAssetTreeDirty)
	{
		RefreshAssetTree();
	}
}

void FEditorContentDrawerWidget::ToggleOpen()
{
	SetOpen(!bOpen);
}

bool FEditorContentDrawerWidget::ConsumeOpenRequest()
{
	const bool bResult = bOpenedThisFrame;
	bOpenedThisFrame = false;
	return bResult;
}

void FEditorContentDrawerWidget::CloseImmediately()
{
	bOpen = false;
	DrawerAnimationAlpha = 0.0f;
	bOpenedThisFrame = false;
}

void FEditorContentDrawerWidget::StartConsoleTakeover()
{
	if (bOpen || DrawerAnimationAlpha > 0.0f)
	{
		bPendingConsoleTakeover = true;
		PendingConsoleTakeoverHeight = DrawerHeight;
	}

	CloseImmediately();
}

bool FEditorContentDrawerWidget::ConsumeConsoleTakeover(float& OutDrawerHeight)
{
	if (!bPendingConsoleTakeover)
	{
		return false;
	}

	bPendingConsoleTakeover = false;
	OutDrawerHeight = PendingConsoleTakeoverHeight;
	PendingConsoleTakeoverHeight = 0.0f;
	return true;
}

void FEditorContentDrawerWidget::RefreshAssetTree()
{
	FolderPaths.clear();
	AssetItems.clear();
	FolderPaths.push_back("Asset");

	namespace fs = std::filesystem;
	const fs::path AssetRoot(FPaths::AssetDirectoryPath());
	if (!fs::exists(AssetRoot) || !fs::is_directory(AssetRoot))
	{
		SelectedFolder = "Asset";
		bAssetTreeDirty = false;
		return;
	}

	const std::unordered_set<FString> StaticMeshPaths =
		MakePathSet(FResourceManager::Get().GetStaticMeshPaths());
	const std::unordered_set<FString> SkeletalMeshPaths =
		MakePathSet(FResourceManager::Get().GetSkeletalMeshPaths());
	const std::unordered_set<FString> TexturePaths =
		MakePathSet(FResourceManager::Get().GetTextureFilePath());
	const std::unordered_set<FString> MaterialPaths =
		MakePathSet(FResourceManager::Get().GetMaterialInterfaceNames());
	const std::unordered_set<FString> FontPaths =
		MakePathSet(FResourceManager::Get().GetFontNames());
	const std::unordered_set<FString> ParticlePaths =
		MakePathSet(FResourceManager::Get().GetParticleNames());

	try
	{
		for (fs::recursive_directory_iterator It(AssetRoot, fs::directory_options::skip_permission_denied), End;
			 It != End;
			 ++It)
		{
			const fs::directory_entry& Entry = *It;
			const fs::path& EntryPath = Entry.path();

			if (Entry.is_directory())
			{
				const FString RelativeFolderPath = MakeRelativeAssetPath(EntryPath);
				if (IsHiddenAssetFolder(RelativeFolderPath))
				{
					It.disable_recursion_pending();
					continue;
				}

				FolderPaths.push_back(RelativeFolderPath);
				continue;
			}

			if (!Entry.is_regular_file())
			{
				continue;
			}

			const FString Extension = GetExtensionFromPath(EntryPath);
			if (ShouldSkipAssetFile(EntryPath, Extension))
			{
				continue;
			}

			FEditorAssetItem Item;
			Item.RelativePath = MakeRelativeAssetPath(EntryPath);
			Item.DisplayName = GetDisplayNameFromPath(EntryPath);
			Item.Extension = Extension.empty() ? "(none)" : Extension;
			Item.Kind = ClassifyAsset(
				Item.RelativePath,
				Extension,
				StaticMeshPaths,
				SkeletalMeshPaths,
				TexturePaths,
				MaterialPaths,
				FontPaths,
				ParticlePaths);

			AssetItems.push_back(Item);
		}
	}
	catch (const std::exception&)
	{
	}

	std::sort(FolderPaths.begin(), FolderPaths.end(), [](const FString& Left, const FString& Right)
	{
		return ToLowerString(Left) < ToLowerString(Right);
	});
	FolderPaths.erase(std::unique(FolderPaths.begin(), FolderPaths.end()), FolderPaths.end());

	std::sort(AssetItems.begin(), AssetItems.end(), [](const FEditorAssetItem& Left, const FEditorAssetItem& Right)
	{
		if (Left.Kind != Right.Kind)
		{
			return static_cast<int32>(Left.Kind) < static_cast<int32>(Right.Kind);
		}
		return ToLowerString(Left.RelativePath) < ToLowerString(Right.RelativePath);
	});

	if (std::find(FolderPaths.begin(), FolderPaths.end(), SelectedFolder) == FolderPaths.end())
	{
		SelectedFolder = "Asset";
	}

	bAssetTreeDirty = false;
}

float FEditorContentDrawerWidget::GetReservedBottomHeight() const
{
	float ReservedDrawerHeight = DrawerHeight;
	if (ReservedDrawerHeight <= 0.0f)
	{
		if (const ImGuiViewport* Viewport = ImGui::GetMainViewport())
		{
			const float AvailableHeight = std::max(120.0f, Viewport->WorkSize.y - ContentDrawerBottomBarHeight);
			ReservedDrawerHeight = AvailableHeight * 0.35f;
		}
	}

	const float EasedAlpha = EaseOutCubic(DrawerAnimationAlpha);
	return ContentDrawerBottomBarHeight + ReservedDrawerHeight * EasedAlpha;
}

void FEditorContentDrawerWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	const ImVec2 WorkPos = Viewport->WorkPos;
	const ImVec2 WorkSize = Viewport->WorkSize;
	const float BottomBarHeight = ContentDrawerBottomBarHeight;
	const float TargetAlpha = bOpen ? 1.0f : 0.0f;
	const float AlphaStep = std::clamp(DeltaTime * ContentDrawerAnimationSpeed, 0.0f, 1.0f);
	DrawerAnimationAlpha += (TargetAlpha - DrawerAnimationAlpha) * AlphaStep;

	if (!bOpen && DrawerAnimationAlpha < 0.001f)
	{
		DrawerAnimationAlpha = 0.0f;
	}
	else if (bOpen && DrawerAnimationAlpha > 0.999f)
	{
		DrawerAnimationAlpha = 1.0f;
	}

	if (!bOpen && DrawerAnimationAlpha <= 0.0f)
	{
		RenderBottomBar(Viewport, BottomBarHeight);
		return;
	}

	if (bOpen && bAssetTreeDirty)
	{
		RefreshAssetTree();
	}

	const float AvailableHeight = std::max(120.0f, WorkSize.y - BottomBarHeight);
	const float MinHeight = std::min(220.0f, AvailableHeight * 0.8f);
	const float MaxHeight = std::max(MinHeight, AvailableHeight * 0.82f);

	if (DrawerHeight <= 0.0f)
	{
		DrawerHeight = AvailableHeight * 0.35f;
	}
	DrawerHeight = std::clamp(DrawerHeight, MinHeight, MaxHeight);
	const float EasedAlpha = EaseOutCubic(DrawerAnimationAlpha);
	const float ClosedY = WorkPos.y + WorkSize.y - BottomBarHeight;
	const float OpenY = ClosedY - DrawerHeight;
	const float AnimatedY = ClosedY + (OpenY - ClosedY) * EasedAlpha;

	ImGui::SetNextWindowViewport(Viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(WorkPos.x, AnimatedY), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(WorkSize.x, DrawerHeight), ImGuiCond_Always);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

	constexpr ImGuiWindowFlags WindowFlags =
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings;

	bool bWindowOpen = bOpen;
	if (ImGui::Begin("Content Drawer", &bWindowOpen, WindowFlags))
	{
		RenderResizeHandle(AvailableHeight);
		RenderDrawerToolbar();
		ImGui::Separator();

		const float StatusBarHeight = ImGui::GetFrameHeightWithSpacing();
		const float BodyHeight = std::max(100.0f, ImGui::GetContentRegionAvail().y - StatusBarHeight);

		ImGui::BeginChild("##ContentDrawerBody", ImVec2(0.0f, BodyHeight), false);
		{
			const float FolderPanelWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.22f, 180.0f, 320.0f);
			ImGui::BeginChild("##ContentDrawerFolders", ImVec2(FolderPanelWidth, 0.0f), true);
			RenderFolderTree();
			ImGui::EndChild();

			ImGui::SameLine();

			ImGui::BeginChild("##ContentDrawerAssets", ImVec2(0.0f, 0.0f), true);
			RenderAssetGrid();
			ImGui::EndChild();
		}
		ImGui::EndChild();

		RenderStatusBar();
	}
	ImGui::End();

	ImGui::PopStyleVar(2);
	bOpen = bWindowOpen;

	RenderBottomBar(Viewport, BottomBarHeight);
}

void FEditorContentDrawerWidget::RenderResizeHandle(float WorkAreaHeight)
{
	const float HandleHeight = 6.0f;
	ImGui::InvisibleButton("##ContentDrawerResizeHandle", ImVec2(-1.0f, HandleHeight));

	const bool bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();
	if (bHovered || bActive)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}

	if (bActive)
	{
		DrawerHeight = std::clamp(
			DrawerHeight - ImGui::GetIO().MouseDelta.y,
			std::min(220.0f, WorkAreaHeight * 0.8f),
			std::max(220.0f, WorkAreaHeight * 0.82f));
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	DrawList->AddRectFilled(
		Min,
		Max,
		(bHovered || bActive) ? IM_COL32(36, 36, 36, 220) : IM_COL32(26, 26, 26, 180));
}

void FEditorContentDrawerWidget::RenderBottomBar(const ImGuiViewport* Viewport, float BottomBarHeight)
{
	if (Viewport == nullptr)
	{
		return;
	}

	const ImVec2 WorkPos = Viewport->WorkPos;
	const ImVec2 WorkSize = Viewport->WorkSize;

	ImGui::SetNextWindowViewport(Viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(WorkPos.x, WorkPos.y + WorkSize.y - BottomBarHeight), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(WorkSize.x, BottomBarHeight), ImGuiCond_Always);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 3.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 3.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.082f, 0.082f, 0.082f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.141f, 0.141f, 0.141f, 1.0f));

	constexpr ImGuiWindowFlags WindowFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	if (ImGui::Begin("##ContentDrawerBottomBar", nullptr, WindowFlags))
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 BarMin = ImGui::GetWindowPos();
		const ImVec2 BarMax = ImVec2(BarMin.x + ImGui::GetWindowWidth(), BarMin.y + ImGui::GetWindowHeight());
		DrawList->AddLine(BarMin, ImVec2(BarMax.x, BarMin.y), IM_COL32(36, 36, 36, 255));

		const bool bWasOpen = bOpen;
		auto PushButtonStyle = [](bool bActive)
		{
			if (bActive)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.141f, 0.141f, 0.141f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.188f, 0.188f, 0.188f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.102f, 0.102f, 0.102f, 1.0f));
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.102f, 0.102f, 0.102f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.141f, 0.141f, 0.141f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.082f, 0.082f, 0.082f, 1.0f));
			}
		};

		PushButtonStyle(bWasOpen);
		if (ImGui::Button("    Content Drawer", ImVec2(142.0f, 22.0f)))
		{
			ToggleOpen();
		}

		const ImVec2 ButtonMin = ImGui::GetItemRectMin();
		const ImU32 FolderColor = bWasOpen ? IM_COL32(214, 214, 214, 255) : IM_COL32(128, 128, 128, 255);
		DrawList->AddRectFilled(
			ImVec2(ButtonMin.x + 10.0f, ButtonMin.y + 8.0f),
			ImVec2(ButtonMin.x + 24.0f, ButtonMin.y + 17.0f),
			FolderColor,
			2.0f);
		DrawList->AddRectFilled(
			ImVec2(ButtonMin.x + 12.0f, ButtonMin.y + 5.0f),
			ImVec2(ButtonMin.x + 20.0f, ButtonMin.y + 10.0f),
			FolderColor,
			2.0f);

		ImGui::PopStyleColor(3);

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Ctrl+Space");
		}

		ImGui::SameLine(0.0f, 6.0f);

		const bool bConsoleOpen = ConsoleWidget && ConsoleWidget->IsOpen();
		PushButtonStyle(bConsoleOpen);
		if (ImGui::Button("    Console", ImVec2(96.0f, 22.0f)) && ConsoleWidget)
		{
			const bool bNextOpen = !ConsoleWidget->IsOpen();
			if (bNextOpen)
			{
				StartConsoleTakeover();
				float TakeoverHeight = 0.0f;
				if (ConsumeConsoleTakeover(TakeoverHeight))
				{
					ConsoleWidget->OpenFromDrawerTakeover(TakeoverHeight);
				}
				else
				{
					ConsoleWidget->SetOpen(true);
				}
			}
			else
			{
				ConsoleWidget->SetOpen(false);
			}
		}

		const ImVec2 ConsoleButtonMin = ImGui::GetItemRectMin();
		const ImVec2 ConsoleButtonMax = ImGui::GetItemRectMax();
		const ImU32 ConsoleColor = bConsoleOpen ? IM_COL32(214, 214, 214, 255) : IM_COL32(128, 128, 128, 255);
		DrawList->AddRect(
			ImVec2(ConsoleButtonMin.x + 10.0f, ConsoleButtonMin.y + 6.0f),
			ImVec2(ConsoleButtonMin.x + 25.0f, ConsoleButtonMax.y - 5.0f),
			ConsoleColor,
			2.0f,
			0,
			1.5f);
		DrawList->AddText(
			ImVec2(ConsoleButtonMin.x + 14.0f, ConsoleButtonMin.y + 4.0f),
			ConsoleColor,
			">");

		ImGui::PopStyleColor(3);

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("`");
		}

		if (ConsoleWidget)
		{
			ImGui::SameLine(0.0f, 8.0f);
			const float CompactInputWidth = std::clamp(ImGui::GetContentRegionAvail().x, 180.0f, 420.0f);
			ConsoleWidget->RenderCompactInput(CompactInputWidth);
			if (ConsoleWidget->ConsumeCompactOpenRequest())
			{
				StartConsoleTakeover();
				float TakeoverHeight = 0.0f;
				if (ConsumeConsoleTakeover(TakeoverHeight))
				{
					ConsoleWidget->OpenFromDrawerTakeover(TakeoverHeight);
				}
				else
				{
					ConsoleWidget->SetOpen(true);
				}
			}
		}
	}
	ImGui::End();

	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(4);
}

void FEditorContentDrawerWidget::RenderDrawerToolbar()
{
	ImGui::TextUnformatted("Content");
	ImGui::SameLine();

	const float RefreshButtonWidth = 70.0f;
	const float ComboWidth = 140.0f;
	const float SearchWidth = std::max(
		180.0f,
		ImGui::GetContentRegionAvail().x - ComboWidth - RefreshButtonWidth - ImGui::GetStyle().ItemSpacing.x * 4.0f);

	ImGui::SetNextItemWidth(SearchWidth);
	ImGui::InputTextWithHint("##ContentDrawerSearch", "Search assets", SearchBuffer, sizeof(SearchBuffer));

	ImGui::SameLine();
	ImGui::SetNextItemWidth(ComboWidth);
	if (ImGui::BeginCombo("##ContentDrawerTypeFilter", AssetKindLabels[SelectedKindFilter]))
	{
		for (int32 i = 0; i < static_cast<int32>(IM_ARRAYSIZE(AssetKindLabels)); ++i)
		{
			const bool bSelected = (SelectedKindFilter == i);
			if (ImGui::Selectable(AssetKindLabels[i], bSelected))
			{
				SelectedKindFilter = i;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Button("Refresh", ImVec2(RefreshButtonWidth, 0.0f)))
	{
		FResourceManager::Get().RefreshFromAssetDirectory(FPaths::ToUtf8(FPaths::AssetDirectoryPath()));
		RefreshAssetTree();
	}
}

void FEditorContentDrawerWidget::RenderFolderTree()
{
	RenderFolderNode("Asset");
}

void FEditorContentDrawerWidget::RenderFolderNode(const FString& FolderPath)
{
	TArray<FString> Children;
	for (const FString& Candidate : FolderPaths)
	{
		if (Candidate != FolderPath && GetParentFolderPath(Candidate) == FolderPath)
		{
			Children.push_back(Candidate);
		}
	}

	const bool bHasChildren = !Children.empty();
	ImGuiTreeNodeFlags Flags =
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth;

	if (FolderPath == "Asset")
	{
		Flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	if (!bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (SelectedFolder == FolderPath)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	const size_t SlashPos = FolderPath.find_last_of('/');
	const FString DisplayName = (SlashPos == FString::npos) ? FolderPath : FolderPath.substr(SlashPos + 1);
	const FString Label = DisplayName + "##" + FolderPath;
	const bool bOpenNode = ImGui::TreeNodeEx(Label.c_str(), Flags);

	if (ImGui::IsItemClicked())
	{
		SelectedFolder = FolderPath;
	}

	if (bHasChildren && bOpenNode)
	{
		for (const FString& Child : Children)
		{
			RenderFolderNode(Child);
		}
		ImGui::TreePop();
	}
}

void FEditorContentDrawerWidget::RenderAssetGrid()
{
	ImGui::TextDisabled("%s", SelectedFolder.c_str());
	ImGui::Separator();

	const float TileWidth = 154.0f;
	const float TileHeight = 104.0f;
	const float SpacingX = ImGui::GetStyle().ItemSpacing.x;
	const float AvailableWidth = std::max(TileWidth, ImGui::GetContentRegionAvail().x);
	const int32 ColumnCount = std::max(1, static_cast<int32>((AvailableWidth + SpacingX) / (TileWidth + SpacingX)));

	LastVisibleFolderCount = 0;
	LastVisibleAssetCount = 0;
	ImGui::Columns(ColumnCount, "##ContentDrawerAssetColumns", false);

	for (const FString& FolderPath : FolderPaths)
	{
		if (FolderPath == SelectedFolder || GetParentFolderPath(FolderPath) != SelectedFolder)
		{
			continue;
		}

		++LastVisibleFolderCount;
		RenderFolderTile(FolderPath, ImVec2(TileWidth, TileHeight));
		ImGui::NextColumn();
	}

	for (const FEditorAssetItem& Item : AssetItems)
	{
		if (!ShouldDisplayAsset(Item))
		{
			continue;
		}

		++LastVisibleAssetCount;
		RenderAssetTile(Item, ImVec2(TileWidth, TileHeight));
		ImGui::NextColumn();
	}
	ImGui::Columns(1);

	if (LastVisibleFolderCount == 0 && LastVisibleAssetCount == 0)
	{
		ImGui::TextDisabled("No assets match the current filters.");
	}
}

void FEditorContentDrawerWidget::RenderFolderTile(const FString& FolderPath, const ImVec2& TileSize)
{
	ImGui::PushID(FolderPath.c_str());

	ImGui::InvisibleButton("##FolderTile", TileSize);
	const bool bHovered = ImGui::IsItemHovered();
	const bool bClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	const bool bDoubleClicked = bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

	if (bClicked)
	{
		SelectedAssetPath.clear();
	}

	if (bDoubleClicked)
	{
		SelectedFolder = FolderPath;
		SelectedAssetPath.clear();
	}

	const size_t SlashPos = FolderPath.find_last_of('/');
	const FString DisplayName = (SlashPos == FString::npos) ? FolderPath : FolderPath.substr(SlashPos + 1);

	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	const ImU32 AccentColor = IM_COL32(214, 172, 68, 255);
	const ImU32 BgColor = bHovered ? IM_COL32(36, 36, 36, 255) : IM_COL32(26, 26, 26, 255);
	const ImU32 BorderColor = bHovered ? IM_COL32(63, 63, 63, 255) : IM_COL32(36, 36, 36, 255);

	DrawList->AddRectFilled(Min, Max, BgColor, 5.0f);
	DrawList->AddRect(Min, Max, BorderColor, 5.0f);

	const ImVec2 FolderMin(Min.x + 14.0f, Min.y + 18.0f);
	const ImVec2 FolderMax(Min.x + 74.0f, Min.y + 58.0f);
	DrawList->AddRectFilled(FolderMin, FolderMax, AccentColor, 4.0f);
	DrawList->AddRectFilled(
		ImVec2(FolderMin.x + 6.0f, FolderMin.y - 7.0f),
		ImVec2(FolderMin.x + 34.0f, FolderMin.y + 6.0f),
		AccentColor,
		3.0f);

	DrawList->PushClipRect(Min, Max, true);
	DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 67.0f), IM_COL32(238, 238, 238, 255),
					  TrimToLength(DisplayName, 22).c_str());
	DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 86.0f), IM_COL32(128, 128, 128, 255), "Folder");
	DrawList->PopClipRect();

	if (bHovered)
	{
		ImGui::SetTooltip("%s", FolderPath.c_str());
	}

	ImGui::PopID();
}

void FEditorContentDrawerWidget::RenderAssetTile(const FEditorAssetItem& Item, const ImVec2& TileSize)
{
	ImGui::PushID(Item.RelativePath.c_str());

	ImGui::InvisibleButton("##AssetTile", TileSize);
	const bool bHovered = ImGui::IsItemHovered();
	const bool bClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	const bool bDoubleClicked = bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

	if (bClicked)
	{
		SelectedAssetPath = Item.RelativePath;
	}

	if (bDoubleClicked)
	{
		SelectedAssetPath = Item.RelativePath;

		if (Item.Kind == EEditorAssetKind::SkeletalMesh)
		{
			if (EditorEngine)
			{
				EditorEngine->GetMainPanel().OpenSkeletalMeshViewer(Item.RelativePath);
			}
		}
		else
		{
			PlaceAssetInWorld(Item);
		}
	}

	const bool bSelected = SelectedAssetPath == Item.RelativePath;
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	const ImU32 AccentColor = GetAssetKindColor(Item.Kind);
	const ImU32 BgColor =
		bSelected ? IM_COL32(36, 36, 36, 255) :
		bHovered ? IM_COL32(30, 30, 30, 255) :
		IM_COL32(26, 26, 26, 255);
	const ImU32 BorderColor = bSelected ? IM_COL32(80, 80, 80, 255) : IM_COL32(36, 36, 36, 255);

	DrawList->AddRectFilled(Min, Max, BgColor, 5.0f);
	DrawList->AddRect(Min, Max, BorderColor, 5.0f);
	DrawList->AddRectFilled(Min, ImVec2(Max.x, Min.y + 5.0f), AccentColor, 5.0f, ImDrawFlags_RoundCornersTop);

	DrawList->PushClipRect(Min, Max, true);
	DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 14.0f), AccentColor, ToAssetKindLabel(Item.Kind));
	DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 37.0f), IM_COL32(238, 238, 238, 255),
					  TrimToLength(Item.DisplayName, 22).c_str());
	DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 59.0f), IM_COL32(160, 160, 160, 255), Item.Extension.c_str());
	DrawList->AddText(ImVec2(Min.x + 8.0f, Min.y + 79.0f), IM_COL32(128, 128, 128, 255),
					  TrimToLength(Item.RelativePath, 28).c_str());
	DrawList->PopClipRect();

	if (bHovered)
	{
		ImGui::SetTooltip("%s\n%s", Item.RelativePath.c_str(), ToAssetKindLabel(Item.Kind));
	}

	ImGui::PopID();
}

void FEditorContentDrawerWidget::RenderStatusBar() const
{
	ImGui::Separator();
	if (SelectedAssetPath.empty())
	{
		ImGui::TextDisabled("%d folders | %d assets", LastVisibleFolderCount, LastVisibleAssetCount);
	}
	else
	{
		ImGui::TextDisabled("%d folders | %d assets | Selected: %s",
							LastVisibleFolderCount,
							LastVisibleAssetCount,
							SelectedAssetPath.c_str());
	}
}

bool FEditorContentDrawerWidget::ShouldDisplayAsset(const FEditorAssetItem& Item) const
{
	if (!MatchesSelectedFolder(Item) || !MatchesSearch(Item))
	{
		return false;
	}

	if (SelectedKindFilter <= 0)
	{
		return true;
	}

	return static_cast<int32>(Item.Kind) == SelectedKindFilter;
}

bool FEditorContentDrawerWidget::MatchesSelectedFolder(const FEditorAssetItem& Item) const
{
	if (SelectedFolder.empty())
	{
		return true;
	}

	const FString ItemFolder = GetDirectoryPath(Item.RelativePath);
	return ItemFolder == SelectedFolder;
}

bool FEditorContentDrawerWidget::MatchesSearch(const FEditorAssetItem& Item) const
{
	if (SearchBuffer[0] == '\0')
	{
		return true;
	}

	const FString Search = ToLowerString(SearchBuffer);
	const FString Haystack = ToLowerString(Item.DisplayName + " " + Item.Extension + " " + Item.RelativePath);
	return Haystack.find(Search) != FString::npos;
}

bool FEditorContentDrawerWidget::PlaceAssetInWorld(const FEditorAssetItem& Item)
{
	if (!EditorEngine)
	{
		return false;
	}

	UWorld* World = EditorEngine->GetFocusedWorld();
	if (!World)
	{
		return false;
	}

	const FVector PlacementLocation = GetAssetPlacementLocation();

	if (Item.Kind == EEditorAssetKind::StaticMesh)
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (!Actor)
		{
			return false;
		}

		Actor->InitDefaultComponents();
		Actor->SetActorLocation(PlacementLocation);

		if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Actor->GetRootComponent()))
		{
			MeshComponent->SetStaticMesh(FResourceManager::Get().LoadStaticMesh(Item.RelativePath));
		}

		EditorEngine->GetSelectionManager().Select(Actor);
		return true;
	}

	if (Item.Kind == EEditorAssetKind::SkeletalMesh)
	{
		ASkeletalMeshActor* Actor = World->SpawnActor<ASkeletalMeshActor>();
		if (!Actor)
		{
			return false;
		}

		Actor->InitDefaultComponents();
		Actor->SetActorLocation(PlacementLocation);

		if (USkeletalMeshComponent* MeshComponent = Cast<USkeletalMeshComponent>(Actor->GetRootComponent()))
		{
			MeshComponent->SetSkeletalMesh(FResourceManager::Get().LoadSkeletalMesh(Item.RelativePath));
		}

		EditorEngine->GetSelectionManager().Select(Actor);
		return true;
	}

	return false;
}

FVector FEditorContentDrawerWidget::GetAssetPlacementLocation() const
{
	if (!EditorEngine)
	{
		return FVector::ZeroVector;
	}

	const FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
	const int32 ViewportIndex =
		std::clamp(Layout.GetLastFocusedViewportIndex(), 0, FEditorViewportLayout::MaxViewports - 1);
	const FEditorViewportClient* Client = Layout.GetViewportClient(ViewportIndex);
	const FViewportCamera* Camera = Client ? Client->GetCamera() : nullptr;
	if (!Camera)
	{
		return FVector::ZeroVector;
	}

	FVector Forward = Camera->GetEffectiveForward();
	if (Forward.IsNearlyZero())
	{
		Forward = Camera->GetForwardVector();
	}
	Forward.NormalizeSafe();

	return Camera->GetLocation() + Forward * 5.0f;
}
