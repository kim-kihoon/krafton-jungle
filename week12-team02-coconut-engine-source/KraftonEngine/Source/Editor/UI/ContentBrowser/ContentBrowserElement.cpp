#include "ContentBrowserElement.h"

#include "Animation/AnimDataModel.h"
#include "Animation/AnimInstanceAsset.h"
#include "Animation/AnimInstanceAssetManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceManager.h"
#include "Asset/AssetPackage.h"
#include "Editor/EditorEngine.h"
#include "Editor/Import/EditorFbxImportService.h"
#include "Editor/Import/EditorObjImportService.h"
#include "FloatCurve/FloatCurveAsset.h"
#include "FloatCurve/FloatCurveManager.h"
#include "CameraShake/CameraShakeAsset.h"
#include "CameraShake/CameraShakeManager.h"
#include "Core/Log.h"
#include "Platform/Paths.h"
#include "Serialization/SceneSaveManager.h"
#include "Materials/MaterialManager.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/MeshManager.h"
#include "Object/Object.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemManager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

static FString FormatBytes(uint64 Bytes)
{
	char Buffer[64];

	if (Bytes >= 1024ull * 1024ull)
	{
		std::snprintf(Buffer, sizeof(Buffer), "%.2f MB", static_cast<double>(Bytes) / (1024.0 * 1024.0));
	}
	else if (Bytes >= 1024ull)
	{
		std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", static_cast<double>(Bytes) / 1024.0);
	}
	else
	{
		std::snprintf(Buffer, sizeof(Buffer), "%llu B", static_cast<unsigned long long>(Bytes));
	}

	return Buffer;
}

static void DrawDetailRow(const char* Label, const FString& Value)
{
	ImGui::TableNextRow();

	ImGui::TableSetColumnIndex(0);
	ImGui::TextDisabled("%s", Label);

	ImGui::TableSetColumnIndex(1);

	const float AvailableWidth = ImGui::GetContentRegionAvail().x;
	FString Clipped = Value;

	if (ImGui::CalcTextSize(Clipped.c_str()).x > AvailableWidth)
	{
		while (!Clipped.empty() && ImGui::CalcTextSize((Clipped + "...").c_str()).x > AvailableWidth)
		{
			Clipped.erase(Clipped.begin());
		}

		Clipped = "..." + Clipped;
	}

	ImGui::TextUnformatted(Clipped.c_str());

	if (ImGui::IsItemHovered() && Clipped != Value)
	{
		ImGui::SetTooltip("%s", Value.c_str());
	}
}

static FString GetLowerExtensionFromPath(const FString& Path)
{
	FString Extension = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(Path)).extension());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
	return Extension;
}

static ImVec2 GetTextureSize(ID3D11ShaderResourceView* View)
{
	if (!View)
	{
		return ImVec2(0.0f, 0.0f);
	}

	ID3D11Resource* Resource = nullptr;
	View->GetResource(&Resource);
	if (!Resource)
	{
		return ImVec2(0.0f, 0.0f);
	}

	ID3D11Texture2D* Texture = nullptr;
	const HRESULT Result = Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture));
	Resource->Release();

	if (FAILED(Result) || !Texture)
	{
		return ImVec2(0.0f, 0.0f);
	}

	D3D11_TEXTURE2D_DESC Desc = {};
	Texture->GetDesc(&Desc);
	Texture->Release();

	return ImVec2(static_cast<float>(Desc.Width), static_cast<float>(Desc.Height));
}

static void DrawImagePreserveAspect(ImDrawList* DrawList, ID3D11ShaderResourceView* Icon, const ImVec2& Min, const ImVec2& Max)
{
	const ImVec2 TextureSize = GetTextureSize(Icon);
	if (!DrawList || !Icon || TextureSize.x <= 0.0f || TextureSize.y <= 0.0f || Max.x <= Min.x || Max.y <= Min.y)
	{
		return;
	}

	const ImVec2 Available(Max.x - Min.x, Max.y - Min.y);
	const float Scale = (std::min)(
		1.0f,
		(std::min)(Available.x / TextureSize.x, Available.y / TextureSize.y));
	const ImVec2 DrawSize(TextureSize.x * Scale, TextureSize.y * Scale);
	const ImVec2 DrawMin(
		Min.x + (Available.x - DrawSize.x) * 0.5f,
		Min.y + (Available.y - DrawSize.y) * 0.5f);
	const ImVec2 DrawMax(DrawMin.x + DrawSize.x, DrawMin.y + DrawSize.y);

	DrawList->AddImage(Icon, DrawMin, DrawMax);
}

