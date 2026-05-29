#include "Editor/UI/ContentBrowser/EditorFbxImportDialog.h"

#include "Asset/AssetPackage.h"
#include "Core/Notification.h"
#include "Editor/EditorEngine.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshImportOptions.h"
#include "Mesh/Skeleton.h"
#include "Mesh/SkeletonManager.h"
#include "Platform/Paths.h"

#include "ImGui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace
{
	std::filesystem::path ResolveProjectPath(const FString& Path)
	{
		std::filesystem::path FullPath(FPaths::ToWide(Path));
		if (!FullPath.is_absolute())
		{
			FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
		}
		return FullPath.lexically_normal();
	}

	FString ToProjectRelativePath(const std::filesystem::path& Path)
	{
		return FPaths::MakeProjectRelative(FPaths::ToUtf8(Path.lexically_normal().generic_wstring()));
	}

	FString NormalizeProjectPathForCompare(const FString& Path)
	{
		FString Normalized = ToProjectRelativePath(ResolveProjectPath(Path));
		std::transform(Normalized.begin(), Normalized.end(), Normalized.begin(),
			[](unsigned char Ch)
			{
				return static_cast<char>(std::tolower(Ch));
			});
		return Normalized;
	}

	FString GetFbxStem(const FString& SourcePath)
	{
		return FPaths::ToUtf8(ResolveProjectPath(SourcePath).stem().wstring());
	}

	bool IsInvalidStemChar(char Ch)
	{
		const unsigned char U = static_cast<unsigned char>(Ch);
		return U < 32 || Ch == '<' || Ch == '>' || Ch == ':' || Ch == '"' ||
			Ch == '/' || Ch == '\\' || Ch == '|' || Ch == '?' || Ch == '*';
	}

	FString SanitizeFileStem(const FString& Name)
	{
		FString Result = Name.empty() ? "None" : Name;
		for (char& Ch : Result)
		{
			if (IsInvalidStemChar(Ch))
			{
				Ch = '_';
			}
		}
		return Result.empty() ? FString("None") : Result;
	}

	void CopyStem(char* Dest, size_t DestSize, const FString& Stem)
	{
		if (!Dest || DestSize == 0)
		{
			return;
		}

		std::snprintf(Dest, DestSize, "%s", Stem.c_str());
	}

	TArray<FString> MakeUniqueStems(const TArray<FString>& Names)
	{
		TMap<FString, int32> UsedCounts;
		TArray<FString> Result;
		Result.reserve(Names.size());
		for (const FString& Name : Names)
		{
			const FString Base = SanitizeFileStem(Name);
			int32& Count = UsedCounts[Base];
			++Count;
			Result.push_back(Count == 1 ? Base : Base + "_" + std::to_string(Count));
		}
		return Result;
	}

	bool IsValidFileStem(const char* Stem, FString& OutMessage)
	{
		if (!Stem || Stem[0] == '\0')
		{
			OutMessage = "File name is required.";
			return false;
		}

		for (const char* Cursor = Stem; *Cursor; ++Cursor)
		{
			if (IsInvalidStemChar(*Cursor))
			{
				OutMessage = "File name contains invalid characters.";
				return false;
			}
		}

		OutMessage.clear();
		return true;
	}

	const char* GetTypeLabel(EFbxImportAssetType Type)
	{
		switch (Type)
		{
		case EFbxImportAssetType::StaticMesh: return "Static Mesh";
		case EFbxImportAssetType::SkeletalMesh: return "Skeletal Mesh";
		case EFbxImportAssetType::AnimSequence: return "Anim Sequence";
		default: return "Asset";
		}
	}

	FString NormalizeBoneNameForCompare(const FString& BoneName)
	{
		const size_t NamespacePos = BoneName.find_last_of(':');
		FString Normalized = NamespacePos == FString::npos ? BoneName : BoneName.substr(NamespacePos + 1);
		std::transform(Normalized.begin(), Normalized.end(), Normalized.begin(),
			[](unsigned char Ch)
			{
				return static_cast<char>(std::tolower(Ch));
			});
		return Normalized;
	}

	struct FSkeletonMatchScore
	{
		bool bCompatible = false;
		int32 Score = 0;
		int32 MatchedTargetBones = 0;
		int32 TargetBoneCount = 0;
		int32 MatchedSourceBones = 0;
		int32 SourceBoneCount = 0;
	};

	int32 CalculateCoveragePercent(int32 MatchedCount, int32 TotalCount)
	{
		return TotalCount > 0 ? (MatchedCount * 100) / TotalCount : 0;
	}

	FSkeletonMatchScore ScoreSkeletonBoneCoverage(const FSkeletonAsset* SkeletonAsset, const TArray<FString>& SourceBoneNames)
	{
		FSkeletonMatchScore Result;
		if (!SkeletonAsset || SourceBoneNames.empty() || SkeletonAsset->Bones.empty())
		{
			return Result;
		}

		TSet<FString> SourceBoneSet;
		SourceBoneSet.reserve(SourceBoneNames.size());
		for (const FString& SourceBoneName : SourceBoneNames)
		{
			const FString NormalizedName = NormalizeBoneNameForCompare(SourceBoneName);
			if (!NormalizedName.empty())
			{
				SourceBoneSet.insert(NormalizedName);
			}
		}

		TArray<FString> TargetBoneNames;
		TSet<FString> TargetBoneSet;
		TargetBoneNames.reserve(SkeletonAsset->Bones.size());
		TargetBoneSet.reserve(SkeletonAsset->Bones.size());
		for (const FBone& Bone : SkeletonAsset->Bones)
		{
			const FString NormalizedName = NormalizeBoneNameForCompare(Bone.Name);
			if (!NormalizedName.empty())
			{
				TargetBoneNames.push_back(NormalizedName);
				TargetBoneSet.insert(NormalizedName);
			}
		}

		Result.TargetBoneCount = static_cast<int32>(TargetBoneNames.size());
		Result.SourceBoneCount = static_cast<int32>(SourceBoneSet.size());
		if (Result.TargetBoneCount == 0 || Result.SourceBoneCount == 0)
		{
			return Result;
		}

		for (const FString& TargetBoneName : TargetBoneNames)
		{
			if (SourceBoneSet.find(TargetBoneName) != SourceBoneSet.end())
			{
				++Result.MatchedTargetBones;
			}
		}

		for (const FString& SourceBoneName : SourceBoneSet)
		{
			if (TargetBoneSet.find(SourceBoneName) != TargetBoneSet.end())
			{
				++Result.MatchedSourceBones;
			}
		}

		bool bExactOrderedMatch = SourceBoneNames.size() == SkeletonAsset->Bones.size();
		if (bExactOrderedMatch)
		{
			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(SourceBoneNames.size()); ++BoneIndex)
			{
				if (NormalizeBoneNameForCompare(SourceBoneNames[BoneIndex]) != NormalizeBoneNameForCompare(SkeletonAsset->Bones[BoneIndex].Name))
				{
					bExactOrderedMatch = false;
					break;
				}
			}
		}

		const int32 TargetCoverage = CalculateCoveragePercent(Result.MatchedTargetBones, Result.TargetBoneCount);
		const int32 SourceCoverage = CalculateCoveragePercent(Result.MatchedSourceBones, Result.SourceBoneCount);
		const bool bExactSetMatch = Result.MatchedTargetBones == Result.TargetBoneCount && Result.MatchedSourceBones == Result.SourceBoneCount;

		Result.bCompatible = bExactOrderedMatch || bExactSetMatch ||
			(TargetCoverage == 100 && SourceCoverage >= 50) ||
			(TargetCoverage >= 90 && SourceCoverage >= 60);
		Result.Score = TargetCoverage * 1000 + SourceCoverage * 100 + Result.MatchedTargetBones;
		if (TargetCoverage == 100)
		{
			Result.Score += 10000;
		}
		if (bExactSetMatch)
		{
			Result.Score += 50000;
		}
		if (bExactOrderedMatch)
		{
			Result.Score += 100000;
		}

		return Result;
	}

	const FSkeletonAsset* GetSkeletonAsset(const FMeshAssetListItem& Item)
	{
		USkeleton* Skeleton = FSkeletonManager::Get().Load(Item.FullPath);
		return Skeleton ? Skeleton->GetSkeletonAsset() : nullptr;
	}

}

