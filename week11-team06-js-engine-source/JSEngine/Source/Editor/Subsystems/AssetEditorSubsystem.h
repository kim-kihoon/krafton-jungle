#pragma once

#include "Core/CoreMinimal.h"

class UEditorEngine;
class USkeletalMeshComponent;

// 에디터에서 에셋을 열 때의 공통 진입점입니다.
// Content Browser, Property Widget 등에서 들어오는 요청을 모아
// 필요한 경우 로딩 모달을 먼저 띄운 뒤 실제 Viewer 열기나 에셋 적용을 수행합니다.
class FAssetEditorSubsystem
{
public:
	void Initialize(UEditorEngine* InEditorEngine);

	// 즉시 에셋 에디터를 엽니다. 이미 모달이 필요한 UI 흐름에서는 RequestOpenEditorForAsset을 사용합니다.
	void OpenEditorForAsset(const FString& AssetPath);
	// 다음 UI 프레임에 모달이 보이도록 요청을 큐에 넣고, 이후 실제 에셋 열기를 실행합니다.
	void RequestOpenEditorForAsset(const FString& AssetPath);
	void RequestApplySkeletalMeshToComponent(USkeletalMeshComponent* Component, const FString& SkeletalMeshPath);
	void DrawOpeningAssetModal();

private:
	enum class ERequestType
	{
		None,
		OpenEditorForAsset,
		ImportFbxAndOpenEditor,
		ApplySkeletalMeshToComponent,
	};

	struct FPendingRequest
	{
		ERequestType Type = ERequestType::None;
		FString Path;
		USkeletalMeshComponent* TargetComponent = nullptr;
		int32 DelayFrames = 0;
		bool bModalRequested = false;
	};

	void QueueRequest(ERequestType Type, const FString& Path, USkeletalMeshComponent* TargetComponent = nullptr);
	void ExecutePendingRequest();
	bool FindExistingSkeletalMeshBinaryForFbx(const FString& FbxPath, FString& OutBinaryPath) const;

private:
	UEditorEngine* EditorEngine = nullptr;
	FPendingRequest PendingRequest;
};