bool ContentBrowserElement::RenderSelectSpace(ContentBrowserContext& Context)
{
	FString Name = FPaths::ToUtf8(ContentItem.Name);
	ImGui::PushID(Name.c_str());

	bIsSelected = Context.SelectedElement.get() == this;

	const ImVec2 CardSize = Context.ContentSize;
	const bool bClicked = ImGui::InvisibleButton("##ElementCard", CardSize);

	const bool bHovered = ImGui::IsItemHovered();

	ImVec2 Min = ImGui::GetItemRectMin();
	ImVec2 Max = ImGui::GetItemRectMax();

	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	const ImU32 CardColor = bIsSelected
		? IM_COL32(54, 86, 130, 255)
		: bHovered
		? IM_COL32(48, 50, 56, 255)
		: IM_COL32(34, 36, 40, 255);

	const ImU32 BorderColor = bIsSelected
		? IM_COL32(98, 160, 255, 255)
		: bHovered
		? IM_COL32(90, 94, 104, 255)
		: IM_COL32(55, 58, 64, 255);

	DrawList->AddRectFilled(Min, Max, CardColor, 6.0f);
	DrawList->AddRect(Min, Max, BorderColor, 6.0f, 0, bIsSelected ? 2.0f : 1.0f);

	const uint32 AccentColor = GetAccentColor();
	if (AccentColor != 0)
	{
		DrawList->AddRectFilled(
			ImVec2(Min.x, Min.y),
			ImVec2(Max.x, Min.y + 4.0f),
			AccentColor,
			6.0f,
			ImDrawFlags_RoundCornersTop);
	}

	const float Padding = 8.0f;
	const float FontSize = ImGui::GetFontSize();

	const float LabelHeight = FontSize * 2.4f;
	ImVec2 IconMin(Min.x + Padding, Min.y + Padding);
	ImVec2 IconMax(Max.x - Padding, Max.y - Padding - LabelHeight);

	if (Icon && IconMax.y > IconMin.y)
	{
		DrawImagePreserveAspect(DrawList, Icon, IconMin, IconMax);
	}

	const char* TypeLabel = GetTypeLabel();
	const bool bHasTypeLabel = TypeLabel && TypeLabel[0] != '\0';

	const FString DisplayName = EllipsisText(GetDisplayName(), CardSize.x - Padding * 2);

	ImVec2 TypePos(Min.x + Padding, Max.y - Padding - FontSize * 2.0f);
	ImVec2 NamePos(Min.x + Padding, Max.y - Padding - FontSize);

	if (bHasTypeLabel)
	{
		DrawList->AddText(TypePos, ImGui::GetColorU32(ImGuiCol_TextDisabled), TypeLabel);
	}

	DrawList->AddText(NamePos, ImGui::GetColorU32(ImGuiCol_Text), DisplayName.c_str());

	ImGui::PopID();

	return bClicked;
}

void ContentBrowserElement::Render(ContentBrowserContext& Context)
{
	if (RenderSelectSpace(Context))
	{
		Context.SelectedElement = shared_from_this();
		bIsSelected = true;
		OnLeftClicked(Context);
	}

	if (ImGui::BeginPopupContextItem())
	{
		RenderContextMenu(Context);
		ImGui::EndPopup();
	}

	if (bPendingOpenDeletePopup)
	{
		ImGui::OpenPopup(GetDeletePopupId().c_str());
		bPendingOpenDeletePopup = false;
	}
	RenderDeleteConfirmation(Context);

	if (bPendingOpenRenamePopup)
	{
		ImGui::OpenPopup(GetRenamePopupId().c_str());
		bPendingOpenRenamePopup = false;
	}
	RenderRenamePopup(Context);

	bool bDoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	if (bDoubleClicked)
	{
		OnDoubleLeftClicked(Context);
	}

	if (ImGui::BeginDragDropSource())
	{
		RenderSelectSpace(Context);
		ImGui::SetDragDropPayload(GetDragItemType(), &ContentItem, sizeof(ContentItem));
		OnDrag(Context);
		ImGui::EndDragDropSource();
	}
}