void FEditorFbxImportDialog::Open(const FString& InSourcePath)
{
	Reset();
	SourcePath = FPaths::MakeProjectRelative(InSourcePath);
	bIsOpen = true;
	bOpenPopup = true;

	bInspectSucceeded = FEditorFbxImportService::InspectFbxSource(SourcePath, SourceInfo);
	if (!bInspectSucceeded)
	{
		StatusMessages.push_back("Failed to inspect FBX source.");
		return;
	}

	TargetSkeletonOptions = ScanSkeletonAssets();
	PendingStaticFbxSkinnedMeshPolicy =
		FImportOptions::Default().StaticFbxSkinnedMeshPolicy == EStaticFbxSkinnedMeshPolicy::ImportBindPoseAsStatic ? 1 : 0;
	BuildRows();
	AutoSelectTargetSkeleton();
	ValidateRows();
}

bool FEditorFbxImportDialog::Render(UEditorEngine* EditorEngine)
{
	bool bCompletedThisFrame = false;

	if (bOpenPopup)
	{
		ImGui::OpenPopup(PopupTitle);
		bOpenPopup = false;
	}

	if (!ImGui::BeginPopupModal(PopupTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return false;
	}

	ValidateRows();
	ImGui::SetNextItemWidth(720.0f);
	RenderSummary();
	ImGui::Separator();

	if (!bInspectSucceeded)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Inspect failed.");
	}
	else
	{
		RenderStaticOptions();
		RenderTargetSkeletonCombo();
		RenderSection("Static Meshes", StaticMeshRows);
		RenderSection("Skeletal Meshes", SkeletalMeshRows);
		RenderSection("Animation Sequences", AnimSequenceRows);
	}

	if (!StatusMessages.empty())
	{
		ImGui::Separator();
		for (const FString& Message : StatusMessages)
		{
			ImGui::TextWrapped("%s", Message.c_str());
		}
	}

	if (bImportedThisSession)
	{
		ImGui::Separator();
		ImGui::Text("Imported: Static %d, Skeletal %d, Anim %d",
			LastResult.StaticMeshCount,
			LastResult.SkeletalMeshCount,
			LastResult.AnimSequenceCount);
	}

	ImGui::Separator();
	const bool bCanImport = bInspectSucceeded && bAnySelected && !bHasValidationErrors && !bImportedThisSession;
	ImGui::BeginDisabled(!bCanImport);
	if (ImGui::Button("Import"))
	{
		bCompletedThisFrame = ExecuteImport(EditorEngine);
	}
	ImGui::EndDisabled();

	if (!bAnySelected && bInspectSucceeded && !bImportedThisSession)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("Select at least one item.");
	}
	else if (bHasValidationErrors && !bImportedThisSession)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("Resolve file name errors.");
	}

	ImGui::SameLine();
	if (ImGui::Button(bImportedThisSession ? "Close" : "Cancel"))
	{
		bIsOpen = false;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
	return bCompletedThisFrame;
}

