#include "AnimSequenceEditorWidget.h"

#include "Animation/AnimDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceManager.h"
#include "Animation/Notify.h"
#include "Animation/NotifyRegistry.h"
#include "Asset/AssetPackage.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/Property/FEnumProperty.h"
#include "Core/Property/PropertyTypes.h"
#include "Editor/Settings/EditorSettings.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/StaticMeshActor.h"
#include "GameFramework/WorldContext.h"
#include "Mesh/MeshManager.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletalMeshAsset.h"
#include "Mesh/SkeletonAsset.h"
#include "Object/FUObjectArray.h"
#include "Platform/Paths.h"
#include "Runtime/Engine.h"
#include "Slate/SlateApplication.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Viewport/Viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <imgui.h>

namespace
{
	// Value값을 String으로 포매팅 + 3자리 마다 ,로 구분시키는 hepler
	FString FormatAnimSequenceStatCount(size_t Value)
	{
		FString Result = std::to_string(Value);
		for (int32 InsertPos = static_cast<int32>(Result.length()) - 3; InsertPos > 0; InsertPos -= 3)
		{
			Result.insert(static_cast<size_t>(InsertPos), ",");
		}
		return Result;
	}

	FString GetAnimSequenceDisplayName(const FString& AnimSequencePath)
	{
		std::filesystem::path Path(FPaths::ToWide(AnimSequencePath));
		FString Name = FPaths::ToUtf8(Path.stem().generic_wstring());
		const FString Suffix = "_AnimSequence";
		if (Name.length() > Suffix.length() &&
			Name.compare(Name.length() - Suffix.length(), Suffix.length(), Suffix) == 0)
		{
			Name.erase(Name.length() - Suffix.length());
		}
		return Name;
	}

	// Notify 프로퍼티 1개를 타입별로 렌더링. 변경 여부 반환.
	bool DrawNotifyPropertyRow(const FProperty& Prop, void* Container)
	{
		bool bChanged = false;
		void* ValuePtr = Prop.ContainerPtrToValuePtr(Container);
		switch (Prop.GetType())
		{
		case EPropertyType::Float:
		{
			const FNumericProperty& NumericProp = static_cast<const FNumericProperty&>(Prop);
			float* Val = static_cast<float*>(ValuePtr);
			if (NumericProp.Min != 0.0f || NumericProp.Max != 0.0f)
				bChanged = ImGui::DragFloat("##v", Val, NumericProp.Speed, NumericProp.Min, NumericProp.Max, "%.4f");
			else
				bChanged = ImGui::DragFloat("##v", Val, NumericProp.Speed);
			break;
		}
		case EPropertyType::Bool:
		{
			bool* Val = static_cast<bool*>(ValuePtr);
			bChanged = ImGui::Checkbox("##v", Val);
			break;
		}
		case EPropertyType::Int:
		{
			const FNumericProperty& NumericProp = static_cast<const FNumericProperty&>(Prop);
			int32* Val = static_cast<int32*>(ValuePtr);
			if (NumericProp.Min != 0.0f || NumericProp.Max != 0.0f)
				bChanged = ImGui::DragInt("##v", Val, static_cast<int32>(NumericProp.Speed), static_cast<int32>(NumericProp.Min), static_cast<int32>(NumericProp.Max));
			else
				bChanged = ImGui::DragInt("##v", Val, static_cast<int32>(NumericProp.Speed));
			break;
		}
		case EPropertyType::Vec3:
		{
			float* Val = static_cast<float*>(ValuePtr);
			bChanged = ImGui::DragFloat3("##v", Val, 0.1f);
			break;
		}
		case EPropertyType::String:
		{
			FString* Val = static_cast<FString*>(ValuePtr);
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), Val->c_str(), _TRUNCATE);
			if (ImGui::InputText("##v", Buf, sizeof(Buf)))
			{
				*Val = Buf;
				bChanged = true;
			}
			break;
		}
		case EPropertyType::Name:
		{
			FName* Val = static_cast<FName*>(ValuePtr);
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), Val->ToString().c_str(), _TRUNCATE);
			if (ImGui::InputText("##v", Buf, sizeof(Buf)))
			{
				*Val = FName(Buf);
				bChanged = true;
			}
			break;
		}
		case EPropertyType::Enum:
		{
			const FEnumProperty& EnumProp = static_cast<const FEnumProperty&>(Prop);
			const UEnum* Enum = EnumProp.GetEnum();
			if (Enum && Enum->NumEnums() > 0)
			{
				int64 Value = 0;
				const uint32 UnderlyingSize = Enum->GetUnderlyingSize();
				memcpy(&Value, ValuePtr, std::min<uint32>(UnderlyingSize, sizeof(Value)));
				int32 Idx = Enum->GetIndexByValue(Value);
				if (Idx < 0) Idx = 0;
				const bool bChangedSelection = ImGui::BeginCombo("##v", Enum->GetNameByIndex(static_cast<uint32>(Idx)));
				if (bChangedSelection)
				{
					for (uint32 i = 0; i < Enum->NumEnums(); ++i)
					{
						const bool bSelected = Idx == static_cast<int32>(i);
						if (ImGui::Selectable(Enum->GetNameByIndex(i), bSelected))
						{
							int64 NewValue = Enum->GetValueByIndex(i);
							memcpy(ValuePtr, &NewValue, std::min<uint32>(UnderlyingSize, sizeof(NewValue)));
							bChanged = true;
						}
						if (bSelected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
			break;
		}
		default:
			ImGui::TextDisabled("(unsupported type)");
			break;
		}
		return bChanged;
	}
}

// 다중 Widget Instance 처리
static uint32 GNextAnimSequenceEditorInstanceId = 0;

FAnimSequenceEditorWidget::FAnimSequenceEditorWidget()
{
	const FString Id = std::to_string(GNextAnimSequenceEditorInstanceId++);
	PreviewWorldHandle = FName("AnimSequenceEditorPreview_" + Id);
	WindowIdSuffix = "###AnimSequenceEditor_" + Id;
}

FAnimSequenceEditorWidget::~FAnimSequenceEditorWidget()
{
	for (FAnimNotifyEvent& Notify : PreviewNotifyMarkers)
	{
		delete Notify.NotifyTrigger;
		Notify.NotifyTrigger = nullptr;
	}
}

bool FAnimSequenceEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UAnimSequence>();
}

bool FAnimSequenceEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const UAnimSequence* CurrentSequence = Cast<UAnimSequence>(EditedObject);
	const UAnimSequence* RequestedSequence = Cast<UAnimSequence>(Object);

	if (!IsOpen() || !CurrentSequence || !RequestedSequence)
	{
		return false;
	}

	const FString& CurrentPath = CurrentSequence->GetAssetPathFileName();

	return !CurrentPath.empty()
		&& CurrentPath != "None"
		&& CurrentPath == RequestedSequence->GetAssetPathFileName();
}

void FAnimSequenceEditorWidget::Open(UObject* Object)
{
	if (!CanEdit(Object))
	{
		return;
	}

	if (IsOpen())
	{
		ReleasePreviewWorld();
	}

	FAssetEditorWidget::Open(Object);

	AnimSequence = Cast<UAnimSequence>(EditedObject);
	if (!AnimSequence)
	{
		FAssetEditorWidget::Close();
		return;
	}

	InitializeFromAnimSequence();

	// AnimSequence는 Skeleton과 keyframe만 보유하고, 어떤 SkeletalMesh로 미리볼지는 저장X
	// 그래서 에디터 내부에서 같은 Skeleton을 참조하는 SkeletalMesh를 찾아 PreviewComponent 생성
	// 찾지 못한 경우에도 Timeline/Notify/Skeleton 정보를 볼 수 있도록 에디터 창은 그대로 유지

	// TODO: 나중에 같은 Skeleton을 사용하는 SkeletalMesh를 출력하는 토글 창 만들어보기.
	PreviewSkeletalMesh = FindPreviewSkeletalMesh();
	if (PreviewSkeletalMesh)
	{
		InitializePreviewWorld();
	}
}

