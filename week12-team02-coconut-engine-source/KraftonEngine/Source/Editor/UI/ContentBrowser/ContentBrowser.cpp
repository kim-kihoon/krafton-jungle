#include "ContentBrowser.h"

#include "Animation/AnimInstanceAsset.h"
#include "Animation/AnimInstanceAssetManager.h"
#include "Animation/AnimSequenceManager.h"
#include "Asset/AssetPackage.h"
#include "CameraShake/CameraShakeAsset.h"
#include "CameraShake/CameraShakeManager.h"
#include "ContentBrowserElement.h"
#include "Core/Log.h"
#include "Editor/Import/EditorFbxImportService.h"
#include "Editor/Import/EditorObjImportService.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Subsystem/AssetFactory.h"
#include "Editor/UI/EditorTextureManager.h"
#include "FloatCurve/FloatCurveAsset.h"
#include "FloatCurve/FloatCurveManager.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshImportOptions.h"
#include "Mesh/MeshManager.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemManager.h"
#include "EditorEngine.h"

#include <Windows.h>
#include <commdlg.h>
#include <algorithm>
#include <cctype>

namespace
{
	bool IsParentDirectoryReference(const std::filesystem::path& Path)
	{
		for (const std::filesystem::path& Part : Path)
		{
			if (Part == L"..")
			{
				return true;
			}
		}

		return false;
	}

	FString MakeContentBrowserSettingsPath(const std::wstring& CurrentPath)
	{
		const std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		const std::filesystem::path Path = std::filesystem::path(CurrentPath).lexically_normal();
		const std::filesystem::path RelativePath = Path.lexically_relative(RootPath);

		if (!RelativePath.empty() && !IsParentDirectoryReference(RelativePath))
		{
			return FPaths::ToUtf8(RelativePath.generic_wstring());
		}

		return FPaths::ToUtf8(Path.wstring());
	}

	std::wstring ResolveContentBrowserSettingsPath(const FString& SavedPath)
	{
		if (SavedPath.empty())
		{
			return FPaths::RootDir();
		}

		std::filesystem::path Path(FPaths::ToWide(SavedPath));
		if (!Path.is_absolute())
		{
			Path = std::filesystem::path(FPaths::RootDir()) / Path;
		}

		Path = Path.lexically_normal();
		if (std::filesystem::exists(Path) && std::filesystem::is_directory(Path))
		{
			return Path.wstring();
		}

		return FPaths::RootDir();
	}

	bool IsSubPath(const std::filesystem::path& Parent, const std::filesystem::path& Child)
	{
		std::filesystem::path P = std::filesystem::weakly_canonical(Parent);
		std::filesystem::path C = std::filesystem::weakly_canonical(Child);

		auto PIt = P.begin();
		auto CIt = C.begin();

		for (; PIt != P.end() && CIt != C.end(); ++PIt, ++CIt)
		{
			if (*PIt != *CIt)
			{
				return false;
			}
		}

		return PIt == P.end();
	}