void FEditorFbxImportDialog::Reset()
{
	bIsOpen = false;
	bOpenPopup = false;
	bInspectSucceeded = false;
	bImportedThisSession = false;
	bAnySelected = false;
	bHasValidationErrors = false;

	SourcePath.clear();
	SourceInfo = FFbxImportSourceInfo();
	LastResult = FFbxImportResult();
	StatusMessages.clear();
	StaticMeshRows.clear();
	SkeletalMeshRows.clear();
	AnimSequenceRows.clear();
	SelectedTargetSkeletonIndex = 0;
	bCombineStaticMeshes = false;
	CombinedStaticMeshStem[0] = '\0';
	CombinedStaticMeshPackagePath.clear();
	CombinedStaticMeshValidationMessage.clear();
	bCombinedStaticMeshWillOverwrite = false;
	TargetSkeletonOptions.clear();
}

void FEditorFbxImportDialog::BuildRows()
{
	const FString FbxStem = GetFbxStem(SourcePath);
	bCombineStaticMeshes = SourceInfo.Meshes.size() > 1;
	CopyStem(CombinedStaticMeshStem, sizeof(CombinedStaticMeshStem), FbxStem + "_StaticMesh");

	TArray<FString> StaticNames;
	StaticNames.reserve(SourceInfo.Meshes.size());
	for (const FFbxImportMeshInfo& MeshInfo : SourceInfo.Meshes)
	{
		StaticNames.push_back(MeshInfo.Name);
	}
	const TArray<FString> StaticUniqueNames = MakeUniqueStems(StaticNames);
	const bool bSingleStaticMesh = SourceInfo.Meshes.size() <= 1;
	for (int32 Index = 0; Index < static_cast<int32>(SourceInfo.Meshes.size()); ++Index)
	{
		const FFbxImportMeshInfo& MeshInfo = SourceInfo.Meshes[Index];
		FImportRow Row;
		Row.Type = EFbxImportAssetType::StaticMesh;
		Row.bImport = !bCombineStaticMeshes;
		Row.bSkinned = MeshInfo.bSkinned;
		Row.SourceIndex = MeshInfo.SourceIndex;
		Row.SourceName = MeshInfo.Name;
		Row.DetailText = FString(MeshInfo.bSkinned ? "Skinned, " : "Mesh, ") +
			std::to_string(MeshInfo.PolygonCount) + " polys, " +
			std::to_string(MeshInfo.MaterialCount) + " materials";
		const FString Stem = bSingleStaticMesh ? FbxStem + "_StaticMesh" : FbxStem + "_" + StaticUniqueNames[Index] + "_StaticMesh";
		CopyStem(Row.FileStem, sizeof(Row.FileStem), Stem);
		StaticMeshRows.push_back(Row);
	}

	TArray<FString> SkeletalNames;
	SkeletalNames.reserve(SourceInfo.SkeletalMeshes.size());
	for (const FFbxImportSkeletalMeshInfo& MeshInfo : SourceInfo.SkeletalMeshes)
	{
		SkeletalNames.push_back(MeshInfo.Name);
	}
	const TArray<FString> SkeletalUniqueNames = MakeUniqueStems(SkeletalNames);
	const bool bSingleSkeletalMesh = SourceInfo.SkeletalMeshes.size() <= 1;
	for (int32 Index = 0; Index < static_cast<int32>(SourceInfo.SkeletalMeshes.size()); ++Index)
	{
		const FFbxImportSkeletalMeshInfo& MeshInfo = SourceInfo.SkeletalMeshes[Index];
		FImportRow Row;
		Row.Type = EFbxImportAssetType::SkeletalMesh;
		Row.bImport = false;
		Row.SourceIndex = MeshInfo.SourceIndex;
		Row.SourceName = MeshInfo.Name;
		Row.DetailText = std::to_string(MeshInfo.MaterialCount) + " materials";
		const FString Stem = bSingleSkeletalMesh ? FbxStem + "_SkeletalMesh" : FbxStem + "_" + SkeletalUniqueNames[Index] + "_SkeletalMesh";
		CopyStem(Row.FileStem, sizeof(Row.FileStem), Stem);
		SkeletalMeshRows.push_back(Row);
	}

	TArray<FString> StackNames;
	StackNames.reserve(SourceInfo.AnimStacks.size());
	for (const FFbxImportAnimStackInfo& StackInfo : SourceInfo.AnimStacks)
	{
		StackNames.push_back(StackInfo.Name);
	}
	const TArray<FString> StackUniqueNames = MakeUniqueStems(StackNames);
	const bool bSingleStack = SourceInfo.AnimStacks.size() <= 1;
	for (int32 Index = 0; Index < static_cast<int32>(SourceInfo.AnimStacks.size()); ++Index)
	{
		const FFbxImportAnimStackInfo& StackInfo = SourceInfo.AnimStacks[Index];
		FImportRow Row;
		Row.Type = EFbxImportAssetType::AnimSequence;
		Row.bImport = false;
		Row.SourceIndex = StackInfo.SourceIndex;
		Row.SourceName = StackInfo.Name;
		Row.DetailText = "AnimStack";
		const FString Stem = bSingleStack ? FbxStem + "_AnimSequence" : FbxStem + "_" + StackUniqueNames[Index] + "_AnimSequence";
		CopyStem(Row.FileStem, sizeof(Row.FileStem), Stem);
		AnimSequenceRows.push_back(Row);
	}
}