void FAnimSequenceEditorWidget::InitializeFromAnimSequence()
{
	PreviewSkeletalMesh = nullptr;
	PreviewMeshComponent = nullptr;
	SingleNodeInstance = nullptr;
	PreviewStatusMessage.clear();
	EvaluatedLocalPose.Reset();
	bLastPoseEvaluationSucceeded = false;
	SelectedBoneIndex = -1;

	// AnimSequence는 SkeletonPath만 저장한 채 로드될 수 있으므로, 에디터 진입 시 Skeleton pointer를 한 번 복원합니다.
	// 이 작업은 참조 asset을 찾는 과정일 뿐이며 원본 keyframe이나 Skeleton pose 데이터를 변경하지 않습니다.
	if (AnimSequence && !AnimSequence->GetSkeletonAsset())
	{
		AnimSequence->ResolveSkeleton();
	}

	const UAnimDataModel* DataModel = AnimSequence ? AnimSequence->GetDataModel() : nullptr;
	PlayLength = AnimSequence ? std::max(0.0f, AnimSequence->GetPlayLength()) : 0.0f;
	if (PlayLength <= 0.0f && DataModel)
	{
		PlayLength = std::max(0.0f, DataModel->GetPlayLength());
	}

	if (PlayLength <= 0.0f && DataModel && DataModel->GetFrameRate() > 0.0f)
	{
		// 일부 오래된 AnimSequence package는 key/track 정보는 있지만 SequenceLength/DataModel.PlayLength가 0으로 저장되어 있습니다.
		// 이 경우 Timeline을 숨겨버리면 asset이 비어 있는지, 길이 metadata만 누락됐는지 구분할 수 없습니다.
		// 원본 asset을 수정하지 않고 에디터 표시용 길이만 프레임 정보에서 복원해 Timeline과 pose scrub이 가능하게 합니다.
		const int32 FrameCount = std::max(DataModel->GetNumberOfFrames(), DataModel->GetNumberOfKeys());
		if (FrameCount > 1)
		{
			PlayLength = static_cast<float>(FrameCount - 1) / DataModel->GetFrameRate();
		}
	}

	const float Padding = PlayLength > 0.0f ? std::max(0.1f, PlayLength * 0.08f) : 0.25f;
	ViewStartTime = -Padding;
	ViewEndTime = std::max(PlayLength + Padding, 1.0f);


	// Timeline 상태는 항상 AnimSequence의 길이를 기준으로 다시 시작합니다.
	// 이전 asset에서 쓰던 재생 시간/드래그 상태가 남으면 새 sequence가 열릴 때 out-of-range pose 평가가 발생할 수 있습니다.
	CurrentTime = 0.0f;
	PreviousTime = 0.0f;
	bPlaying = false;
	bLooping = AnimSequence ? AnimSequence->IsLooping() : true;
	TimelinePlayRate = 1.0f;

	SelectedNotifyIndex = -1;
	DraggingNotifyIndex = -1;
	bDraggingNotify = false;
	ContextTimelineTime = 0.0f;

	// DataModel이 없는 예외 상황에서만 사용하는 fallback preview notify 배열입니다.
	PreviewNotifyMarkers.clear();
	RelatedAnimSequences.clear();

	// Bone override도 asset 원본에 저장하지 않는 preview-local 상태입니다.
	// 새 sequence를 열 때 이전 override가 남으면 다른 Skeleton index에 잘못 적용될 수 있어 반드시 비웁니다.
	EditorBoneOverrides.clear();

	RefreshRelatedAnimSequences();
}

void FAnimSequenceEditorWidget::RefreshRelatedAnimSequences()
{
	RelatedAnimSequences.clear();

	if (!AnimSequence)
	{
		return;
	}

	const FString& SkeletonPath = AnimSequence->GetSkeletonPath();
	if (SkeletonPath.empty())
	{
		return;
	}
	// Asset/ 부터 순회 시작
	const std::filesystem::path AssetRoot(FPaths::AssetDir());
	if (!std::filesystem::exists(AssetRoot))
	{
		return;
	}

	// .uasset 확장자 + 헤더 정보 읽어보고 AnimSequence 타입이 아닌경우 패스
	for (const auto& Entry : std::filesystem::recursive_directory_iterator(AssetRoot))
	{
		if (!Entry.is_regular_file() || Entry.path().extension() != L".uasset")
		{
			continue;
		}

		const FString PackagePath = FPaths::ToUtf8(
			Entry.path().lexically_relative(FPaths::RootDir()).generic_wstring());

		EAssetPackageType PackageType = EAssetPackageType::Unknown;
		if (!FAssetPackage::GetPackageType(PackagePath, PackageType) ||
			PackageType != EAssetPackageType::AnimSequence)
		{
			continue;
		}

		UAnimSequence* CandidateSequence = FAnimSequenceManager::Get().Load(PackagePath);
		if (!CandidateSequence)
		{
			continue;
		}

		if (CandidateSequence->GetSkeletonPath() != SkeletonPath)
		{
			continue;
		}

		FRelatedAnimSequenceItem Item;
		Item.Path = CandidateSequence->GetAssetPathFileName();
		Item.Name = GetAnimSequenceDisplayName(Item.Path);
		Item.Sequence = CandidateSequence;
		RelatedAnimSequences.push_back(Item);
	}

	std::sort(
		RelatedAnimSequences.begin(),
		RelatedAnimSequences.end(),
		[](const FRelatedAnimSequenceItem& A, const FRelatedAnimSequenceItem& B)
		{
			return A.Name < B.Name;
		});
}

void FAnimSequenceEditorWidget::OpenRelatedAnimSequence(const FString& AnimSequencePath)
{
	if (!AnimSequence || AnimSequencePath.empty())
	{
		return;
	}

	if (AnimSequence->GetAssetPathFileName() == AnimSequencePath)
	{
		return;
	}

	UAnimSequence* SequenceToOpen = FAnimSequenceManager::Get().Load(AnimSequencePath);
	if (!SequenceToOpen)
	{
		return;
	}

	Open(SequenceToOpen);
}

USkeletalMesh* FAnimSequenceEditorWidget::FindPreviewSkeletalMesh()
{
	if (!AnimSequence)
	{
		PreviewStatusMessage = "AnimSequence가 유효하지 않습니다.";
		return nullptr;
	}

	const FString& SkeletonPath = AnimSequence->GetSkeletonPath();
	if (SkeletonPath.empty())
	{
		PreviewStatusMessage = "Preview Mesh를 찾을 수 없음: AnimSequence에 Skeleton 경로가 없습니다.";
		return nullptr;
	}

	// 1차 탐색: 캐시된 Mesh 중에 같은 SkeletonPath를 사용하는 Mesh가 있는지 탐색
	// UAnimSequence에는 PreviewSkeletalMesh 참조가 아직 없음.
	// 이미 로드된 SkeletalMesh 중 같은 Skeleton을 쓰는 것을 먼저 찾습니다.
	for (const auto& Pair : FMeshManager::SkeletalMeshCache)
	{
		USkeletalMesh* Mesh = Pair.second;
		const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		if (MeshAsset && MeshAsset->SkeletonPath == SkeletonPath)
		{
			return Mesh;
		}
	}

	ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
	if (!Device)
	{
		PreviewStatusMessage = "Preview Mesh를 찾을 수 없음: 렌더 디바이스가 준비되지 않았습니다.";
		return nullptr;
	}

	// 2차 탐색: 캐시된 Mesh 중에 같은 SkeletonPath를 사용하는 Mesh가 있는지 탐색
	// TODO(AnimationSequenceEditor)
	// - UAnimSequence 또는 import metadata에 Preview Mesh 참조가 생기면 이 휴리스틱은 제거해야 합니다.
	FMeshManager::ScanMeshAssets();
	for (const FMeshAssetListItem& Item : FMeshManager::GetAvailableSkeletalMeshFiles())
	{
		USkeletalMesh* Mesh = FMeshManager::LoadSkeletalMesh(Item.FullPath, Device);
		const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		if (MeshAsset && MeshAsset->SkeletonPath == SkeletonPath)
		{
			return Mesh;
		}
	}

	PreviewStatusMessage = "Can't found Preview Mesh";
	return nullptr;
}

void FAnimSequenceEditorWidget::InitializePreviewWorld()
{
	if (!GEngine || !PreviewSkeletalMesh)
	{
		return;
	}
	// World Context 생성
	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	// PreviewComponent는 AnimSequence 평가 결과를 적용받는 임시 대상입니다
	// 애니메이션은 SkeletalMesh asset에 저장되지 않고, 매 frame CurrentTime에서 평가한 pose만 component-local edit pose로 덮습니다.
	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	PreviewMeshComponent = Actor ? Actor->AddComponent<USkeletalMeshComponent>() : nullptr;
	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->SetSkeletalMesh(PreviewSkeletalMesh);
		Actor->SetRootComponent(PreviewMeshComponent);

		SingleNodeInstance = GUObjectArray.CreateObject<UAnimSingleNodeInstance>();
		PreviewMeshComponent->SetAnimInstance(SingleNodeInstance);
		SingleNodeInstance->Initialize(PreviewMeshComponent);
		SingleNodeInstance->SetAnimation(AnimSequence);
	}

	if (!Actor || !PreviewMeshComponent)
	{
		PreviewStatusMessage = "Preview Mesh를 찾았지만 PreviewComponent 생성에 실패했습니다.";
		if (PreviewWorldHandle.IsValid())
		{
			GEngine->DestroyWorldContext(PreviewWorldHandle);
		}
		return;
	}
	if (Actor)
	{
		Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));
	}

	// Directional Light
	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	if (LightActor)
	{
		LightActor->InitDefaultComponents();
		LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
		if (UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>())
		{
			LightComp->SetShadowBias(0.0f);
			LightComp->PushToScene();
		}
	}
	// Floor Static Mesh
	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	if (FloorActor)
	{
		FloorActor->InitDefaultComponents("Asset/Mesh/BasicShape/Cube_StaticMesh.uasset");
		FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -0.05f));
		FloorActor->SetActorScale(FVector(10.0f, 10.0f, 0.02f));
	}

	// 지금은 MeshEditorViewportClient가 bone gizmo/debug draw와 preview world 렌더링을 이미 제공하므로 재사용합니다.
	// AnimSequence 전용 카메라/조작 정책이 필요해지면 이 파일 안에서 별도 client로 분리할 수 있습니다.
	const ImVec2 RequestedViewportSize = ImGui::GetContentRegionAvail();
	const uint32 InitialWidth = static_cast<uint32>(std::max(1.0f, RequestedViewportSize.x));
	const uint32 InitialHeight = static_cast<uint32>(std::max(1.0f, RequestedViewportSize.y));
	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), InitialWidth, InitialHeight);
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewMeshComponent(PreviewMeshComponent);

	ViewportClient.CreatePreviewGizmo();
	ViewportClient.CreateBoneDebugComponent();
	ViewportClient.ResetCameraToPreviousBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);
	ViewportClient.SetSelectedBone(PreviewSkeletalMesh, -1);

	FSlateApplication::Get().RegisterViewport(&MeshViewportWindow, &ViewportClient);
}