void ContentBrowserElement::RenderDetail()
{
	const FString DisplayName = GetDisplayName();
	const char* TypeLabel = GetTypeLabel();

	ImGui::TextUnformatted(DisplayName.c_str());
	if (TypeLabel && TypeLabel[0] != '\0')
	{
		ImGui::TextDisabled("%s", TypeLabel);
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::BeginTable("AssetDetailsTable", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 72.0f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		DrawDetailRow("Name", DisplayName);

		if (TypeLabel && TypeLabel[0] != '\0')
		{
			DrawDetailRow("Type", TypeLabel);
		}

		const FString RelativePath = FPaths::ToUtf8(
			ContentItem.Path.lexically_relative(FPaths::RootDir()).generic_wstring());
		DrawDetailRow("Path", RelativePath);

		FString Extension = FPaths::ToUtf8(ContentItem.Path.extension());
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::tolower);

		if (Extension == ".uasset")
		{
			DrawDetailRow("Package", "uasset");

			const FString PackagePath = RelativePath;

			EAssetPackageType PackageType = EAssetPackageType::Unknown;
			if (FAssetPackage::GetPackageType(PackagePath, PackageType))
			{
				FAssetImportMetadata Metadata;
				if (FAssetPackage::ReadMetadata(PackagePath, PackageType, Metadata))
				{
					if (!Metadata.SourcePath.empty())
					{
						DrawDetailRow("Source", Metadata.SourcePath);
					}

					if (Metadata.SourceFileSize > 0)
					{
						DrawDetailRow("Size", FormatBytes(Metadata.SourceFileSize));
					}
				}
			}
		}

		ImGui::EndTable();
	}
}