void FEditorFbxImportDialog::ValidateRows()
{
	bAnySelected = false;
	bHasValidationErrors = false;
	CombinedStaticMeshPackagePath = BuildPackagePath(CombinedStaticMeshStem);
	CombinedStaticMeshValidationMessage.clear();
	bCombinedStaticMeshWillOverwrite = false;

	TMap<FString, int32> PackageUseCounts;
	if (bCombineStaticMeshes && !StaticMeshRows.empty())
	{
		bAnySelected = true;
		FString ValidationMessage;
		if (!IsValidFileStem(CombinedStaticMeshStem, ValidationMessage))
		{
			CombinedStaticMeshValidationMessage = ValidationMessage;
			bHasValidationErrors = true;
		}
		else
		{
			++PackageUseCounts[CombinedStaticMeshPackagePath];
			bCombinedStaticMeshWillOverwrite = std::filesystem::exists(ResolveProjectPath(CombinedStaticMeshPackagePath));
		}
	}

	auto ValidateRowSet = [&](TArray<FImportRow>& Rows)
	{
		for (FImportRow& Row : Rows)
		{
			Row.PackagePath = BuildPackagePath(Row.FileStem);
			Row.ValidationMessage.clear();
			Row.bWillOverwrite = false;

			if ((bCombineStaticMeshes && Row.Type == EFbxImportAssetType::StaticMesh) || !IsRowVisible(Row) || !Row.bImport)
			{
				continue;
			}

			bAnySelected = true;
			FString ValidationMessage;
			if (!IsValidFileStem(Row.FileStem, ValidationMessage))
			{
				Row.ValidationMessage = ValidationMessage;
				bHasValidationErrors = true;
				continue;
			}

			++PackageUseCounts[Row.PackagePath];
			Row.bWillOverwrite = std::filesystem::exists(ResolveProjectPath(Row.PackagePath));
		}
	};

	ValidateRowSet(StaticMeshRows);
	ValidateRowSet(SkeletalMeshRows);
	ValidateRowSet(AnimSequenceRows);

	auto MarkDuplicates = [&](TArray<FImportRow>& Rows)
	{
		for (FImportRow& Row : Rows)
		{
			if (!IsRowVisible(Row) || !Row.bImport)
			{
				continue;
			}

			auto It = PackageUseCounts.find(Row.PackagePath);
			if (It != PackageUseCounts.end() && It->second > 1)
			{
				Row.ValidationMessage = "Duplicate output path.";
				bHasValidationErrors = true;
			}
		}
	};

	MarkDuplicates(StaticMeshRows);
	MarkDuplicates(SkeletalMeshRows);
	MarkDuplicates(AnimSequenceRows);
}

