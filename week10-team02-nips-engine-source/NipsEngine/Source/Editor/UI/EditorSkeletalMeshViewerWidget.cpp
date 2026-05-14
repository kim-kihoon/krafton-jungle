#include "Editor/UI/EditorSkeletalMeshViewerWidget.h"

#include "Core/ResourceManager.h"
#include "Editor/Settings/EditorSettings.h"
#include "ImGui/imgui.h"

#include <d3d11.h>
#include <filesystem>

#include "Component/SkeletalMeshComponent.h"

namespace
{
constexpr float BoneTreeIndentSpacing = 12.0f;

const char* GetPreviewViewportTypeName(EEditorViewportType Type)
{
	switch (Type)
	{
	case EVT_Perspective: return "Perspective";
	case EVT_OrthoTop: return "Top";
	case EVT_OrthoBottom: return "Bottom";
	case EVT_OrthoFront: return "Front";
	case EVT_OrthoBack: return "Back";
	case EVT_OrthoLeft: return "Left";
	case EVT_OrthoRight: return "Right";
	default: return "Viewport";
	}
}

const char* GetPreviewViewModeName(EViewMode Mode)
{
	switch (Mode)
	{
	case EViewMode::Lit: return "Lit";
	case EViewMode::Unlit: return "Unlit";
	case EViewMode::Wireframe: return "Wireframe";
	case EViewMode::SceneDepth: return "Scene Depth";
	case EViewMode::WorldNormal: return "World Normal";
	case EViewMode::CascadeShadow: return "Cascade Shadow";
	case EViewMode::DebugCollision: return "Collision";
	default: return "Lit";
	}
}
}

void FEditorSkeletalMeshViewerWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	bIsOpen = true;
	PreviewScene.Initialize(InEditorEngine);
	RefreshSkeletalMeshPathCache();
	SyncCurrentMeshFromPreview();
}

void FEditorSkeletalMeshViewerWidget::Render(float DeltaTime)
{
	bWindowFocused = false;

	if (!bIsOpen)
	{
		PreviewScene.SetVisible(false);
		return;
	}

	PreviewScene.SetVisible(true);

	if (WindowName.empty())
	{
		WindowName = "SkeletalMesh Viewer##Viewer_" + std::to_string(InstanceId);
	}

	if (bFocusNextFrame)
	{
		ImGui::SetNextWindowFocus();
		bFocusNextFrame = false;
	}

	if (ImGui::Begin(WindowName.c_str(), &bIsOpen, ImGuiWindowFlags_MenuBar))
	{
		bWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		SyncCurrentMeshFromPreview();
		RenderToolbar();

		const ImVec2 LayoutSize = ImGui::GetContentRegionAvail();
		if (ImGui::BeginTable("ViewerLayout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, LayoutSize))
		{
			ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, 250.0f);
			ImGui::TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 300.0f);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::BeginChild("BoneHierarchyPanel", ImVec2(0, LayoutSize.y), true);
			RenderBoneHierarchyPanel();
			ImGui::EndChild();

			ImGui::TableSetColumnIndex(1);

			ImVec2 ViewportAvail = ImGui::GetContentRegionAvail();

			ImGui::BeginChild("ViewportPanel", ViewportAvail, false);

			FSkeletalMeshPreviewViewportClient& Client = PreviewScene.GetViewportClient();
			const FEditorSettings& Settings = FEditorSettings::Get();
			Client.SetMoveSpeed(Settings.CameraSpeed);
			Client.SetMoveSensitivity(Settings.CameraMoveSensitivity);
			Client.SetRotateSensitivity(Settings.CameraRotateSensitivity);
			Client.SetZoomSpeed(Settings.CameraZoomSpeed);

			const ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
			if (ViewportSize.x > 0.0f && ViewportSize.y > 0.0f)
			{
				const uint32 ViewportWidth = static_cast<uint32>(ViewportSize.x);
				const uint32 ViewportHeight = static_cast<uint32>(ViewportSize.y);
				if (ViewportWidth != LastViewportWidth || ViewportHeight != LastViewportHeight)
				{
					PreviewScene.SetViewportSize(ViewportWidth, ViewportHeight);
					LastViewportWidth = ViewportWidth;
					LastViewportHeight = ViewportHeight;
				}

				if (ID3D11ShaderResourceView* SRV = PreviewScene.GetSceneViewport().GetOutSRV())
				{
					ImGui::Image(reinterpret_cast<ImTextureID>(SRV), ViewportSize);
				}
				else
				{
					ImGui::Dummy(ViewportSize);
				}

				const ImVec2 Min = ImGui::GetItemRectMin();
				const ImVec2 Max = ImGui::GetItemRectMax();
				PreviewScene.SetInputRectFromScreenRect(Min.x, Min.y, Max.x, Max.y);
				PreviewScene.SetViewportHovered(ImGui::IsItemHovered());
			}
			ImGui::EndChild();

			ImGui::TableSetColumnIndex(2);

			ImGui::BeginChild("DetailsPanel", ImVec2(0, LayoutSize.y), true);
			RenderBoneDetailsPanel();
			ImGui::EndChild();

			ImGui::EndTable();
		}

		PreviewScene.Tick(DeltaTime);
	}
	ImGui::End();
}