void FAnimSequenceEditorWidget::ReleasePreviewWorld()
{
	if (SingleNodeInstance)
	{
		GUObjectArray.DestroyObject(SingleNodeInstance);
		SingleNodeInstance = nullptr;
	}

	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		FScene& PreviewScene = PreviewWorld->GetScene();
		GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);

		if (PreviewWorldHandle.IsValid())
		{
			GEngine->DestroyWorldContext(PreviewWorldHandle);
		}
	}

	if (ViewportClient.IsRenderable())
	{
		FSlateApplication::Get().UnregisterViewport(&ViewportClient);
		ViewportClient.Release();
	}
}

const FSkeletonAsset* FAnimSequenceEditorWidget::GetEditableSkeletonAsset() const
{
	return AnimSequence ? AnimSequence->GetSkeletonAsset() : nullptr;
}

void FAnimSequenceEditorWidget::ApplyAnimationPoseToPreview(float Time)
{
	EvaluatedLocalPose.Reset();
	bLastPoseEvaluationSucceeded = false;

	if (!AnimSequence)
	{
		return;
	}

	// UAnimSequence::EvaluatePose()가 UAnimDataModel의 FBoneAnimationTrack / FRawAnimSequenceTrack을 샘플링합니다.
	// 여기서는 반환된 local matrix를 임시 pose로만 사용하고, AnimSequence 내부 keyframe 배열은 절대 수정하지 않습니다.
	bLastPoseEvaluationSucceeded = AnimSequence->EvaluatePose(Time, EvaluatedLocalPose, bLooping);
	if (!bLastPoseEvaluationSucceeded)
	{
		return;
	}

	if (!PreviewMeshComponent)
	{
		return;
	}

	// AnimSequence 평가 결과는 skeleton 전체 local pose이므로 batch API로 한 번에 넣습니다.
	// bone마다 setter를 호출하면 CPU skinning이 bone 수만큼 반복되어 Timeline 재생 중 큰 병목이 됩니다.
	TArray<FMatrix> TempMatrices;
	TempMatrices.resize(EvaluatedLocalPose.BoneLocalTransforms.size());
	for (size_t i = 0; i < EvaluatedLocalPose.BoneLocalTransforms.size(); ++i)
		TempMatrices[i] = EvaluatedLocalPose.BoneLocalTransforms[i].ToMatrix();
	PreviewMeshComponent->SetBoneLocalTransformByArray(TempMatrices);
}

void FAnimSequenceEditorWidget::CaptureSelectedBoneOverrideFromPreview()
{
	if (!PreviewMeshComponent || SelectedBoneIndex < 0)
	{
		return;
	}

	/*Gizmo는 ViewportClient 내부에서 component의 bone setter를 직접 호출합니다.
	그 값을 editor override 배열에 다시 받아두어야
	다음 Tick에서 AnimSequence pose를 재평가한 뒤에도
	사용자가 조작한 Bone SRT가 pose 위에 계속 덮여 재생됩니다.*/
	SetEditedBoneTransform(
		SelectedBoneIndex,
		PreviewMeshComponent->GetBoneLocalTransformByIndex(SelectedBoneIndex));
}

FTransform FAnimSequenceEditorWidget::GetCurrentBoneLocalTransformForDetails(int32 BoneIndex) const
{
	for (const FEditorBoneOverride& Override : EditorBoneOverrides)
	{
		if (Override.BoneIndex == BoneIndex)
		{
			return Override.LocalTransform;
		}
	}

	if (PreviewMeshComponent)
	{
		return PreviewMeshComponent->GetBoneLocalTransformByIndex(BoneIndex);
	}

	if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(EvaluatedLocalPose.BoneLocalTransforms.size()))
	{
		return EvaluatedLocalPose.BoneLocalTransforms[BoneIndex];
	}

	const FSkeletonAsset* SkeletonAsset = GetEditableSkeletonAsset();
	if (SkeletonAsset && BoneIndex >= 0 && BoneIndex < static_cast<int32>(SkeletonAsset->Bones.size()))
	{
		return FTransform(SkeletonAsset->Bones[BoneIndex].LocalMatrix);
	}

	return FTransform();
}

void FAnimSequenceEditorWidget::Close()
{
	FAssetEditorWidget::Close();

	ReleasePreviewWorld();

	AnimSequence = nullptr;
	PreviewSkeletalMesh = nullptr;
	PreviewMeshComponent = nullptr;
	SingleNodeInstance = nullptr;
	PreviewStatusMessage.clear();
	EvaluatedLocalPose.Reset();
	bLastPoseEvaluationSucceeded = false;
	SelectedBoneIndex = -1;
	EditorBoneOverrides.clear();
	PreviewNotifyMarkers.clear();
}

void FAnimSequenceEditorWidget::Tick(float DeltaTime)
{
	if (!IsOpen() || !AnimSequence)
	{
		return;
	}

	if (SingleNodeInstance)
	{
		SingleNodeInstance->Update(DeltaTime);
		CurrentTime = SingleNodeInstance->GetCurrentTickTime();

		FPoseContext Pose;
		SingleNodeInstance->GetCurrentPose(Pose);
		SingleNodeInstance->TriggerAnimNotifies();

		if (!Pose.BoneLocalTransforms.empty() && PreviewMeshComponent)
		{
			TArray<FMatrix> Matrices;
			Matrices.resize(Pose.BoneLocalTransforms.size());
			for (size_t i = 0; i < Pose.BoneLocalTransforms.size(); ++i)
			{
				Matrices[i] = Pose.BoneLocalTransforms[i].ToMatrix();
			}
			PreviewMeshComponent->SetBoneLocalTransformByArray(Matrices);
		}
	}
	else
	{
		TickTimeline(DeltaTime);
		ApplyAnimationPoseToPreview(CurrentTime);
	}

	ApplyEditorBoneOverrides();

	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
		if (ViewportClient.IsGizmoHolding())
		{
			CaptureSelectedBoneOverrideFromPreview();
		}
	}
}

// ----------- Render Section ----------------
void FAnimSequenceEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	// ImGui::Image가 들고 있는 SRV lifetime 문제를 피하려고 close는 다음 frame에 수행한다.
	if (bPendingClose)
	{
		Close();
		bPendingClose = false;
		return;
	}

	if (!IsOpen() || !AnimSequence)
	{
		return;
	}

	static float HierarchyWidth = 250.0f;
	static float DetailsWidth = 300.0f;

	bool bWindowOpen = true;
	FString VisibleTitle = "Animation Sequence Editor";
	if (AnimSequence)
	{
		const FString& AssetPath = AnimSequence->GetAssetPathFileName();
		if (!AssetPath.empty())
		{
			VisibleTitle += " - ";
			VisibleTitle += AssetPath;
		}
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;
	if (ViewportClient.IsMouseOverViewport())
	{
		WindowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	}

	const FString WindowTitle = VisibleTitle + WindowIdSuffix;
	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen, WindowFlags))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			bPendingClose = true;
		}
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ViewportClient.IsRenderable())
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);
	}

	FString SaveReason;
	const bool bHasAssetPath = AnimSequence && !AnimSequence->GetAssetPathFileName().empty()
		&& AnimSequence->GetAssetPathFileName() != "None";
	const bool bHasDataModel = AnimSequence && AnimSequence->GetDataModel();
	const bool bCanSave = bHasAssetPath && bHasDataModel;
	if (!bHasAssetPath)
	{
		SaveReason = "Unsaved AnimSequence path.";
	}
	else if (!bHasDataModel)
	{
		SaveReason = "AnimDataModel is missing.";
	}

	ImGui::BeginDisabled(!bCanSave);
	if (ImGui::Button("Save"))
	{
		if (FAnimSequenceManager::Get().Save(AnimSequence))
		{
			ClearDirty();
			PreviewStatusMessage = "AnimSequence saved.";
		}
		else
		{
			PreviewStatusMessage = "AnimSequence save failed.";
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (!bCanSave)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", SaveReason.c_str());
	}
	else
	{
		ImGui::TextDisabled("%s", IsDirty() ? "Unsaved changes" : "Saved");
	}
	ImGui::Separator();

	constexpr float SplitterH = 4.0f;
	const float MainHeight = std::max(220.0f, ImGui::GetContentRegionAvail().y - SplitterH - TimelinePanelHeight);
	ImGui::BeginChild("##AnimSequenceMainArea", ImVec2(0.0f, MainHeight), false);

	ImGui::BeginChild("SkeletonTree", ImVec2(HierarchyWidth, 0), true);
	ImGui::Text("Skeleton Tree");
	ImGui::Separator();

	if (const UAnimDataModel* DataModel = AnimSequence ? AnimSequence->GetDataModel() : nullptr)
	{
		if (DataModel->GetNumBoneTracks() == 0)
		{
			ImGui::TextDisabled("No Animation Tracks");
			ImGui::Separator();
		}
	}
	else
	{
		ImGui::TextDisabled("No AnimDataModel");
		ImGui::Separator();
	}

	if (const FSkeletonAsset* SkeletonAsset = GetEditableSkeletonAsset())
	{
		if (SkeletonAsset->Bones.empty())
		{
			ImGui::TextDisabled("No skeleton bones.");
		}
		else
		{
			for (int32 i = 0; i < static_cast<int32>(SkeletonAsset->Bones.size()); ++i)
			{
				if (SkeletonAsset->Bones[i].ParentIndex == -1)
				{
					RenderSkeletonTree(SkeletonAsset, i);
				}
			}
		}
	}
	else
	{
		ImGui::TextDisabled("No skeleton.");
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

	ImGui::Button("##AnimSequenceSplitter", ImVec2(4.0f, -1.0f));
	if (ImGui::IsItemActive())
	{
		HierarchyWidth += ImGui::GetIO().MouseDelta.x;
		HierarchyWidth = std::clamp(HierarchyWidth, 100.0f, ImGui::GetWindowWidth() - 100.0f);
	}
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}

	ImGui::PopStyleColor(3);
	ImGui::SameLine();

	ImGui::BeginGroup();
	RenderViewportPanel(DeltaTime);
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::BeginChild("AnimSequenceBoneDetails", ImVec2(DetailsWidth, 0), true);
	RenderBoneDetailsPanel();
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	RenderRelatedAnimSequenceList();
	ImGui::EndChild();

	ImGui::EndChild();

	// --- 타임라인 / 메인 패널 수직 스플리터 ---
	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
	ImGui::Button("##TimelineSplitter", ImVec2(-1.0f, SplitterH));
	if (ImGui::IsItemActive())
	{
		TimelinePanelHeight -= ImGui::GetIO().MouseDelta.y;
		TimelinePanelHeight = std::clamp(TimelinePanelHeight, 80.0f, ImGui::GetWindowHeight() - 150.0f);
	}
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	ImGui::PopStyleColor(3);

	RenderTimelinePanel();

	ImGui::End();

	if (!bWindowOpen)
	{
		bPendingClose = true;
	}
}

void FAnimSequenceEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen() && ViewportClient.IsRenderable())
	{
		OutClients.push_back(const_cast<FMeshEditorViewportClient*>(&ViewportClient));
	}
}
// --- 동일한 Bone을 공유하는 Sequence list Render ImGui 묶음---
void FAnimSequenceEditorWidget::RenderRelatedAnimSequenceList()
{
	ImGui::Text("Anim Sequences");
	ImGui::Separator();

	if (RelatedAnimSequences.empty())
	{
		ImGui::TextDisabled("No related sequences.");
		return;
	}

	const FString CurrentPath = AnimSequence ? AnimSequence->GetAssetPathFileName() : FString();
	const float ListHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.35f, 120.0f, 260.0f);

	ImGui::BeginChild("##RelatedAnimSequenceList", ImVec2(0.0f, ListHeight), true);
	for (const FRelatedAnimSequenceItem& Item : RelatedAnimSequences)
	{
		const bool bSelected = Item.Path == CurrentPath;
		if (ImGui::Selectable(Item.Name.c_str(), bSelected, ImGuiSelectableFlags_AllowDoubleClick))
		{
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				OpenRelatedAnimSequence(Item.Path);
				ImGui::EndChild();
				return;
			}
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(Item.Name.c_str());
			ImGui::EndTooltip();
		}
	}
	ImGui::EndChild();
}

void FAnimSequenceEditorWidget::RenderSkeletonTree(const FSkeletonAsset* SkeletonAsset, int32 BoneIndex)
{
	if (!SkeletonAsset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(SkeletonAsset->Bones.size()))
	{
		return;
	}

	const FBone& Bone = SkeletonAsset->Bones[BoneIndex];

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_DefaultOpen;

	if (BoneIndex == SelectedBoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	bool bHasChildren = false;
	for (int32 i = BoneIndex + 1; i < static_cast<int32>(SkeletonAsset->Bones.size()); ++i)
	{
		if (SkeletonAsset->Bones[i].ParentIndex == BoneIndex)
		{
			bHasChildren = true;
			break;
		}
	}

	if (!bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	// Skeleton Tree는 Preview Mesh가 없어도 AnimSequence의 SkeletonAsset만으로 표시할 수 있습니다.
	// 선택된 Bone index는 이후 Timeline pose 평가와 Detail override가 같은 index를 바라보게 하는 기준점입니다.
	const bool bOpen = ImGui::TreeNodeEx(Bone.Name.c_str(), Flags);

	if (ImGui::IsItemClicked())
	{
		SetSelectedBones(BoneIndex);
	}

	if (bOpen && bHasChildren)
	{
		for (int32 i = BoneIndex + 1; i < static_cast<int32>(SkeletonAsset->Bones.size()); ++i)
		{
			if (SkeletonAsset->Bones[i].ParentIndex == BoneIndex)
			{
				RenderSkeletonTree(SkeletonAsset, i);
			}
		}
		ImGui::TreePop();
	}
}

void FAnimSequenceEditorWidget::RenderViewportPanel(float Deltatime)
{
	(void)Deltatime;

	const float DetailsWidth = 300.0f;
	float AvailableWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
	AvailableWidth = std::max(1.0f, AvailableWidth);
	const ImVec2 Size(AvailableWidth, ImGui::GetContentRegionAvail().y);
	const ImVec2 ViewportPos = ImGui::GetCursorScreenPos();

	if (!ViewportClient.IsRenderable())
	{
		ImGui::BeginChild("AnimSequenceMissingPreviewViewport", Size, true);
		ImGui::TextDisabled("Preview Mesh를 찾을 수 없음");
		ImGui::Spacing();
		if (!PreviewStatusMessage.empty())
		{
			ImGui::TextWrapped("%s", PreviewStatusMessage.c_str());
		}
		ImGui::Spacing();
		ImGui::TextWrapped("TODO(AnimationSequenceEditor): 추후 UAnimSequence 또는 import metadata에 Preview SkeletalMesh 참조를 저장하면, 같은 Skeleton을 스캔하는 임시 경로를 제거해야 합니다.");
		ImGui::EndChild();
		return;
	}

	ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

	FViewport* VP = ViewportClient.GetViewport();
	if (VP && Size.x > 0.0f && Size.y > 0.0f)
	{
		VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
		MeshViewportWindow.SetRect(FRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y));

		if (VP->GetSRV())
		{
			ImGui::Image((ImTextureID)VP->GetSRV(), Size);
		}
		else
		{
			ImGui::Dummy(Size);
		}

		constexpr float ToolbarHeight = 28.0f;

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled(
			ViewportPos,
			ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ToolbarHeight),
			IM_COL32(40, 40, 40, 255));

		FViewportToolbarContext Context;
		Context.Renderer = &GEngine->GetRenderer();
		Context.Gizmo = ViewportClient.GetGizmo();
		Context.Settings = &FEditorSettings::Get().MeshEditorViewportSettings;
		Context.RenderOptions = &ViewportClient.GetRenderOptions();
		Context.ToolbarLeft = ViewportPos.x;
		Context.ToolbarTop = ViewportPos.y;
		Context.ToolbarWidth = Size.x;
		Context.bReservePlayStopSpace = false;
		Context.bShowAddActor = false;
		Context.OnCoordSystemToggled = [&]()
		{
			FGizmoToolSettings& Settings = FEditorSettings::Get().MeshEditorViewportSettings.Gizmo;
			Settings.CoordSystem = (Settings.CoordSystem == EEditorCoordSystem::World) ? EEditorCoordSystem::Local : EEditorCoordSystem::World;

			ViewportClient.ApplyTransformSettingsToGizmo();
		};
		Context.OnSettingsChanged = [&]()
		{
			ViewportClient.ApplyTransformSettingsToGizmo();
		};
		Context.OnRenderViewModeExtras = [&]()
		{
			const EBoneDebugDrawMode CurrentBoneDrawMode = ViewportClient.GetBoneDebugDrawMode();
			int32 BoneDrawMode = static_cast<int32>(CurrentBoneDrawMode);
			ImGui::Text("Bone Display");
			ImGui::RadioButton("Selected Bone", &BoneDrawMode, static_cast<int32>(EBoneDebugDrawMode::SelectedOnly));
			ImGui::RadioButton("All Bones", &BoneDrawMode, static_cast<int32>(EBoneDebugDrawMode::AllBones));
			if (BoneDrawMode != static_cast<int32>(CurrentBoneDrawMode))
			{
				ViewportClient.SetBoneDebugDrawMode(static_cast<EBoneDebugDrawMode>(BoneDrawMode));
			}
			ImGui::Separator();

			bool bHeatmap = ViewportClient.IsBoneWeightHeatmapEnabled();

			if (ImGui::Checkbox("Bone Weight Heatmap", &bHeatmap))
			{
				ViewportClient.SetBoneWeightHeatmapEnabled(bHeatmap);
			}
		};

		FViewportToolbar::Render(Context);
		RenderStatsOverlay(DrawList, ViewportPos);
	}
}