void FEditorFbxImportDialog::RenderSummary() const
{
	ImGui::TextUnformatted(SourcePath.c_str());
	if (!bInspectSucceeded)
	{
		return;
	}

	ImGui::TextDisabled(
		"%d mesh nodes, %d skeletal mesh groups, %d anim stacks, %d materials, %d skeleton nodes",
		static_cast<int32>(SourceInfo.Meshes.size()),
		static_cast<int32>(SourceInfo.SkeletalMeshes.size()),
		static_cast<int32>(SourceInfo.AnimStacks.size()),
		SourceInfo.MaterialCount,
		SourceInfo.SkeletonNodeCount);
}

void FEditorFbxImportDialog::RenderStaticOptions()
{
	if (StaticMeshRows.size() > 1)
	{
		ImGui::TextUnformatted("Static import");
		ImGui::Checkbox("Combine Static Meshes", &bCombineStaticMeshes);
		if (bCombineStaticMeshes)
		{
			ImGui::InputText("Combined Asset Name", CombinedStaticMeshStem, sizeof(CombinedStaticMeshStem));
			ImGui::TextWrapped("%s", CombinedStaticMeshPackagePath.c_str());
			if (!CombinedStaticMeshValidationMessage.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", CombinedStaticMeshValidationMessage.c_str());
			}
			else if (bCombinedStaticMeshWillOverwrite)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Overwrite");
			}
		}
	}

	if (!HasSkinnedStaticMeshRows())
	{
		return;
	}

	if (StaticMeshRows.size() <= 1)
	{
		ImGui::TextUnformatted("Static import");
	}
	ImGui::RadioButton("Skip skinned mesh for static import", &PendingStaticFbxSkinnedMeshPolicy, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Import bind pose as static mesh", &PendingStaticFbxSkinnedMeshPolicy, 1);
}