void FEditorSkeletalMeshViewerWidget::SetInstanceId(int32 InInstanceId)
{
	InstanceId = InInstanceId;
	WindowName = "SkeletalMesh Viewer##Viewer_" + std::to_string(InstanceId);
}

void FEditorSkeletalMeshViewerWidget::OpenMesh(const FString& MeshPath)
{
	USkeletalMesh* LoadedMesh = FResourceManager::Get().LoadSkeletalMesh(MeshPath);
	if (LoadedMesh)
	{
		PreviewScene.SetSkeletalMesh(LoadedMesh);
		RebuildBoneCache(LoadedMesh);
	}

	std::wstring WidePath = FPaths::ToWide(MeshPath.c_str());
	std::filesystem::path PathObj(WidePath);
	std::string FileName = FPaths::ToUtf8(PathObj.filename().generic_wstring());

	WindowName = "SkeletalMesh Viewer - " + FileName + "##Viewer_" + std::to_string(InstanceId);
}

void FEditorSkeletalMeshViewerWidget::RenderToolbar()
{
	FSkeletalMeshPreviewViewportClient& Client = PreviewScene.GetViewportClient();
	FEditorViewportState& State = PreviewScene.GetSceneViewport().GetState();

	if (!ImGui::BeginMenuBar())
	{
		return;
	}

	ImGui::TextDisabled(
		"SkeletalMesh Viewer | %s | %s",
		GetPreviewViewportTypeName(Client.GetViewportType()),
		GetPreviewViewModeName(State.ViewMode));
	ImGui::SameLine();

	if (ImGui::BeginMenu("Type"))
	{
		static constexpr EEditorViewportType ViewTypes[] = {
			EVT_Perspective,
			EVT_OrthoTop,
			EVT_OrthoBottom,
			EVT_OrthoFront,
			EVT_OrthoBack,
			EVT_OrthoLeft,
			EVT_OrthoRight,
		};

		for (EEditorViewportType Type : ViewTypes)
		{
			const bool bSelected = Client.GetViewportType() == Type;
			if (ImGui::MenuItem(GetPreviewViewportTypeName(Type), nullptr, bSelected))
			{
				Client.SetViewportType(Type);
				Client.ApplyCameraMode();
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("View"))
	{
		static constexpr EViewMode MainModes[] = {
			EViewMode::Lit,
			EViewMode::Unlit,
			EViewMode::Wireframe,
			EViewMode::SceneDepth,
			EViewMode::WorldNormal,
		};

		for (EViewMode Mode : MainModes)
		{
			const bool bSelected = State.ViewMode == Mode;
			if (ImGui::MenuItem(GetPreviewViewModeName(Mode), nullptr, bSelected))
			{
				State.ViewMode = Mode;
			}
		}

		ImGui::Separator();

		static constexpr EViewMode DebugModes[] = {
			EViewMode::CascadeShadow,
			EViewMode::DebugCollision,
		};

		for (EViewMode Mode : DebugModes)
		{
			const bool bSelected = State.ViewMode == Mode;
			if (ImGui::MenuItem(GetPreviewViewModeName(Mode), nullptr, bSelected))
			{
				State.ViewMode = Mode;
			}
		}
		ImGui::EndMenu();
	}

	if (SelectedMeshPathIndex >= static_cast<int32>(CachedSkeletalMeshPaths.size()))
	{
		SelectedMeshPathIndex = CachedSkeletalMeshPaths.empty() ? -1 : 0;
	}

	if (ImGui::BeginMenu("Mesh"))
	{
		RefreshSkeletalMeshPathCache();

		for (int32 i = 0; i < static_cast<int32>(CachedSkeletalMeshPaths.size()); ++i)
		{
			const bool bSelected = SelectedMeshPathIndex == i;
			if (ImGui::MenuItem(CachedSkeletalMeshPaths[i].c_str(), nullptr, bSelected))
			{
				SelectedMeshPathIndex = i;
				USkeletalMesh* LoadedMesh = FResourceManager::Get().LoadSkeletalMesh(CachedSkeletalMeshPaths[i]);
				PreviewScene.SetSkeletalMesh(LoadedMesh);

				RebuildBoneCache(LoadedMesh);
			}
		}
		if (CachedSkeletalMeshPaths.empty())
		{
			ImGui::MenuItem("No SkeletalMesh assets", nullptr, false, false);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Skeleton"))
	{
		bool bShow = PreviewScene.IsSkeletonVisible();
		if (ImGui::MenuItem("Show Skeleton", nullptr, &bShow))
		{
			PreviewScene.SetSkeletonVisible(bShow);
		}

		bool bShowFull = PreviewScene.IsFullSkeletonVisible();
		if (ImGui::MenuItem("Show Full Skeleton", nullptr, &bShowFull))
		{
			PreviewScene.SetFullSkeletonVisible(bShowFull);
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Reset Camera"))
	{
		Client.ResetCamera();
		Client.ApplyCameraMode();
	}

	ImGui::EndMenuBar();
}

void FEditorSkeletalMeshViewerWidget::RenderBoneHierarchyPanel()
{
	ImGui::Text("Bone Hierarchy");
	ImGui::Separator();

	if (!CurrentSkeletalMesh || RootBones.empty())
	{
		ImGui::TextDisabled("No skeleton data.");
		return;
	}

	const TArray<FSkeletalBone>& Bones = CurrentSkeletalMesh->GetBones();
	ImGui::TextDisabled("%d bones", static_cast<int32>(Bones.size()));
	ImGui::Spacing();

	ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, BoneTreeIndentSpacing);
	for (int32 RootIndex : RootBones)
	{
		RenderBoneTree(RootIndex, Bones);
	}
	ImGui::PopStyleVar();
}

void FEditorSkeletalMeshViewerWidget::RenderBoneTree(int32 BoneIndex, const TArray<FSkeletalBone>& Bones)
{
	if (BoneIndex < 0 ||
		BoneIndex >= static_cast<int32>(Bones.size()) ||
		BoneIndex >= static_cast<int32>(CachedBoneChildren.size()))
	{
		return;
	}

	const FSkeletalBone& Bone = Bones[BoneIndex];

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

	if (SelectedBoneIndex == BoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (CachedBoneChildren[BoneIndex].empty())
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	else
	{
		Flags |= ImGuiTreeNodeFlags_DefaultOpen; // 모든 하위 트리를 기본적으로 열어둠
	}

	const bool bIsOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(BoneIndex)), Flags, "%s", Bone.Name.c_str());

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		SelectedBoneIndex = BoneIndex;
		PreviewScene.SelectBone(BoneIndex);
	}

	if (bIsOpen && !CachedBoneChildren[BoneIndex].empty())
	{
		for (int32 ChildIndex : CachedBoneChildren[BoneIndex])
		{
			RenderBoneTree(ChildIndex, Bones);
		}
		ImGui::TreePop();
	}
}

void FEditorSkeletalMeshViewerWidget::RenderBoneDetailsPanel()
{
	ImGui::Text("Bone Transform Details");
	ImGui::Separator();

	if (SelectedBoneIndex == -1 || !CurrentSkeletalMesh)
	{
		ImGui::TextDisabled("Select a bone to edit its local transform.");
		return;
	}

	USkeletalMeshComponent* MeshComp = PreviewScene.GetPreviewMeshComponent();
	if (!MeshComp) return;

	const TArray<FSkeletalBone>& Bones = CurrentSkeletalMesh->GetBones();
	if (SelectedBoneIndex >= static_cast<int32>(Bones.size())) return;

	ImGui::Text("Name: %s (Index: %d)", Bones[SelectedBoneIndex].Name.c_str(), SelectedBoneIndex);
	ImGui::Separator();

	// --- 1. Reset Buttons ---
	if (ImGui::Button("Reset Selected Bone"))
	{
		MeshComp->SetBoneLocalTransform(SelectedBoneIndex, Bones[SelectedBoneIndex].ReferenceLocalTransform);
		LastEditedBoneIndex = -1; // 캐시 무효화
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset All Pose"))
	{
		MeshComp->ResetPose();
		LastEditedBoneIndex = -1; // 캐시 무효화
	}
	ImGui::Separator();

	// --- 2. Local Transform Edit ---
	ImGui::Text("Local Transform (Editable)");
	FTransform LocalTransform = MeshComp->GetBoneLocalTransform(SelectedBoneIndex);
	FVector Position = LocalTransform.GetLocation();
	FVector Scale = LocalTransform.GetScale3D();

	if (LastEditedBoneIndex != SelectedBoneIndex)
	{
		CachedEulerRotation = LocalTransform.GetRotation().Euler();
		LastEditedBoneIndex = SelectedBoneIndex;
	}

	bool bChanged = false;
	ImGui::PushItemWidth(200.0f);
	if (ImGui::DragFloat3("Location", &Position.X, 0.1f)) bChanged = true;
	if (ImGui::DragFloat3("Rotation", &CachedEulerRotation.X, 0.1f)) bChanged = true;
	if (ImGui::DragFloat3("Scale", &Scale.X, 0.01f)) bChanged = true;
	ImGui::PopItemWidth();

	if (bChanged)
	{
		FTransform NewTransform;
		NewTransform.SetLocation(Position);
		NewTransform.SetRotation(FQuat::MakeFromEuler(CachedEulerRotation));
		NewTransform.SetScale3D(Scale);

		MeshComp->SetBoneLocalTransform(SelectedBoneIndex, NewTransform);
	}
	ImGui::Separator();

	ImGui::TextDisabled("Component Space Transform");
	FTransform CompTransform = MeshComp->GetBoneComponentTransform(SelectedBoneIndex);
	FVector CompPos = CompTransform.GetLocation();
	ImGui::Text("Loc: %.2f, %.2f, %.2f", CompPos.X, CompPos.Y, CompPos.Z);

	ImGui::Spacing();

	ImGui::TextDisabled("World Space Transform");
	FTransform WorldTransform = MeshComp->GetBoneWorldTransform(SelectedBoneIndex);
	FVector WorldPos = WorldTransform.GetLocation();
	ImGui::Text("Loc: %.2f, %.2f, %.2f", WorldPos.X, WorldPos.Y, WorldPos.Z);
}

void FEditorSkeletalMeshViewerWidget::RebuildBoneCache(USkeletalMesh* Mesh)
{
	CurrentSkeletalMesh = Mesh;
	SelectedBoneIndex = -1;
	CachedBoneChildren.clear();
	RootBones.clear();

	PreviewScene.SelectBone(-1);

	if (!CurrentSkeletalMesh || !CurrentSkeletalMesh->HasValidSkeleton())
	{
		return;
	}

	const TArray<FSkeletalBone>& Bones = CurrentSkeletalMesh->GetBones();
	CachedBoneChildren.resize(Bones.size());

	for (int32 i = 0; i < static_cast<int32>(Bones.size()); i++)
	{
		const int32 ParentIndex = Bones[i].ParentIndex;
		if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(Bones.size()))
		{
			CachedBoneChildren[ParentIndex].push_back(i);
		}
		else
		{
			RootBones.push_back(i);
		}
	}
}

void FEditorSkeletalMeshViewerWidget::SyncCurrentMeshFromPreview()
{
	USkeletalMesh* PreviewMesh = PreviewScene.GetCurrentSkeletalMesh();
	if (PreviewMesh != CurrentSkeletalMesh)
	{
		RebuildBoneCache(PreviewMesh);
	}

	const int32 PreviewSelectedBoneIndex = PreviewScene.GetSelectedBoneIndex();
	if (PreviewSelectedBoneIndex != SelectedBoneIndex)
	{
		SelectedBoneIndex = PreviewSelectedBoneIndex;
	}
}

void FEditorSkeletalMeshViewerWidget::RefreshSkeletalMeshPathCache()
{
	CachedSkeletalMeshPaths = FResourceManager::Get().GetSkeletalMeshPaths();
}
