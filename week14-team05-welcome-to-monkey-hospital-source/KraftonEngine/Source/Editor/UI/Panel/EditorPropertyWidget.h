#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Object/Object.h"
#include "Asset/AssetRegistry.h"
#include "Editor/UI/Dialog/FbxImportOptionsDialog.h"
#include "Object/Ptr/WeakObjectPtr.h"

class UActorComponent;
class AActor;

class FEditorPropertyWidget : public FEditorWidget
{
public:
	virtual void Render(float DeltaTime) override;
	void SetShowEditorOnlyComponents(bool bEnable) { bShowEditorOnlyComponents = bEnable; }
	bool IsShowingEditorOnlyComponents() const { return bShowEditorOnlyComponents; }

private:
	enum class ERenameTarget : uint8
	{
		None,
		Actor,
		Component
	};

	//Rename
	void BeginRenameActor(AActor* TargetActor);
	void BeginRenameComponent(UActorComponent* TargetComponent);
	void RenderRenamePopup();
	bool TryRenameActor(AActor* TargetActor, const FString& NewName);
	bool TryRenameComponent(UActorComponent* TargetComponent, const FString& NewName);
	void RenameActor(AActor* PrimaryActor);

	void RenderComponentTree(AActor* Actor);
	void RenderSceneComponentNode(class USceneComponent* Comp);
	void RenderDetails(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	void RenderComponentProperties(AActor* Actor, const TArray<AActor*>& SelectedActors);
	void RenderActorProperties(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors);
	bool RenderPropertyWidget(TArray<struct FPropertyValue>& Props, int32& Index, bool bDispatchChange = true, const FString& PropertyPath = {});
	bool RenderSoftObjectPropertyWidget(struct FPropertyValue& Prop);
	bool RenderAttachSocketNameProperty(struct FPropertyValue& Prop);
	bool RenderBoneNameProperty(struct FPropertyValue& Prop);
	bool RenderComponentNameProperty(struct FPropertyValue& Prop);
	bool RenderEnumPropertyWidget(struct FPropertyValue& Prop);
	bool RenderStructPropertyWidget(struct FPropertyValue& Prop, bool bDispatchChange, const FString& PropertyPath);
	bool RenderArrayPropertyWidget(struct FPropertyValue& Prop, bool bDispatchChange, const FString& PropertyPath);
	bool RenderVehicleWheelSetupTools(struct FPropertyValue& Prop, bool bDispatchChange, const FString& PropertyPath);

	void PropagatePropertyChange(const FString& PropName, const TArray<AActor*>& SelectedActors);

	void AddComponentToActor(AActor* Actor, UClass* ComponentClass);
	bool CanRemoveSelectedComponent(AActor* Actor) const;
	bool RemoveSelectedComponent(AActor* Actor);

	static FString OpenObjFileDialog();
	static FString OpenStaticMeshFileDialog();
	static FString OpenFbxFileDialog();

	TWeakObjectPtr<UActorComponent> SelectedComponent = nullptr;
	TWeakObjectPtr<AActor> LastSelectedActor = nullptr;
	bool bActorSelected = true; // true: Actor details, false: Component details
	bool bShowEditorOnlyComponents = false;

	float PendingDetailsScrollY = -1.0f;
	bool bRestoreDetailsScrollY = false;

	//Rename
	TWeakObjectPtr<AActor> RenameTargetActor = nullptr;
	TWeakObjectPtr<UActorComponent> RenameTargetComponent = nullptr;
	ERenameTarget RenameTarget = ERenameTarget::None;
	bool bRenamePopupRequested = false;
	bool bFocusRenameInputNextFrame = false;

	char RenameBuffer[256] = {};
	bool bShowDuplicateWarning = false;
	FString PendingStaticMeshImportPath;
	FString* PendingStaticMeshImportTarget = nullptr;
	int32 PendingStaticFbxSkinnedMeshPolicy = 0;

	FFbxSceneImportDialogState SkeletalFbxImportDialog;
};