void FEditorFbxImportDialog::RenderTargetSkeletonCombo()
{
	if (AnimSequenceRows.empty())
	{
		return;
	}

	const char* PreviewSkeleton = "None (Adjacent/Create)";
	if (SelectedTargetSkeletonIndex > 0 && SelectedTargetSkeletonIndex <= static_cast<int32>(TargetSkeletonOptions.size()))
	{
		PreviewSkeleton = TargetSkeletonOptions[SelectedTargetSkeletonIndex - 1].FullPath.c_str();
	}

	if (ImGui::BeginCombo("Target Skeleton", PreviewSkeleton))
	{
		const bool bNoneSelected = SelectedTargetSkeletonIndex == 0;
		if (ImGui::Selectable("None (Adjacent/Create)", bNoneSelected))
		{
			SelectedTargetSkeletonIndex = 0;
		}

		for (int32 Index = 0; Index < static_cast<int32>(TargetSkeletonOptions.size()); ++Index)
		{
			const bool bSelected = SelectedTargetSkeletonIndex == Index + 1;
			const FMeshAssetListItem& Item = TargetSkeletonOptions[Index];
			if (ImGui::Selectable(Item.FullPath.c_str(), bSelected))
			{
				SelectedTargetSkeletonIndex = Index + 1;
			}
		}
		ImGui::EndCombo();
	}
}

void FEditorFbxImportDialog::RenderSection(const char* Label, TArray<FImportRow>& Rows)
{
	if (Rows.empty())
	{
		ImGui::TextDisabled("%s: none detected", Label);
		return;
	}

	if (!HasVisibleRows(Rows))
	{
		return;
	}

	if (!ImGui::CollapsingHeader(Label, ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}

	if (!ImGui::BeginTable(Label, 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
	{
		return;
	}

	ImGui::TableSetupColumn("Import", ImGuiTableColumnFlags_WidthFixed, 56.0f);
	ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 1.1f);
	ImGui::TableSetupColumn("File Name", ImGuiTableColumnFlags_WidthStretch, 1.1f);
	ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch, 1.4f);
	ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.8f);
	ImGui::TableHeadersRow();

	for (FImportRow& Row : Rows)
	{
		if (!IsRowVisible(Row))
		{
			continue;
		}

		ImGui::PushID(&Row);
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::Checkbox("##Import", &Row.bImport);

		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted(Row.SourceName.c_str());
		if (!Row.DetailText.empty())
		{
			ImGui::TextDisabled("%s", Row.DetailText.c_str());
		}

		ImGui::TableSetColumnIndex(2);
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputText("##FileStem", Row.FileStem, sizeof(Row.FileStem));

		ImGui::TableSetColumnIndex(3);
		ImGui::TextWrapped("%s", Row.PackagePath.c_str());

		ImGui::TableSetColumnIndex(4);
		if (!Row.ValidationMessage.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "%s", Row.ValidationMessage.c_str());
		}
		else if (Row.bImport && Row.bWillOverwrite)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Overwrite");
		}
		else if (Row.bImport)
		{
			ImGui::TextDisabled("%s", GetTypeLabel(Row.Type));
		}
		else
		{
			ImGui::TextDisabled("Skipped");
		}

		ImGui::PopID();
	}

	ImGui::EndTable();
}