void ContentBrowserElement::RenderContextMenu(ContentBrowserContext& Context)
{
	if (ImGui::MenuItem("Open"))
	{
		OnDoubleLeftClicked(Context);
	}

	if (ImGui::MenuItem("Rename"))
	{
		PrepareRenamePopup();
		bPendingOpenRenamePopup = true;
	}

	if (ImGui::MenuItem("Show in Explorer"))
	{
		const std::wstring Args = L"/select,\"" + ContentItem.Path.wstring() + L"\"";
		ShellExecuteW(nullptr, L"open", L"explorer.exe", Args.c_str(), nullptr, SW_SHOWNORMAL);
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Delete"))
	{
		bPendingOpenDeletePopup = true;
	}
}

FString ContentBrowserElement::GetDeletePopupId() const
{
	return "Delete Asset?##" + FPaths::ToUtf8(ContentItem.Path.generic_wstring());
}

FString ContentBrowserElement::GetRenamePopupId() const
{
	return "Rename Asset##" + FPaths::ToUtf8(ContentItem.Path.generic_wstring());
}

void ContentBrowserElement::RenderDeleteConfirmation(ContentBrowserContext& Context)
{
	const FString PopupId = GetDeletePopupId();
	if (!ImGui::BeginPopupModal(PopupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	const FString DisplayName = GetDisplayName();
	ImGui::Text("Delete '%s'?", DisplayName.c_str());
	ImGui::TextDisabled("This removes the file from disk.");
	ImGui::Spacing();

	if (ImGui::Button("Delete", ImVec2(96.0f, 0.0f)))
	{
		if (DeleteFromDisk())
		{
			if (Context.SelectedElement.get() == this)
			{
				Context.SelectedElement.reset();
			}
			Context.bPendingContentRefresh = true;
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)))
	{
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void ContentBrowserElement::RenderRenamePopup(ContentBrowserContext& Context)
{
	const FString PopupId = GetRenamePopupId();
	if (!ImGui::BeginPopupModal(PopupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	ImGui::TextUnformatted("Rename");
	ImGui::SetNextItemWidth(280.0f);
	const bool bSubmitted = ImGui::InputText("##Rename", RenameBuffer, sizeof(RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::Spacing();

	if (ImGui::Button("Rename", ImVec2(96.0f, 0.0f)) || bSubmitted)
	{
		if (RenameOnDisk(RenameBuffer))
		{
			Context.SelectedElement.reset();
			Context.bPendingContentRefresh = true;
		}
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)))
	{
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void ContentBrowserElement::PrepareRenamePopup()
{
	const FString DisplayName = GetDisplayName();
	std::snprintf(RenameBuffer, sizeof(RenameBuffer), "%s", DisplayName.c_str());
}

bool ContentBrowserElement::DeleteFromDisk()
{
	std::error_code Error;
	if (std::filesystem::is_directory(ContentItem.Path, Error))
	{
		std::filesystem::remove_all(ContentItem.Path, Error);
	}
	else
	{
		std::filesystem::remove(ContentItem.Path, Error);
	}

	if (Error)
	{
		UE_LOG("Content Browser delete failed: %s", Error.message().c_str());
		return false;
	}

	return true;
}

bool ContentBrowserElement::RenameOnDisk(const FString& NewName)
{
	if (NewName.empty() || NewName.find_first_of("<>:\"/\\|?*") != FString::npos)
	{
		UE_LOG("Content Browser rename failed: invalid name '%s'", NewName.c_str());
		return false;
	}

	std::filesystem::path TargetPath = ContentItem.Path.parent_path() / FPaths::ToWide(NewName);
	std::error_code Error;
	const bool bIsDirectory = std::filesystem::is_directory(ContentItem.Path, Error);
	if (!bIsDirectory && TargetPath.extension().empty())
	{
		TargetPath.replace_extension(ContentItem.Path.extension());
	}

	if (TargetPath == ContentItem.Path)
	{
		return true;
	}

	if (std::filesystem::exists(TargetPath, Error))
	{
		UE_LOG("Content Browser rename failed: target already exists. Path=%s", FPaths::ToUtf8(TargetPath.generic_wstring()).c_str());
		return false;
	}

	std::filesystem::rename(ContentItem.Path, TargetPath, Error);
	if (Error)
	{
		UE_LOG("Content Browser rename failed: %s", Error.message().c_str());
		return false;
	}

	return true;
}

FString ContentBrowserElement::EllipsisText(const FString& text, float maxWidth)
{
	ImFont* font = ImGui::GetFont();
	float fontSize = ImGui::GetFontSize();

	if (font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x <= maxWidth)
		return text;

	const char* ellipsis = "...";
	float ellipsisWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, ellipsis).x;

	std::string result = text;

	while (!result.empty())
	{
		result.pop_back();

		float w = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, result.c_str()).x;
		if (w + ellipsisWidth <= maxWidth)
		{
			result += ellipsis;
			break;
		}
	}

	return result;
}

FString ContentBrowserElement::GetDisplayName() const
{
	FString Extension = FPaths::ToUtf8(ContentItem.Path.extension());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::tolower);

	if (Extension == ".uasset")
	{
		return FPaths::ToUtf8(ContentItem.Path.stem().wstring());
	}

	return FPaths::ToUtf8(ContentItem.Name);
}

void DirectoryElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	Context.CurrentPath = ContentItem.Path;
	Context.PendingRevealPath = ContentItem.Path;
	Context.bPendingContentRefresh = true;
}

void SourceFileElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	FString Extension = FPaths::ToUtf8(ContentItem.Path.extension());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::tolower);
	if (Extension == ".fbx" && Context.OnImportFbxSource)
	{
		const FString SourcePath = FPaths::MakeProjectRelative(FPaths::ToUtf8(ContentItem.Path.generic_wstring()));
		Context.OnImportFbxSource(SourcePath);
		return;
	}

	ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void SceneElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	std::filesystem::path ScenePath = ContentItem.Path;
	FString FilePath = FPaths::ToUtf8(ScenePath.wstring());
	UEditorEngine* EditorEngine = Context.EditorEngine;
	EditorEngine->LoadSceneFromPath(FilePath);
}

void ObjectElement::RenderContextMenu(ContentBrowserContext& Context)
{
	ContentBrowserElement::RenderContextMenu(Context);
	ImGui::Separator();

	FString Extension = FPaths::ToUtf8(ContentItem.Path.extension());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::tolower);

	FString PackagePath = FPaths::ToUtf8(ContentItem.Path.lexically_relative(FPaths::RootDir()).generic_wstring());

	if (Extension == ".uasset" && FMeshManager::IsStaticMeshPackage(PackagePath))
	{
		if (ImGui::MenuItem("Reimport"))
		{
			UStaticMesh* Reimported = nullptr;

			if (Context.EditorEngine)
			{
				ID3D11Device* Device = Context.EditorEngine->GetRenderer().GetFD3DDevice().GetDevice();

				FAssetImportMetadata Metadata;
				if (FAssetPackage::ReadMetadata(PackagePath, EAssetPackageType::StaticMesh, Metadata))
				{
					const FString SourceExtension = GetLowerExtensionFromPath(Metadata.SourcePath);
					if (SourceExtension == ".obj")
					{
						FEditorObjImportService::ImportStaticMeshFromObj(Metadata.SourcePath, Device, Reimported);
					}
					else if (SourceExtension == ".fbx")
					{
						FEditorFbxImportService::ImportStaticMeshFromFbx(Metadata.SourcePath, Device, Reimported);
					}
				}
			}
		}
	}
}

void ObjectElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	FString Extension = FPaths::ToUtf8(ContentItem.Path.extension());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::tolower);

	const FString FilePath = FPaths::ToUtf8(ContentItem.Path.wstring());
	const FString PackagePath = FPaths::ToUtf8(ContentItem.Path.lexically_relative(FPaths::RootDir()).generic_wstring());

	if (Extension == ".uasset" && FMeshManager::IsStaticMeshPackage(PackagePath))
	{
		if (UStaticMesh* MeshAsset = FMeshManager::LoadStaticMesh(FilePath, Context.EditorEngine->GetRenderer().GetFD3DDevice().GetDevice()))
		{
			Context.EditorEngine->OpenAssetEditorForObject(MeshAsset);
		}
		return;
	}

	ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void FloatCurveElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		return;
	}

	const FString FilePath = FPaths::ToUtf8(ContentItem.Path.wstring());
	if (UFloatCurveAsset* CurveAsset = FFloatCurveManager::Get().Load(FilePath))
	{
		Context.EditorEngine->OpenAssetEditorForObject(CurveAsset);
	}
}

void CameraShakeElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		return;
	}
	const FString FilePath = FPaths::ToUtf8(ContentItem.Path.wstring());
	if (UCameraShakeAsset* ShakeAsset = FCameraShakeManager::Get().Load(FilePath))
	{
		Context.EditorEngine->OpenAssetEditorForObject(ShakeAsset);
	}
}

void MeshElement::RenderContextMenu(ContentBrowserContext& Context)
{
	ContentBrowserElement::RenderContextMenu(Context);
	ImGui::Separator();

	FString Extension = FPaths::ToUtf8(ContentItem.Path.extension());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::tolower);

	FString PackagePath = FPaths::ToUtf8(ContentItem.Path.lexically_relative(FPaths::RootDir()).generic_wstring());

	if (Extension == ".uasset" && FMeshManager::IsSkeletalMeshPackage(PackagePath))
	{
		if (ImGui::MenuItem("Reimport"))
		{
			if (Context.EditorEngine)
			{
				FAssetImportMetadata Metadata;
				if (FAssetPackage::ReadMetadata(PackagePath, EAssetPackageType::SkeletalMesh, Metadata)
					&& GetLowerExtensionFromPath(Metadata.SourcePath) == ".fbx")
				{
					TArray<USkeletalMesh*> ReimportedMeshes;
					FEditorFbxImportService::ImportSkeletalMeshesFromFbx(
						Metadata.SourcePath,
						Context.EditorEngine->GetRenderer().GetFD3DDevice().GetDevice(),
						ReimportedMeshes);
				}
			}
		}
	}
}

void MeshElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		return;
	}
	const FString FilePath = FPaths::ToUtf8(ContentItem.Path.wstring());
	if (USkeletalMesh* MeshAsset = FMeshManager::LoadSkeletalMesh(FilePath, Context.EditorEngine->GetRenderer().GetFD3DDevice().GetDevice()))
	{
		Context.EditorEngine->OpenAssetEditorForObject(MeshAsset);
	}
}

void AnimSequenceElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	const FString FilePath = FPaths::ToUtf8(ContentItem.Path.wstring());

	// AnimSequence asset은 SkeletalMesh를 여는 우회 경로를 타면 안 됩니다.
	// 여기서 UAnimSequence를 직접 로드해야 AssetEditorManager가 FAnimSequenceEditorWidget::CanEdit()을 통해
	// 정확히 AnimSequence editor를 선택할 수 있습니다.
	if (UAnimSequence* AnimSequence = FAnimSequenceManager::Get().Load(FilePath))
	{
		Context.EditorEngine->OpenAssetEditorForObject(AnimSequence);
	}
}

void AnimSequenceElement::RenderContextMenu(ContentBrowserContext& Context)
{
	ContentBrowserElement::RenderContextMenu(Context);
	ImGui::Separator();

	FString Extension = FPaths::ToUtf8(ContentItem.Path.extension());
	std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::tolower);

	FString PackagePath = FPaths::ToUtf8(ContentItem.Path.lexically_relative(FPaths::RootDir()).generic_wstring());
	if (Extension == ".uasset" && FAnimSequenceManager::Get().IsAnimSequencePackage(PackagePath))
	{
		if (ImGui::MenuItem("Reimport"))
		{
			FAssetImportMetadata Metadata;
			if (FAssetPackage::ReadMetadata(PackagePath, EAssetPackageType::AnimSequence, Metadata)
				&& GetLowerExtensionFromPath(Metadata.SourcePath) == ".fbx")
			{
				TArray<UAnimSequence*> ReimportedSequences;
				FEditorFbxImportService::ImportAnimSequencesFromFbx(Metadata.SourcePath, ReimportedSequences);
			}
		}
	}
}