void FAnimSequenceEditorWidget::RenderBoneDetailsPanel()
{
	ImGui::Text("Bone Details");
	ImGui::Separator();

	const FSkeletonAsset* SkeletonAsset = GetEditableSkeletonAsset();
	if (!SkeletonAsset || SelectedBoneIndex == -1)
	{
		ImGui::TextDisabled("Select a bone to edit.");
		return;
	}

	if (SelectedBoneIndex < 0 || SelectedBoneIndex >= static_cast<int32>(SkeletonAsset->Bones.size()))
	{
		ImGui::TextDisabled("Invalid bone selection.");
		return;
	}

	const FBone& Bone = SkeletonAsset->Bones[SelectedBoneIndex];

	ImGui::Text("Name: %s", Bone.Name.c_str());
	ImGui::Text("Index: %d", SelectedBoneIndex);
	ImGui::Dummy(ImVec2(0, 10));

	FTransform LocalTransform = GetCurrentBoneLocalTransformForDetails(SelectedBoneIndex);

	if (ImGui::BeginTable("##AnimSequenceBoneTransformTable", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Location");
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);
		FVector Location = LocalTransform.Location;
		if (ImGui::DragFloat3("##AnimSequenceBoneLocation", &Location.X, 0.1f))
		{
			LocalTransform.Location = Location;
			SetEditedBoneTransform(SelectedBoneIndex, LocalTransform);
		}

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Rotation");
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);
		FVector Rotation = LocalTransform.GetRotator().ToVector();
		if (ImGui::DragFloat3("##AnimSequenceBoneRotation", &Rotation.X, 0.1f))
		{
			LocalTransform.SetRotation(FRotator(Rotation));
			SetEditedBoneTransform(SelectedBoneIndex, LocalTransform);
		}

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Scale");
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);
		FVector Scale = LocalTransform.Scale;
		if (ImGui::DragFloat3("##AnimSequenceBoneScale", &Scale.X, 0.1f, 0.01f))
		{
			LocalTransform.Scale = Scale;
			SetEditedBoneTransform(SelectedBoneIndex, LocalTransform);
		}

		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::Separator();
	/*ImGui::TextDisabled("Preview override only");
	ImGui::TextWrapped(
		"Detail/Gizmo에서 바꾼 Bone SRT는 AnimSequence keyframe을 수정하지 않고 Preview pose 위에만 덮습니다. "
		"저장 가능한 Additive/Override track 구조가 생기면 이 임시 상태를 별도 데이터로 옮겨야 합니다.");*/
}

void FAnimSequenceEditorWidget::RenderStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos) const
{
	if (!DrawList)
	{
		return;
	}

	size_t VertexCount = 0;
	size_t TriangleCount = 0;
	size_t BoneCount = 0;

	if (PreviewSkeletalMesh)
	{
		if (const FSkeletalMesh* Asset = PreviewSkeletalMesh->GetSkeletalMeshAsset())
		{
			VertexCount = Asset->Vertices.size();
			TriangleCount = Asset->Indices.size() / 3;
		}
	}
	if (const FSkeletonAsset* SkeletonAsset = GetEditableSkeletonAsset())
	{
		BoneCount = SkeletonAsset->Bones.size();
	}

	const FString Text =
		"Triangles: " + FormatAnimSequenceStatCount(TriangleCount) + "\n" +
		"Vertices: " + FormatAnimSequenceStatCount(VertexCount) + "\n" +
		"Bones: " + FormatAnimSequenceStatCount(BoneCount);

	const ImVec2 TextPos(ViewportPos.x + 8.0f, ViewportPos.y + 36.0f);
	DrawList->AddText(ImVec2(TextPos.x + 1.0f, TextPos.y + 1.0f), IM_COL32(0, 0, 0, 220), Text.c_str());
	DrawList->AddText(TextPos, IM_COL32(235, 238, 242, 255), Text.c_str());
}

// ----- Timeline 관련 계산 기능 ----------
float FAnimSequenceEditorWidget::GetTimelineFrameStep() const
{
	float FrameRate = AnimSequence ? AnimSequence->GetSamplingFrameRate() : 0.0f;
	if (FrameRate <= 0.0f)
	{
		if (const UAnimDataModel* DataModel = AnimSequence ? AnimSequence->GetDataModel() : nullptr)
		{
			FrameRate = DataModel->GetFrameRate();
		}
	}

	return FrameRate > 0.0f ? (1.0f / FrameRate) : (1.0f / 30.0f);
}

void FAnimSequenceEditorWidget::SetTimelineTime(float NewTime)
{
	const float ClampedTime = std::clamp(NewTime, 0.0f, std::max(0.0f, PlayLength));

	if (SingleNodeInstance)
	{
		SingleNodeInstance->Stop();
		SingleNodeInstance->SetCurrentTime(ClampedTime);
		CurrentTime = SingleNodeInstance->GetCurrentTickTime();
	}
	else
	{
		bPlaying = false;
		PreviousTime = CurrentTime;
		CurrentTime = ClampedTime;
	}
}

void FAnimSequenceEditorWidget::StopTimelinePlayback()
{
	if (SingleNodeInstance)
	{
		SingleNodeInstance->Stop();
		CurrentTime = SingleNodeInstance->GetCurrentTickTime();
	}
	else
	{
		bPlaying = false;
	}
}

void FAnimSequenceEditorWidget::PlayTimeline(float PlayRate)
{
	if (PlayLength <= 0.0f)
	{
		return;
	}

	TimelinePlayRate = PlayRate;

	if (SingleNodeInstance)
	{
		if (PlayRate > 0.0f && CurrentTime >= PlayLength)
		{
			SingleNodeInstance->SetCurrentTime(0.0f);
		}
		else if (PlayRate < 0.0f && CurrentTime <= 0.0f)
		{
			SingleNodeInstance->SetCurrentTime(PlayLength);
		}

		SingleNodeInstance->SetPlayRate(PlayRate);
		SingleNodeInstance->SetLooping(bLooping);
		SingleNodeInstance->Play(bLooping);
		CurrentTime = SingleNodeInstance->GetCurrentTickTime();
	}
	else
	{
		if (PlayRate > 0.0f && CurrentTime >= PlayLength)
		{
			CurrentTime = 0.0f;
		}
		else if (PlayRate < 0.0f && CurrentTime <= 0.0f)
		{
			CurrentTime = PlayLength;
		}

		bPlaying = true;
	}
}

void FAnimSequenceEditorWidget::StepTimelineFrame(int32 FrameOffset)
{
	SetTimelineTime(CurrentTime + GetTimelineFrameStep() * static_cast<float>(FrameOffset));
}

void FAnimSequenceEditorWidget::TickTimeline(float DeltaTime)
{
	PreviousTime = CurrentTime;
	CurrentTime = std::clamp(CurrentTime, 0.0f, std::max(0.0f, PlayLength));

	if (!bPlaying || PlayLength <= 0.0f)
	{
		return;
	}

	// Tick에서는 Timeline 시간이 먼저 진행되고, 그 다음 단계에서 CurrentTime 기준 pose가 평가됩니다.
	// 이렇게 순서를 고정해야 UI의 playhead, Notify marker trigger 판단, PreviewComponent pose가 같은 시간을 보게 됩니다.
	CurrentTime += DeltaTime * TimelinePlayRate;

	if (bLooping)
	{
		while (CurrentTime > PlayLength) // 정방향 재생
		{
			CurrentTime -= PlayLength;
		}
		while (CurrentTime < 0.0f)		// 역방향 재생
		{
			CurrentTime += PlayLength;
		}
	}
	else
	{
		CurrentTime = std::clamp(CurrentTime, 0.0f, PlayLength);

		if ((TimelinePlayRate >= 0.0f && CurrentTime >= PlayLength) ||
			(TimelinePlayRate < 0.0f && CurrentTime <= 0.0f))
		{
			bPlaying = false;
		}
	}

	CurrentTime = std::clamp(CurrentTime, 0.0f, PlayLength);
}