bool FEditorFbxImportDialog::IsRowVisible(const FImportRow& Row) const
{
	if (Row.Type == EFbxImportAssetType::StaticMesh && Row.bSkinned && PendingStaticFbxSkinnedMeshPolicy == 0)
	{
		return false;
	}

	return true;
}

bool FEditorFbxImportDialog::HasVisibleRows(const TArray<FImportRow>& Rows) const
{
	for (const FImportRow& Row : Rows)
	{
		if (IsRowVisible(Row))
		{
			return true;
		}
	}

	return false;
}

bool FEditorFbxImportDialog::HasSkinnedStaticMeshRows() const
{
	for (const FImportRow& Row : StaticMeshRows)
	{
		if (Row.Type == EFbxImportAssetType::StaticMesh && Row.bSkinned)
		{
			return true;
		}
	}

	return false;
}

bool FEditorFbxImportDialog::ExecuteImport(UEditorEngine* EditorEngine)
{
	if (!EditorEngine)
	{
		StatusMessages.push_back("Editor engine is not available.");
		return false;
	}

	FFbxImportRequest Request;
	Request.SourcePath = SourcePath;
	Request.bRefreshAssetLists = true;
	Request.bCombineStaticMeshes = bCombineStaticMeshes && !StaticMeshRows.empty();
	Request.CombinedStaticMeshPackagePath = CombinedStaticMeshPackagePath;
	Request.StaticMeshOptions = FImportOptions::Default();
	Request.StaticMeshOptions.StaticFbxSkinnedMeshPolicy = PendingStaticFbxSkinnedMeshPolicy == 1
		? EStaticFbxSkinnedMeshPolicy::ImportBindPoseAsStatic
		: EStaticFbxSkinnedMeshPolicy::Skip;

	if (SelectedTargetSkeletonIndex > 0 && SelectedTargetSkeletonIndex <= static_cast<int32>(TargetSkeletonOptions.size()))
	{
		Request.TargetSkeletonPath = TargetSkeletonOptions[SelectedTargetSkeletonIndex - 1].FullPath;
	}

	auto AppendRows = [this](const TArray<FImportRow>& Rows, TArray<FFbxImportItemRequest>& OutItems)
	{
		for (const FImportRow& Row : Rows)
		{
			if (!IsRowVisible(Row) || !Row.bImport)
			{
				continue;
			}

			FFbxImportItemRequest Item;
			Item.bImport = true;
			Item.SourceIndex = Row.SourceIndex;
			Item.SourceName = Row.SourceName;
			Item.PackagePath = Row.PackagePath;
			OutItems.push_back(std::move(Item));
		}
	};

	if (!Request.bCombineStaticMeshes)
	{
		AppendRows(StaticMeshRows, Request.StaticMeshes);
	}
	AppendRows(SkeletalMeshRows, Request.SkeletalMeshes);
	AppendRows(AnimSequenceRows, Request.AnimSequences);

	LastResult = FFbxImportResult();
	ID3D11Device* Device = EditorEngine->GetRenderer().GetFD3DDevice().GetDevice();
	const bool bSucceeded = FEditorFbxImportService::ImportFromRequest(Request, Device, LastResult);

	StatusMessages = LastResult.Messages;
	if (bSucceeded)
	{
		bImportedThisSession = true;
		bIsOpen = false;
		ImGui::CloseCurrentPopup();
		FNotificationManager::Get().AddNotification("FBX import completed.", ENotificationType::Success, 3.0f);
	}
	else
	{
		FNotificationManager::Get().AddNotification("FBX import failed. See log.", ENotificationType::Error, 5.0f);
	}

	return bSucceeded;
}