void AnimSequenceElement::RenderDetail()
{
	// TODO: 성능 부하가 너무 심해서 주석 처리
	//ContentBrowserElement::RenderDetail();

	//const FString FilePath = FPaths::ToUtf8(ContentItem.Path.wstring());
	//const UAnimSequence* AnimSequence = FAnimSequenceManager::Get().Load(FilePath);
	//if (!AnimSequence)
	//{
	//	ImGui::Spacing();
	//	ImGui::TextDisabled("AnimSequence data could not be loaded.");
	//	return;
	//}

	//const UAnimDataModel* DataModel = AnimSequence->GetDataModel();

	//ImGui::Spacing();
	//ImGui::Separator();
	//ImGui::Spacing();

	//if (ImGui::BeginTable("AnimSequenceAssetDetailsTable", 2, ImGuiTableFlags_SizingStretchProp))
	//{
	//	ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 96.0f);
	//	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

	//	char Buffer[64];

	//	// Content Browser 상세 정보는 asset을 열기 전에도 AnimSequence가 실제로 어떤 시간축 데이터를 들고 있는지
	//	// 확인하기 위한 읽기 전용 영역입니다. 여기서는 UAnimSequence/UAnimDataModel 값을 표시만 하며,
	//	// 원본 keyframe이나 import metadata를 절대 수정하지 않습니다.
	//	std::snprintf(Buffer, sizeof(Buffer), "%.3f sec", AnimSequence->GetPlayLength());
	//	DrawDetailRow("Play Length", Buffer);

	//	std::snprintf(Buffer, sizeof(Buffer), "%.2f fps", AnimSequence->GetSamplingFrameRate());
	//	DrawDetailRow("Frame Rate", Buffer);

	//	std::snprintf(Buffer, sizeof(Buffer), "%d", DataModel ? DataModel->GetNumberOfFrames() : 0);
	//	DrawDetailRow("Frames", Buffer);

	//	std::snprintf(Buffer, sizeof(Buffer), "%d", AnimSequence->GetNumberOfSampledKeys());
	//	DrawDetailRow("Keys", Buffer);

	//	std::snprintf(Buffer, sizeof(Buffer), "%d", DataModel ? DataModel->GetNumBoneTracks() : 0);
	//	DrawDetailRow("Tracks", Buffer);

	//	const FString& SkeletonPath = AnimSequence->GetSkeletonPath();
	//	DrawDetailRow("Skeleton", SkeletonPath.empty() ? FString("None") : SkeletonPath);

	//	if (DataModel && DataModel->GetPlayLength() <= 0.0f && DataModel->GetNumberOfKeys() <= 1)
	//	{
	//		DrawDetailRow("Status", "Single-frame or missing duration");
	//	}

	//	ImGui::EndTable();
	//}
}

void AnimInstanceElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	const FString FilePath = FPaths::ToUtf8(ContentItem.Path.wstring());
	if (UAnimInstanceAsset* AnimInstanceAsset = FAnimInstanceAssetManager::Get().Load(FilePath))
	{
		Context.EditorEngine->OpenAssetEditorForObject(AnimInstanceAsset);
	}
}

void MaterialElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	const FString MaterialPath = FPaths::MakeProjectRelative(
		FPaths::ToUtf8(ContentItem.Path.generic_wstring()));
	if (UMaterial* Material = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath))
	{
		Context.EditorEngine->OpenAssetEditorForObject(Material);
	}
}

void ParticleSystemElement::OnDoubleLeftClicked(ContentBrowserContext& Context)
{
	if (!Context.EditorEngine)
	{
		ShellExecuteW(nullptr, L"open", ContentItem.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	const FString FilePath = FPaths::ToUtf8(ContentItem.Path.wstring());
	if (UParticleSystem* ParticleSystem = FParticleSystemManager::Get().Load(FilePath))
	{
		Context.EditorEngine->OpenAssetEditorForObject(ParticleSystem);
	}
}