void FAnimSequenceEditorWidget::RenderTimelinePanel()
{
	constexpr float LeftPanelWidth = 180.0f;
	constexpr float RulerHeight = 28.0f;
	const bool bHasTimelineLength = PlayLength > 0.0f;

	ImGui::BeginChild(
		"##AnimSequenceTimelinePanel",
		ImVec2(0.0f, TimelinePanelHeight),
		true,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// Timeline은 AnimSequence editor의 핵심 상태 표시 영역이므로 PlayLength가 0이어도 숨기지 않습니다.
	// 길이가 0인 asset은 컨트롤만 비활성화하고, 아래 ruler를 0초 기준으로 그려 metadata 문제를 바로 확인할 수 있게 합니다.
	const bool bCanPlay = bHasTimelineLength;
	const float ButtonWidth = 28.0f;

	auto DrawTimelineButton = [&](const char* Label, const char* Tooltip, const ImVec2& Size) -> bool
	{
		const bool bClicked = ImGui::Button(Label, Size);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(Tooltip);
			ImGui::EndTooltip();
		}
		return bClicked;
	};

	ImGui::BeginDisabled(!bCanPlay);
	if (DrawTimelineButton("|<##FirstFrame", "첫 프레임", ImVec2(ButtonWidth, 0.0f)))
	{
		SetTimelineTime(0.0f);
	}
	ImGui::EndDisabled();

	ImGui::SameLine(0.0f, 3.0f);

	ImGui::BeginDisabled(!bCanPlay);
	if (DrawTimelineButton("<|##PreviousFrame", "이전 프레임", ImVec2(ButtonWidth, 0.0f)))
	{
		StepTimelineFrame(-1);
	}
	ImGui::EndDisabled();

	ImGui::SameLine(0.0f, 3.0f);

	ImGui::BeginDisabled(!bCanPlay);
	if (DrawTimelineButton("<##ReversePlay", "역재생", ImVec2(ButtonWidth, 0.0f)))
	{
		PlayTimeline(-std::abs(TimelinePlayRate));
	}
	ImGui::EndDisabled();

	ImGui::SameLine(0.0f, 3.0f);

	if (DrawTimelineButton("||##Pause", "일시정지", ImVec2(ButtonWidth, 0.0f)))
	{
		StopTimelinePlayback();
	}

	ImGui::SameLine(0.0f, 3.0f);

	ImGui::BeginDisabled(!bCanPlay);
	if (DrawTimelineButton(">##ForwardPlay", "정재생", ImVec2(ButtonWidth, 0.0f)))
	{
		PlayTimeline(std::abs(TimelinePlayRate));
	}
	ImGui::EndDisabled();

	ImGui::SameLine(0.0f, 3.0f);

	ImGui::BeginDisabled(!bCanPlay);
	if (DrawTimelineButton("|>##NextFrame", "다음 프레임", ImVec2(ButtonWidth, 0.0f)))
	{
		StepTimelineFrame(1);
	}
	ImGui::EndDisabled();

	ImGui::SameLine(0.0f, 3.0f);

	ImGui::BeginDisabled(!bCanPlay);
	if (DrawTimelineButton(">|##LastFrame", "마지막 프레임", ImVec2(ButtonWidth, 0.0f)))
	{
		SetTimelineTime(PlayLength);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Checkbox("Loop", &bLooping))
	{
		if (SingleNodeInstance)
			SingleNodeInstance->SetLooping(bLooping);
	}

	ImGui::SameLine();
	if (PreviewMeshComponent)
	{
		bool bIgnore = PreviewMeshComponent->GetIgnoreRootMotion();
		if (ImGui::Checkbox("Ignore Root Motion", &bIgnore))
		{
			PreviewMeshComponent->SetIgnoreRootMotion(bIgnore);
		}
	}
	else
	{
		ImGui::BeginDisabled();
		bool bDummy = true;
		ImGui::Checkbox("Ignore Root Motion", &bDummy);
		ImGui::EndDisabled();
	}

	ImGui::SameLine();

	ImGui::SetNextItemWidth(60.0f);
	float SpeedInput = std::abs(TimelinePlayRate);
	if (ImGui::DragFloat("Speed", &SpeedInput, 0.05f, 0.1f, 8.0f, "%.2fx"))
	{
		SpeedInput = std::max(0.1f, SpeedInput);
		const float Sign = (TimelinePlayRate >= 0.0f) ? 1.0f : -1.0f;
		TimelinePlayRate = Sign * SpeedInput;
		if (SingleNodeInstance && SingleNodeInstance->IsPlaying())
		{
			SingleNodeInstance->SetPlayRate(TimelinePlayRate);
		}
	}

	ImGui::SameLine();

	ImGui::SetNextItemWidth(120.0f);
	float TimeInput = CurrentTime;
	ImGui::BeginDisabled(!bHasTimelineLength);
	if (ImGui::DragFloat("Time", &TimeInput, 0.01f, 0.0f, PlayLength, "%.3f"))
	{
		if (SingleNodeInstance)
		{
			SingleNodeInstance->Stop();
			SingleNodeInstance->SetCurrentTime(TimeInput);
			CurrentTime = SingleNodeInstance->GetCurrentTickTime();
		}
		else
		{
			bPlaying = false;
			PreviousTime = CurrentTime;
			CurrentTime = std::clamp(TimeInput, 0.0f, PlayLength);
		}
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::Text("/ %.3f sec", PlayLength);
	if (!bHasTimelineLength)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("Timeline length is 0. Frame/key metadata may be missing.");
	}

	TArray<FAnimNotifyEvent>& NotifyMarkers = GetNotifyMarkers();
	if (SelectedNotifyIndex >= static_cast<int32>(NotifyMarkers.size()))
	{
		SelectedNotifyIndex = -1;
	}

	if (SelectedNotifyIndex >= 0)
	{
		FAnimNotifyEvent& SelectedNotify = NotifyMarkers[SelectedNotifyIndex];

		ImGui::Spacing();
		ImGui::PushID("AnimSequenceSelectedNotifyEditor");

		// Notify 편집은 DataModel에 즉시 반영되고 Save 버튼으로 AnimSequence package에 저장됩니다.
		ImGui::TextDisabled("Selected Notify");
		ImGui::SameLine();

		char NameBuffer[128];
		const FString CurrentNotifyName = SelectedNotify.NotifyName.ToString();
		std::snprintf(NameBuffer, sizeof(NameBuffer), "%s", CurrentNotifyName.c_str());

		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::InputText("Name", NameBuffer, sizeof(NameBuffer)))
		{
			SelectedNotify.NotifyName = FName(NameBuffer);
			MarkDirty();
		}

		ImGui::SameLine();

		float NotifyTime = SelectedNotify.TriggerTime;
		ImGui::SetNextItemWidth(110.0f);
		if (ImGui::DragFloat("Time", &NotifyTime, 0.01f, 0.0f, PlayLength, "%.3f"))
		{
			MoveVisualNotify(SelectedNotifyIndex, NotifyTime);
		}

		ImGui::SameLine();

		float NotifyDuration = SelectedNotify.Duration;
		ImGui::SetNextItemWidth(110.0f);
		if (ImGui::DragFloat("Duration", &NotifyDuration, 0.01f, 0.0f, PlayLength, "%.3f"))
		{
			SelectedNotify.Duration = std::clamp(NotifyDuration, 0.0f, PlayLength);
			MarkDirty();
		}

		ImGui::SameLine();
		if (ImGui::Button("Delete"))
		{
			DeleteSelectedVisualNotify();
		}

		// --- Notify 타입 선택 드롭다운 ---
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		const char* CurrentTypeName = SelectedNotify.NotifyTrigger
			? SelectedNotify.NotifyTrigger->GetClass()->GetName() : "(None)";

		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::BeginCombo("Notify Type", CurrentTypeName))
		{
			if (ImGui::Selectable("(None)", SelectedNotify.NotifyTrigger == nullptr))
			{
				delete SelectedNotify.NotifyTrigger;
				SelectedNotify.NotifyTrigger = nullptr;
				MarkDirty();
			}

			// Registry에서 미리 등록된 목록 가져오기
			const TMap<FString, UClass*>& NotifyMap = FNotifyRegistry::Get().GetNotifyClasses();

			for (const auto& Pair : NotifyMap)
			{
				const FString& ClassName = Pair.first;
				UClass* Cls = Pair.second;

				const bool bSelected = SelectedNotify.NotifyTrigger &&
					SelectedNotify.NotifyTrigger->GetClass() == Cls;

				if (ImGui::Selectable(ClassName.c_str(), bSelected))
				{
					// 기존 객체 안전하게 삭제
					delete SelectedNotify.NotifyTrigger;

					// 팩토리를 통해 클래스 이름으로 새 객체 생성
					UObject* Obj = FObjectFactory::Get().Create(ClassName);
					SelectedNotify.NotifyTrigger = Cast<UNotify>(Obj);

					MarkDirty();
				}
			}
			ImGui::EndCombo();
		}

		// --- 리플렉션 기반 프로퍼티 인스펙터 ---
		if (UNotify* Notify = SelectedNotify.NotifyTrigger)
		{
			TArray<const FProperty*> Props;
			Notify->GetEditableProperties(Props);

			if (!Props.empty())
			{
				ImGui::Spacing();
				if (ImGui::BeginTable("##NotifyProps", 2,
					ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_BordersInnerV |
					ImGuiTableFlags_PadOuterX |
					ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthFixed, 130.0f);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					for (int32 i = 0; i < static_cast<int32>(Props.size()); ++i)
					{
						ImGui::TableNextRow();
						ImGui::PushID(i);

						ImGui::TableSetColumnIndex(0);
						ImGui::AlignTextToFramePadding();
						ImGui::TextUnformatted(Props[i]->Name.c_str());

						ImGui::TableSetColumnIndex(1);
						ImGui::SetNextItemWidth(-1);
						if (DrawNotifyPropertyRow(*Props[i], Notify))
							MarkDirty();

						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}
		}

		ImGui::PopID();
	}

	const ImVec2 PanelPos = ImGui::GetCursorScreenPos();
	const ImVec2 PanelSize = ImGui::GetContentRegionAvail();
	const ImVec2 PanelEnd(PanelPos.x + PanelSize.x, PanelPos.y + PanelSize.y);

	const float TimelineX = PanelPos.x + LeftPanelWidth;
	const float TimelineWidth = std::max(1.0f, PanelEnd.x - TimelineX);
	const float TrackTop = PanelPos.y + RulerHeight;
	const float VisibleRange = std::max(0.05f, ViewEndTime - ViewStartTime);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	ImGui::InvisibleButton(
		"##AnimSequenceTimelineCanvas",
		PanelSize,
		ImGuiButtonFlags_MouseButtonLeft |
		ImGuiButtonFlags_MouseButtonRight |
		ImGuiButtonFlags_MouseButtonMiddle);

	const bool bCanvasHovered = ImGui::IsItemHovered();
	const bool bCanvasActive = ImGui::IsItemActive();
	const ImVec2 Mouse = ImGui::GetIO().MousePos;

	DrawList->AddRectFilled(PanelPos, PanelEnd, ImGui::GetColorU32(ImGuiCol_WindowBg));
	DrawList->AddRect(PanelPos, PanelEnd, ImGui::GetColorU32(ImGuiCol_Border));

	DrawList->AddText(
		ImVec2(PanelPos.x + 10.0f, TrackTop + 7.0f),
		ImGui::GetColorU32(ImGuiCol_Text),
		"Notifies");

	if (bCanvasHovered && ImGui::GetIO().KeyCtrl && std::fabs(ImGui::GetIO().MouseWheel) > 0.0f)
	{
		const float MouseTime = XToTime(
			std::clamp(Mouse.x, TimelineX, PanelEnd.x),
			TimelineX,
			TimelineWidth,
			VisibleRange);

		const float ZoomFactor = ImGui::GetIO().MouseWheel > 0.0f ? 0.85f : 1.0f / 0.85f;
		const float NewRange = std::clamp(VisibleRange * ZoomFactor, 0.05f, 100000.0f);
		const float AnchorAlpha = std::clamp((MouseTime - ViewStartTime) / VisibleRange, 0.0f, 1.0f);

		ViewStartTime = MouseTime - NewRange * AnchorAlpha;
		ViewEndTime = ViewStartTime + NewRange;
	}

	if (bCanvasActive &&
		(ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
		 ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
	{
		const float DeltaTime = -(ImGui::GetIO().MouseDelta.x / TimelineWidth) * VisibleRange;
		ViewStartTime += DeltaTime;
		ViewEndTime += DeltaTime;
	}

	const bool bMouseInRuler =
		bCanvasHovered &&
		Mouse.x >= TimelineX &&
		Mouse.x <= PanelEnd.x &&
		Mouse.y >= PanelPos.y &&
		Mouse.y <= TrackTop;

	if (bMouseInRuler && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		float NewTime = std::clamp(XToTime(Mouse.x, TimelineX, TimelineWidth, VisibleRange), 0.0f, PlayLength);
		if (SingleNodeInstance)
		{
			SingleNodeInstance->Stop();
			SingleNodeInstance->SetCurrentTime(NewTime);
			CurrentTime = SingleNodeInstance->GetCurrentTickTime();
		}
		else
		{
			bPlaying = false;
			PreviousTime = CurrentTime;
			CurrentTime = NewTime;
		}
	}

	const ImU32 GridColor = IM_COL32(160, 160, 160, 80);
	const ImU32 MinorGridColor = IM_COL32(160, 160, 160, 36);

	const float MajorStepCandidates[] =
	{
		0.01f, 0.05f, 0.1f, 0.25f, 0.5f,
		1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f
	};

	float MajorStep = 1.0f;
	for (float Candidate : MajorStepCandidates)
	{
		if ((Candidate / VisibleRange) * TimelineWidth >= 72.0f)
		{
			MajorStep = Candidate;
			break;
		}
	}

	const float MinorStep = MajorStep * 0.5f;
	const float FirstMinor = std::floor(ViewStartTime / MinorStep) * MinorStep;

	DrawList->PushClipRect(ImVec2(TimelineX, PanelPos.y), PanelEnd, true);

	for (float Time = FirstMinor; Time <= ViewEndTime + MinorStep; Time += MinorStep)
	{
		const float X = TimeToX(Time, TimelineX, TimelineWidth, VisibleRange);
		const bool bMajor = std::fabs(std::fmod(std::fabs(Time), MajorStep)) < 0.0001f;

		DrawList->AddLine(
			ImVec2(X, PanelPos.y),
			ImVec2(X, PanelEnd.y),
			bMajor ? GridColor : MinorGridColor);

		if (bMajor)
		{
			char Label[32];
			std::snprintf(Label, sizeof(Label), "%.2f", Time);

			DrawList->AddText(
				ImVec2(X + 4.0f, PanelPos.y + 6.0f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled),
				Label);
		}
	}

	const float StartX = TimeToX(0.0f, TimelineX, TimelineWidth, VisibleRange);
	const float EndX = TimeToX(PlayLength, TimelineX, TimelineWidth, VisibleRange);

	DrawList->AddLine(
		ImVec2(StartX, PanelPos.y),
		ImVec2(StartX, PanelEnd.y),
		IM_COL32(44, 220, 84, 255),
		2.0f);

	DrawList->AddLine(
		ImVec2(EndX, PanelPos.y),
		ImVec2(EndX, PanelEnd.y),
		IM_COL32(220, 24, 24, 255),
		2.0f);

	DrawList->PopClipRect();

	DrawNotifyTrack(
		DrawList,
		PanelPos,
		PanelEnd,
		TimelineX,
		TrackTop,
		TimelineWidth,
		VisibleRange,
		bCanvasHovered,
		Mouse);

	const float PlayheadX = TimeToX(CurrentTime, TimelineX, TimelineWidth, VisibleRange);

	DrawList->PushClipRect(ImVec2(TimelineX, PanelPos.y), PanelEnd, true);

	DrawList->AddLine(
		ImVec2(PlayheadX, PanelPos.y),
		ImVec2(PlayheadX, PanelEnd.y),
		IM_COL32(255, 255, 255, 255),
		2.0f);

	DrawList->AddRectFilled(
		ImVec2(PlayheadX - 6.0f, PanelPos.y + 2.0f),
		ImVec2(PlayheadX + 6.0f, PanelPos.y + 16.0f),
		IM_COL32(255, 64, 64, 255),
		2.0f);

	DrawList->PopClipRect();

	ImGui::EndChild();
}

void FAnimSequenceEditorWidget::DrawNotifyTrack(
	ImDrawList* DrawList,
	const ImVec2& PanelPos,
	const ImVec2& PanelEnd,
	float TimelineX,
	float TrackTop,
	float TimelineWidth,
	float VisibleRange,
	bool bCanvasHovered,
	const ImVec2& Mouse)
{
	constexpr float NotifyRowHeight = 30.0f;

	const float RowY = TrackTop;
	const float RowMinY = RowY;
	const float RowMaxY = RowY + NotifyRowHeight;

	TArray<FAnimNotifyEvent>& Markers = GetNotifyMarkers();

	DrawList->AddRectFilled(
		ImVec2(PanelPos.x, RowMinY),
		ImVec2(PanelEnd.x, RowMaxY),
		IM_COL32(45, 45, 45, 180));

	DrawList->AddLine(
		ImVec2(PanelPos.x, RowMaxY),
		ImVec2(PanelEnd.x, RowMaxY),
		ImGui::GetColorU32(ImGuiCol_Border));

	int32 HoveredNotifyIndex = -1;

	DrawList->PushClipRect(ImVec2(TimelineX, RowMinY), ImVec2(PanelEnd.x, RowMaxY), true);

	for (int32 NotifyIndex = 0; NotifyIndex < static_cast<int32>(Markers.size()); ++NotifyIndex)
	{
		const FAnimNotifyEvent& Notify = Markers[NotifyIndex];

		const float X = TimeToX(Notify.TriggerTime, TimelineX, TimelineWidth, VisibleRange);
		if (X < TimelineX || X > PanelEnd.x)
		{
			continue;
		}

		const bool bSelected = NotifyIndex == SelectedNotifyIndex;

		const ImU32 MarkerColor = bSelected
			? IM_COL32(255, 230, 90, 255)
			: IM_COL32(220, 70, 70, 255);

		if (Notify.Duration > 0.0f)
		{
			const float EndTime = std::clamp(Notify.TriggerTime + Notify.Duration, 0.0f, PlayLength);
			const float EndX = TimeToX(EndTime, TimelineX, TimelineWidth, VisibleRange);
			const float RegionMinX = std::clamp(std::min(X, EndX), TimelineX, PanelEnd.x);
			const float RegionMaxX = std::clamp(std::max(X, EndX), TimelineX, PanelEnd.x);

			// Duration Notify는 아직 정식 runtime event 구조가 없으므로 구간 편집은 단순 표시 수준으로 둡니다.
			// 시간/길이 값은 위의 선택 UI에서 바꾸고, Timeline에는 trigger부터 duration 끝까지의 범위만 시각화합니다.
			if (RegionMaxX > RegionMinX)
			{
				DrawList->AddRectFilled(
					ImVec2(RegionMinX, RowY + 18.0f),
					ImVec2(RegionMaxX, RowY + 25.0f),
					bSelected ? IM_COL32(255, 230, 90, 92) : IM_COL32(220, 70, 70, 72),
					2.0f);
			}
		}

		const ImVec2 P0(X, RowY + 5.0f);
		const ImVec2 P1(X - 6.0f, RowY + 18.0f);
		const ImVec2 P2(X + 6.0f, RowY + 18.0f);

		DrawList->AddTriangleFilled(P0, P1, P2, MarkerColor);
		DrawList->AddTriangle(P0, P1, P2, IM_COL32(20, 20, 20, 255), 1.0f);

		const FString NotifyName = Notify.NotifyName.ToString();

		DrawList->AddText(
			ImVec2(X + 8.0f, RowY + 8.0f),
			ImGui::GetColorU32(ImGuiCol_Text),
			NotifyName.c_str());

		const float Dx = Mouse.x - X;
		const float Dy = Mouse.y - (RowY + 13.0f);

		if ((Dx * Dx + Dy * Dy) <= 12.0f * 12.0f)
		{
			HoveredNotifyIndex = NotifyIndex;
		}
	}

	DrawList->PopClipRect();

	const bool bMouseInNotifyRow =
		bCanvasHovered &&
		Mouse.x >= TimelineX &&
		Mouse.x <= PanelEnd.x &&
		Mouse.y >= RowMinY &&
		Mouse.y <= RowMaxY;

	if (bMouseInNotifyRow && HoveredNotifyIndex >= 0)
	{
		const FAnimNotifyEvent& Notify = Markers[HoveredNotifyIndex];
		const FString NotifyName = Notify.NotifyName.ToString();

		ImGui::BeginTooltip();
		ImGui::Text("Notify: %s", NotifyName.c_str());
		ImGui::Text("Time: %.3f", Notify.TriggerTime);
		ImGui::Text("Duration: %.3f", Notify.Duration);
		ImGui::EndTooltip();
	}

	if (bMouseInNotifyRow && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		SelectedNotifyIndex = HoveredNotifyIndex;

		if (HoveredNotifyIndex >= 0)
		{
			bDraggingNotify = true;
			DraggingNotifyIndex = HoveredNotifyIndex;
		}
	}

	if (bDraggingNotify &&
		DraggingNotifyIndex >= 0 &&
		ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		float NewTime = XToTime(Mouse.x, TimelineX, TimelineWidth, VisibleRange);

		if (!ImGui::GetIO().KeyAlt)
		{
			NewTime = SnapTime(NewTime);
		}

		MoveVisualNotify(DraggingNotifyIndex, NewTime);
	}

	if (bDraggingNotify && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		bDraggingNotify = false;
		DraggingNotifyIndex = -1;

		SortVisualNotifyMarkers();
	}

	if (bMouseInNotifyRow &&
		ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
		!ImGui::IsMouseDragging(ImGuiMouseButton_Right, ImGui::GetIO().MouseDragThreshold))
	{
		ContextTimelineTime = XToTime(Mouse.x, TimelineX, TimelineWidth, VisibleRange);
		ContextTimelineTime = SnapTime(ContextTimelineTime);
		ContextTimelineTime = std::clamp(ContextTimelineTime, 0.0f, PlayLength);

		ImGui::OpenPopup("##AnimNotifyMarkerContext");
	}

	if (ImGui::BeginPopup("##AnimNotifyMarkerContext"))
	{
		ImGui::Text("Time: %.3f", ContextTimelineTime);
		ImGui::Separator();

		if (ImGui::MenuItem("Add Notify Marker"))
		{
			AddVisualNotifyAtTime(ContextTimelineTime);
		}

		ImGui::BeginDisabled(SelectedNotifyIndex < 0);
		if (ImGui::MenuItem("Delete Selected Notify"))
		{
			DeleteSelectedVisualNotify();
		}
		ImGui::EndDisabled();

		ImGui::EndPopup();
	}

	if (SelectedNotifyIndex >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		DeleteSelectedVisualNotify();
	}
}

float FAnimSequenceEditorWidget::TimeToX(
	float Time,
	float TimelineX,
	float TimelineWidth,
	float VisibleRange) const
{
	return TimelineX + ((Time - ViewStartTime) / VisibleRange) * TimelineWidth;
}

float FAnimSequenceEditorWidget::XToTime(
	float X,
	float TimelineX,
	float TimelineWidth,
	float VisibleRange) const
{
	return ViewStartTime + ((X - TimelineX) / TimelineWidth) * VisibleRange;
}

float FAnimSequenceEditorWidget::SnapTime(float Time) const
{
	constexpr float SnapUnit = 0.01f;
	return std::round(Time / SnapUnit) * SnapUnit;
}

TArray<FAnimNotifyEvent>& FAnimSequenceEditorWidget::GetNotifyMarkers()
{
	if (AnimSequence)
	{
		if (UAnimDataModel* DataModel = AnimSequence->GetDataModel())
			return DataModel->GetNotifiesMutable();
	}
	return PreviewNotifyMarkers;
}

void FAnimSequenceEditorWidget::AddVisualNotifyAtTime(float Time)
{
	FAnimNotifyEvent NewNotify;
	NewNotify.TriggerTime = std::clamp(Time, 0.0f, PlayLength);
	NewNotify.Duration = 0.0f;
	NewNotify.NotifyName = FName("NewNotify");

	TArray<FAnimNotifyEvent>& Markers = GetNotifyMarkers();

	Markers.push_back(NewNotify);
	SelectedNotifyIndex = static_cast<int32>(Markers.size()) - 1;

	MarkDirty();
}

void FAnimSequenceEditorWidget::MoveVisualNotify(int32 NotifyIndex, float NewTime)
{
	TArray<FAnimNotifyEvent>& Markers = GetNotifyMarkers();

	if (NotifyIndex < 0 || NotifyIndex >= static_cast<int32>(Markers.size()))
	{
		return;
	}

	Markers[NotifyIndex].TriggerTime = std::clamp(NewTime, 0.0f, PlayLength);
	MarkDirty();
}

void FAnimSequenceEditorWidget::DeleteSelectedVisualNotify()
{
	TArray<FAnimNotifyEvent>& Markers = GetNotifyMarkers();

	if (SelectedNotifyIndex < 0 ||
		SelectedNotifyIndex >= static_cast<int32>(Markers.size()))
	{
		return;
	}

	Markers.erase(Markers.begin() + SelectedNotifyIndex);

	SelectedNotifyIndex = -1;
	DraggingNotifyIndex = -1;
	bDraggingNotify = false;

	MarkDirty();
}

void FAnimSequenceEditorWidget::SortVisualNotifyMarkers()
{
	TArray<FAnimNotifyEvent>& Markers = GetNotifyMarkers();

	const bool bHadSelection =
		SelectedNotifyIndex >= 0 &&
		SelectedNotifyIndex < static_cast<int32>(Markers.size());
	const FAnimNotifyEvent SelectedNotify = bHadSelection ? Markers[SelectedNotifyIndex] : FAnimNotifyEvent();

	std::sort(
		Markers.begin(),
		Markers.end(),
		[](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B)
		{
			return A.TriggerTime < B.TriggerTime;
		});

	if (!bHadSelection)
	{
		return;
	}

	for (int32 NotifyIndex = 0; NotifyIndex < static_cast<int32>(Markers.size()); ++NotifyIndex)
	{
		const FAnimNotifyEvent& Notify = Markers[NotifyIndex];
		if (Notify.TriggerTime == SelectedNotify.TriggerTime &&
			Notify.Duration == SelectedNotify.Duration &&
			Notify.NotifyName == SelectedNotify.NotifyName)
		{
			SelectedNotifyIndex = NotifyIndex;
			return;
		}
	}

	SelectedNotifyIndex = -1;
}

void FAnimSequenceEditorWidget::SetSelectedBones(int32 BoneIndex)
{
	SelectedBoneIndex = BoneIndex;
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.SetSelectedBone(PreviewSkeletalMesh, BoneIndex);
	}
}

void FAnimSequenceEditorWidget::SetEditedBoneTransform(
	int32 BoneIndex,
	const FTransform& LocalTransform)
{
	if (BoneIndex < 0)
	{
		return;
	}

	bool bFound = false;

	for (FEditorBoneOverride& Override : EditorBoneOverrides)
	{
		if (Override.BoneIndex == BoneIndex)
		{
			Override.LocalTransform = LocalTransform;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		FEditorBoneOverride NewOverride;
		NewOverride.BoneIndex = BoneIndex;
		NewOverride.LocalTransform = LocalTransform;
		EditorBoneOverrides.push_back(NewOverride);
	}

	// PreviewComponent가 있을 때만 즉시 시각화합니다.
	// component가 없는 상태에서도 override를 보관하는 이유는 Preview Mesh 연결이 나중에 생겼을 때 같은 UI 흐름을 유지하기 위해서입니다.
	if (PreviewMeshComponent)
	{
		PreviewMeshComponent->SetBoneLocalTransformByIndex(BoneIndex, LocalTransform);
	}
}

void FAnimSequenceEditorWidget::ApplyEditorBoneOverrides()
{
	if (!PreviewMeshComponent)
	{
		return;
	}

	for (const FEditorBoneOverride& Override : EditorBoneOverrides)
	{
		if (Override.BoneIndex < 0)
		{
			continue;
		}

		PreviewMeshComponent->SetBoneLocalTransformByIndex(
			Override.BoneIndex,
			Override.LocalTransform);
	}
}