	FString GetLowerExtension(const std::filesystem::path& Path)
	{
		FString Extension = FPaths::ToUtf8(Path.extension().wstring());
		std::transform(Extension.begin(), Extension.end(), Extension.begin(),
			[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
		return Extension;
	}

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

	FString OpenImportSourceFileDialog(const std::wstring& InitialDirectory)
	{
		wchar_t FilePath[MAX_PATH] = {};
		std::wstring InitialDir = InitialDirectory.empty() ? FPaths::AssetDir() : InitialDirectory;

		OPENFILENAMEW Ofn = {};
		Ofn.lStructSize = sizeof(Ofn);
		Ofn.hwndOwner = nullptr;
		Ofn.lpstrFilter = L"Asset Source Files (*.obj;*.fbx)\0*.obj;*.fbx\0OBJ Files (*.obj)\0*.obj\0FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
		Ofn.lpstrFile = FilePath;
		Ofn.nMaxFile = MAX_PATH;
		Ofn.lpstrInitialDir = InitialDir.c_str();
		Ofn.lpstrTitle = L"Import Asset Source";
		Ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

		if (!GetOpenFileNameW(&Ofn))
		{
			return FString();
		}

		return FPaths::ToUtf8(std::filesystem::path(FilePath).lexically_normal().generic_wstring());
	}

	bool IsEditorSourceFile(const std::filesystem::path& Path)
	{
		const FString Extension = GetLowerExtension(Path);
		return Extension == ".obj" || Extension == ".mtl" || Extension == ".fbx";
	}
}

void FEditorContentBrowserWidget::Initialize(UEditorEngine* InEditor, ID3D11Device* InDevice)
{
	FEditorWidget::Initialize(InEditor);
	if (!InDevice) return;

	IconFileMap[".Scene"] = L"World_64x.png";
	IconFileMap[".obj"] = L"icon_MatEd_Mesh_40x.png";
	IconFileMap[".mat"] = L"Sphere_64x.png";
	IconFileMap[".shake"] = L"StartMerge_42x.png";
	IconFileMap[".fbx"] = L"icon_MatEd_Mesh_40x.png";
	IconFileMap[".uasset"] = L"icon_MatEd_Mesh_40x.png";

	ContentBrowserContext Context;
	Context.ContentSize = ImVec2(112, 112);
	Context.EditorEngine = InEditor;
	Context.OnImportFbxSource = [this](const FString& SourcePath)
	{
		BeginFbxImport(SourcePath);
	};
	BrowserContext = Context;
	LoadFromSettings();

	Refresh();
}

void FEditorContentBrowserWidget::Render(float DeltaTime)
{
	Render(DeltaTime, nullptr);
}

void FEditorContentBrowserWidget::Render(float DeltaTime, bool* bOpen)
{
	(void)DeltaTime;

	if (!ImGui::Begin("ContentBrowser", bOpen))
	{
		ImGui::End();
		return;
	}

	RenderBody();

	ImGui::End();
}

void FEditorContentBrowserWidget::RenderBody()
{
	if (ImGui::Button("Import"))
	{
		BeginImportSourceFile();
	}

	ImGui::SameLine();
	if (BrowserContext.bPendingContentRefresh)
	{
		RefreshContent();
		BrowserContext.bPendingContentRefresh = false;
	}

	std::wstring PathText = BrowserContext.CurrentPath;
	if (BrowserContext.SelectedElement)
		PathText += L"/" + BrowserContext.SelectedElement->GetFileName();

	int Size = static_cast<int>(BrowserContext.ContentSize.x);
	BrowserContext.ContentSize = ImVec2(static_cast<float>(Size), static_cast<float>(Size));

	bool bShowSourceFiles = BrowserContext.bShowSourceFiles;
	if (ImGui::Checkbox("Source Files", &bShowSourceFiles))
	{
		BrowserContext.bShowSourceFiles = bShowSourceFiles;
		RefreshContent();
	}

	RenderFbxImportPopup();

	if (!ImGui::BeginTable("ContentBrowserLayout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
	{
		return;
	}

	ImGui::TableSetupColumn("Directory", ImGuiTableColumnFlags_WidthFixed, 250.0f);
	ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 260.0f);

	ImGui::TableNextColumn();
	{
		ImGui::BeginChild("DirectoryTree", ImVec2(0, 0), true);
		DrawDirNode(RootNode);
		BrowserContext.PendingRevealPath.clear();
		ImGui::EndChild();
	}

	ImGui::TableNextColumn();
	{
		ImGui::BeginChild("ContentArea", ImVec2(0, 0), true);
		DrawContents();
		ImGui::EndChild();
	}

	ImGui::TableNextColumn();
	{
		ImGui::BeginChild("DetailsPanel", ImVec2(0, 0), true);

		if (BrowserContext.SelectedElement)
		{
			BrowserContext.SelectedElement->RenderDetail();
		}
		else
		{
			ImGui::TextDisabled("No asset selected");
		}

		ImGui::EndChild();
	}

	ImGui::EndTable();
}

void FEditorContentBrowserWidget::Refresh()
{
	RootNode = BuildDirectoryTree(FPaths::RootDir());
	RefreshContent();

	BrowserContext.bPendingContentRefresh = false;
}

void FEditorContentBrowserWidget::SetIconSize(float Size)
{
	const float ClampedSize = (std::max)(72.0f, (std::min)(Size, 160.0f));
	BrowserContext.ContentSize = ImVec2(ClampedSize, ClampedSize);
}

void FEditorContentBrowserWidget::LoadFromSettings()
{
	BrowserContext.CurrentPath = ResolveContentBrowserSettingsPath(FEditorSettings::Get().ContentBrowserPath);
	BrowserContext.PendingRevealPath = BrowserContext.CurrentPath;
}

void FEditorContentBrowserWidget::SaveToSettings() const
{
	FEditorSettings::Get().ContentBrowserPath = MakeContentBrowserSettingsPath(BrowserContext.CurrentPath);
}

void FEditorContentBrowserWidget::RefreshContent()
{
	FEditorTextureManager::Get().ClearThumbnails();
	CachedBrowserElements.clear();
	TArray<FContentItem> CurrentContents = ReadDirectory(BrowserContext.CurrentPath);
	for (const auto& Content : CurrentContents)
	{
		std::shared_ptr<ContentBrowserElement> Element;
		FString Extension = GetLowerExtension(Content.Path);
		ID3D11ShaderResourceView* Icon = nullptr;

		if (Content.bIsDirectory)
		{
			Element = std::make_shared<DirectoryElement>();
			Icon = FEditorTextureManager::Get().GetOrLoadIcon(FPaths::ToUtf8(FPaths::Combine(FPaths::AssetDir(), L"Editor/Icons/", L"Folder_Base_256x.png")));
		}
		else if (IsEditorSourceFile(Content.Path))
		{
			Element = std::make_shared<SourceFileElement>();
		}
		else if (Extension == ".scene")
		{
			Element = std::make_shared<SceneElement>();
		}
		else if (Extension == ".mat")
		{
			Element = std::make_shared<MaterialElement>();
		}
		else if (Extension == ".curve")
		{
			Element = std::make_shared<FloatCurveElement>();
		}
		else if (Extension == ".shake")
		{
			Element = std::make_shared<CameraShakeElement>();
		}
		else if (Extension == ".fbx")
		{
			Element = std::make_shared<MeshElement>();
		}
		else if (Extension == ".png")
		{
			Element = std::make_shared<PNGElement>();
			Icon = FEditorTextureManager::Get().GetOrLoadThumbnail(FPaths::ToUtf8(Content.Path.lexically_relative(FPaths::RootDir()).generic_wstring()));
		}
		else if (Extension == ".uasset")
		{
			FString PackagePath = FPaths::ToUtf8(Content.Path.lexically_relative(FPaths::RootDir()).generic_wstring());

			EAssetPackageType Type = EAssetPackageType::Unknown;
			if (FAssetPackage::GetPackageType(PackagePath, Type))
			{
				switch (Type)
				{
				case EAssetPackageType::StaticMesh:
					Element = std::make_shared<ObjectElement>();
					break;
				case EAssetPackageType::SkeletalMesh:
					Element = std::make_shared<MeshElement>();
					break;
				case EAssetPackageType::Skeleton:
					Element = std::make_shared<SkeletonElement>();
					break;
				case EAssetPackageType::AnimSequence:
					if (FAnimSequenceManager::Get().IsAnimSequencePackage(PackagePath))
					{
						Element = std::make_shared<AnimSequenceElement>();
					}
					else
					{
						Element = std::make_shared<ContentBrowserElement>();
					}
					break;
				case EAssetPackageType::AnimInstance:
					if (FAnimInstanceAssetManager::Get().IsAnimInstanceAssetPackage(PackagePath))
					{
						Element = std::make_shared<AnimInstanceElement>();
					}
					else
					{
						Element = std::make_shared<ContentBrowserElement>();
					}
					break;
				case EAssetPackageType::FloatCurve:
					Element = std::make_shared<FloatCurveElement>();
					break;
				case EAssetPackageType::CameraShake:
					Element = std::make_shared<CameraShakeElement>();
					break;
				case EAssetPackageType::ParticleSystem:
					Element = std::make_shared<ParticleSystemElement>();
					break;
				default:
					Element = std::make_shared<ContentBrowserElement>();
					break;
				}
			}
		}
		else
		{
			Element = std::make_shared<ContentBrowserElement>();
		}

		if (!Icon)
		{
			std::wstring IconFileName = L"StartMerge_42x.png";
			if (auto It = IconFileMap.find(Extension); It != IconFileMap.end())
			{
				IconFileName = It->second;
			}

			Icon = FEditorTextureManager::Get().GetOrLoadIcon(FPaths::ToUtf8(FPaths::Combine(FPaths::AssetDir(), L"Editor/Icons/", IconFileName)));
		}

		Element->SetIcon(Icon);

		Element->SetContent(Content);
		CachedBrowserElements.push_back(std::move(Element));
	}
}

void FEditorContentBrowserWidget::DrawDirNode(const FDirNode& InNode)
{
	ImGuiTreeNodeFlags Flag =
		InNode.Children.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_OpenOnArrow;

	if (InNode.Self.Path == BrowserContext.CurrentPath)
	{
		Flag |= ImGuiTreeNodeFlags_Selected;
	}
	if (!BrowserContext.PendingRevealPath.empty() && IsSubPath(InNode.Self.Path, BrowserContext.PendingRevealPath))
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}

	bool bIsOpen = ImGui::TreeNodeEx(FPaths::ToUtf8(InNode.Self.Name).c_str(), Flag);
	if (ImGui::IsItemClicked())
	{
		if (BrowserContext.CurrentPath != InNode.Self.Path)
		{
			BrowserContext.CurrentPath = InNode.Self.Path;
			RefreshContent();
		}
	}

	if (!bIsOpen)
	{
		return;
	}

	int32 ChildrenCount = static_cast<int32>(InNode.Children.size());
	for (int i = 0; i < ChildrenCount; i++)
	{
		DrawDirNode(InNode.Children[i]);
	}

	ImGui::TreePop();
}

void FEditorContentBrowserWidget::DrawContents()
{
	int ElementCount = static_cast<int>(CachedBrowserElements.size());

	const float ContentWidth = ImGui::GetContentRegionAvail().x;
	const float ItemWidth = BrowserContext.ContentSize.x;
	const float ItemHeight = BrowserContext.ContentSize.y;

	int ColumnCount = static_cast<int>(ContentWidth / ItemWidth);
	if (ColumnCount < 1)
	{
		ColumnCount = 1;
	}

	float GapSize = 0.0f;
	if (ColumnCount > 1)
	{
		GapSize = (ContentWidth - ItemWidth * ColumnCount) / (ColumnCount);
	}

	ImVec2 StartPos = ImGui::GetCursorPos();

	for (int i = 0; i < ElementCount; ++i)
	{
		int Column = i % ColumnCount;
		int Row = i / ColumnCount;

		float X = StartPos.x + Column * (ItemWidth + GapSize);
		float Y = StartPos.y + Row * (ItemHeight + GapSize * 2.f);

		ImGui::SetCursorPos(ImVec2(X, Y));
		CachedBrowserElements[i]->Render(BrowserContext);
	}

	int RowCount = (ElementCount + ColumnCount - 1) / ColumnCount;
	ImGui::SetCursorPos(ImVec2(StartPos.x, StartPos.y + RowCount * ItemHeight));

	if (ImGui::BeginPopupContextWindow("##ContentBrowserBackgroundContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::BeginMenu("Create"))
		{
			if (ImGui::MenuItem("Float Curve"))
			{
				FString CreatedPath;
				if (FAssetFactory::CreateFloatCurve(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewFloatCurve", CreatedPath))
				{
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (UFloatCurveAsset* CurveAsset = FFloatCurveManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(CurveAsset);
						}
					}
				}
			}
			if (ImGui::MenuItem("Camera Shake"))
			{
				FString CreatedPath;
				if (FAssetFactory::CreateCameraShake(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewCameraShake", CreatedPath))
				{
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (UCameraShakeAsset* ShakeAsset = FCameraShakeManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(ShakeAsset);
						}
					}
				}
			}
			if (ImGui::MenuItem("Anim Instance"))
			{
				FString CreatedPath;
				if (FAssetFactory::CreateAnimInstanceAsset(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewAnimInstance", CreatedPath))
				{
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (UAnimInstanceAsset* AnimInstanceAsset = FAnimInstanceAssetManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(AnimInstanceAsset);
						}
					}
				}
			}
			if (ImGui::MenuItem("Particle System"))
			{
				FString CreatedPath;
				if (FAssetFactory::CreateParticleSystem(FPaths::ToUtf8(BrowserContext.CurrentPath), "NewParticleSystem", CreatedPath))
				{
					Refresh();
					if (BrowserContext.EditorEngine)
					{
						if (UParticleSystem* ParticleSystem = FParticleSystemManager::Get().Load(CreatedPath))
						{
							BrowserContext.EditorEngine->OpenAssetEditorForObject(ParticleSystem);
						}
					}
				}
			}
			ImGui::EndMenu();
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Refresh"))
		{
			Refresh();
		}

		ImGui::EndPopup();
	}
}

void FEditorContentBrowserWidget::BeginImportSourceFile()
{
	const FString SelectedPath = OpenImportSourceFileDialog(BrowserContext.CurrentPath);
	if (SelectedPath.empty())
	{
		return;
	}

	const std::filesystem::path FullPath = ResolveProjectPath(SelectedPath);
	const std::filesystem::path AssetRoot(FPaths::AssetDir());
	if (!IsSubPath(AssetRoot, FullPath))
	{
		UE_LOG("Content Browser import rejected: source must be inside Asset/. Path=%s", SelectedPath.c_str());
		return;
	}

	const FString SourcePath = ToProjectRelativePath(FullPath);
	const FString Extension = GetLowerExtension(FullPath);
	if (Extension == ".obj")
	{
		if (ExecuteObjImport(SourcePath))
		{
			Refresh();
		}
		return;
	}

	if (Extension == ".fbx")
	{
		BeginFbxImport(SourcePath);
		return;
	}

	UE_LOG("Content Browser import rejected: unsupported source extension. Path=%s", SelectedPath.c_str());
}

void FEditorContentBrowserWidget::BeginFbxImport(const FString& SourcePath)
{
	FbxImportDialog.Open(SourcePath);
}

void FEditorContentBrowserWidget::RenderFbxImportPopup()
{
	if (FbxImportDialog.Render(BrowserContext.EditorEngine))
	{
		Refresh();
	}
}

bool FEditorContentBrowserWidget::ExecuteObjImport(const FString& SourcePath)
{
	if (!BrowserContext.EditorEngine)
	{
		return false;
	}

	UStaticMesh* ImportedMesh = nullptr;
	ID3D11Device* Device = BrowserContext.EditorEngine->GetRenderer().GetFD3DDevice().GetDevice();
	const bool bImported = FEditorObjImportService::ImportStaticMeshFromObj(SourcePath, Device, ImportedMesh, false);
	if (bImported)
	{
		RefreshImportedAssetLists();
	}
	return bImported;
}

void FEditorContentBrowserWidget::RefreshImportedAssetLists()
{
	FMeshManager::ScanMeshAssets();
	FMaterialManager::Get().ScanMaterialAssets();
	FEditorObjImportService::ScanObjSourceFiles();
	FEditorFbxImportService::ScanFbxSourceFiles();
}

TArray<FContentItem> FEditorContentBrowserWidget::ReadDirectory(std::wstring Path)
{
	TArray<FContentItem> Items;

	if (!std::filesystem::exists(Path) || !std::filesystem::is_directory(Path))
		return Items;

	for (const auto& Entry : std::filesystem::directory_iterator(Path))
	{
		std::wstring Name = Entry.path().filename().wstring();
		if (Entry.is_directory())
		{
			if (Name == L"Bin" || Name == L"Build" || Name == L".git" || Name == L".vs")
				continue;
		}
		else if (!BrowserContext.bShowSourceFiles && IsEditorSourceFile(Entry.path()))
		{
			continue;
		}

		FContentItem Item;
		Item.Path = Entry.path();
		Item.Name = Name;
		Item.bIsDirectory = Entry.is_directory();

		Items.push_back(Item);
	}

	std::sort(Items.begin(), Items.end(),
		[](const FContentItem& A, const FContentItem& B)
		{
			if (A.bIsDirectory != B.bIsDirectory)
				return A.bIsDirectory > B.bIsDirectory;

			return A.Name < B.Name;
		});

	return Items;
}

FEditorContentBrowserWidget::FDirNode FEditorContentBrowserWidget::BuildDirectoryTree(const std::filesystem::path& DirPath)
{
	FDirNode Node;
	Node.Self.Path = DirPath;
	Node.Self.Name = DirPath.filename().wstring();
	Node.Self.bIsDirectory = true;

	for (const auto& Entry : std::filesystem::directory_iterator(DirPath))
	{
		if (!Entry.is_directory())
			continue;

		std::wstring DirName = Entry.path().filename().wstring();
		if (DirName == L"Bin" || DirName == L"Build" || DirName == L".git" || DirName == L".vs")
			continue;

		Node.Children.push_back(BuildDirectoryTree(Entry.path()));
	}

	if (Node.Self.Name.empty())
		Node.Self.Name = FPaths::ToWide("Project");

	return Node;
}

TArray<FMeshAssetListItem> FEditorContentBrowserWidget::ScanSkeletonAssets() const
{
	TArray<FMeshAssetListItem> Items;

	const std::filesystem::path AssetRoot(FPaths::AssetDir());
	if (!std::filesystem::exists(AssetRoot))
	{
		return Items;
	}

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(AssetRoot))
	{
		if (!Entry.is_regular_file() || GetLowerExtension(Entry.path()) != ".uasset")
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