void FEditorFbxImportDialog::AutoSelectTargetSkeleton()
{
	SelectedTargetSkeletonIndex = 0;
	if (SourceInfo.AnimStacks.empty() || TargetSkeletonOptions.empty())
	{
		return;
	}

	const int32 AdjacentSkeletonIndex = FindTargetSkeletonOptionIndex(FEditorFbxImportService::GetSkeletonPackagePathForFbx(SourcePath));
	if (AdjacentSkeletonIndex > 0)
	{
		if (SourceInfo.SkeletonBoneNames.empty())
		{
			SelectedTargetSkeletonIndex = AdjacentSkeletonIndex;
			return;
		}

		const FSkeletonAsset* AdjacentSkeletonAsset = GetSkeletonAsset(TargetSkeletonOptions[AdjacentSkeletonIndex - 1]);
		if (ScoreSkeletonBoneCoverage(AdjacentSkeletonAsset, SourceInfo.SkeletonBoneNames).bCompatible)
		{
			SelectedTargetSkeletonIndex = AdjacentSkeletonIndex;
			return;
		}
	}

	if (SourceInfo.SkeletonBoneNames.empty())
	{
		return;
	}

	const std::filesystem::path SourceDirectory = ResolveProjectPath(SourcePath).parent_path();
	int32 BestIndex = 0;
	int32 BestScore = -1;
	for (int32 Index = 0; Index < static_cast<int32>(TargetSkeletonOptions.size()); ++Index)
	{
		const FSkeletonAsset* SkeletonAsset = GetSkeletonAsset(TargetSkeletonOptions[Index]);
		FSkeletonMatchScore MatchScore = ScoreSkeletonBoneCoverage(SkeletonAsset, SourceInfo.SkeletonBoneNames);
		if (!MatchScore.bCompatible)
		{
			continue;
		}

		int32 Score = MatchScore.Score;
		if (ResolveProjectPath(TargetSkeletonOptions[Index].FullPath).parent_path() == SourceDirectory)
		{
			Score += 1000;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestIndex = Index + 1;
		}
	}

	SelectedTargetSkeletonIndex = BestIndex;
}

int32 FEditorFbxImportDialog::FindTargetSkeletonOptionIndex(const FString& PackagePath) const
{
	const FString TargetPath = NormalizeProjectPathForCompare(PackagePath);
	for (int32 Index = 0; Index < static_cast<int32>(TargetSkeletonOptions.size()); ++Index)
	{
		if (NormalizeProjectPathForCompare(TargetSkeletonOptions[Index].FullPath) == TargetPath)
		{
			return Index + 1;
		}
	}

	return 0;
}

FString FEditorFbxImportDialog::BuildPackagePath(const char* FileStem) const
{
	const std::filesystem::path SourceFullPath = ResolveProjectPath(SourcePath);
	const std::filesystem::path PackagePath = SourceFullPath.parent_path() / FPaths::ToWide(FString(FileStem ? FileStem : "") + ".uasset");
	return ToProjectRelativePath(PackagePath);
}

TArray<FMeshAssetListItem> FEditorFbxImportDialog::ScanSkeletonAssets() const
{
	TArray<FMeshAssetListItem> Items;
	const std::filesystem::path AssetRoot(FPaths::AssetDir());
	if (!std::filesystem::exists(AssetRoot))
	{
		return Items;
	}

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(AssetRoot))
	{
		if (!Entry.is_regular_file() || Entry.path().extension() != L".uasset")
		{
			continue;
		}

		const FString PackagePath = ToProjectRelativePath(Entry.path());
		EAssetPackageType PackageType = EAssetPackageType::Unknown;
		if (!FAssetPackage::GetPackageType(PackagePath, PackageType) || PackageType != EAssetPackageType::Skeleton)
		{
			continue;
		}

		FMeshAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
		Item.FullPath = PackagePath;
		Items.push_back(Item);
	}

	std::sort(Items.begin(), Items.end(),
		[](const FMeshAssetListItem& A, const FMeshAssetListItem& B)
		{
			return A.FullPath < B.FullPath;
		});

	return Items;
}
