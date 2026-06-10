#pragma once

#include "Core/Types/CoreTypes.h"
#include "Gizmo/GizmoTransformTarget.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Quat.h"

#include "Source/Editor/UI/Rml/RmlUiEditorManager.generated.h"

#include <filesystem>

class FLevelEditorViewportClient;
class FSelectionManager;
class UEditorEngine;
class UUserWidget;
class UWorld;

UENUM()
enum class ERmlUiElementType : uint8
{
	Canvas,
	Panel,
	Button,
	Image,
	Text
};

class FEditorRmlUiManager;

UCLASS()
class URmlUiDocumentEditObject : public UObject
{
public:
	GENERATED_BODY()

	void SetOwnerManager(FEditorRmlUiManager* InManager) { OwnerManager = InManager; }
	void PostEditProperty(const char* PropertyName) override;

	UPROPERTY(Edit, Save, Category="RML Document", DisplayName="Document Path")
	FString DocumentPath;

	UPROPERTY(Edit, Save, Category="RML Document", DisplayName="Canvas Width", Min=1.0f, Max=8192.0f, Speed=1.0f)
	float CanvasWidth = 1920.0f;

	UPROPERTY(Edit, Save, Category="RML Document", DisplayName="Canvas Height", Min=1.0f, Max=8192.0f, Speed=1.0f)
	float CanvasHeight = 1080.0f;

	UPROPERTY(Edit, Save, Category="RML Document", DisplayName="Preview Scale", Min=0.1f, Max=4.0f, Speed=0.01f)
	float PreviewScale = 1.0f;

	UPROPERTY(Edit, Save, Category="RML Document", DisplayName="Selected Element")
	FString SelectedElementId;

private:
	FEditorRmlUiManager* OwnerManager = nullptr;
};

UCLASS()
class URmlUiElementEditObject : public UObject
{
public:
	GENERATED_BODY()

	void SetOwnerManager(FEditorRmlUiManager* InManager) { OwnerManager = InManager; }
	void PostEditProperty(const char* PropertyName) override;

	UPROPERTY(Edit, Save, Category="RML Element", DisplayName="Element Id")
	FString ElementId;

	UPROPERTY(Edit, Save, Category="Layout", DisplayName="X", Min=0.0f, Max=8192.0f, Speed=1.0f)
	float X = 100.0f;

	UPROPERTY(Edit, Save, Category="Layout", DisplayName="Y", Min=0.0f, Max=8192.0f, Speed=1.0f)
	float Y = 100.0f;

	UPROPERTY(Edit, Save, Category="Layout", DisplayName="Width", Min=1.0f, Max=8192.0f, Speed=1.0f)
	float Width = 160.0f;

	UPROPERTY(Edit, Save, Category="Layout", DisplayName="Height", Min=1.0f, Max=8192.0f, Speed=1.0f)
	float Height = 48.0f;

	UPROPERTY(Edit, Save, Category="Layout", DisplayName="Z Order", Min=-1000.0f, Max=1000.0f, Speed=1.0f)
	int32 ZOrder = 0;

	UPROPERTY(Edit, Save, Category="Layout", DisplayName="Visible")
	bool bVisible = true;

	UPROPERTY(Edit, Save, Category="Layout", DisplayName="Locked")
	bool bLocked = false;

	UPROPERTY(Edit, Save, Category="Style", DisplayName="Opacity", Min=0.0f, Max=1.0f, Speed=0.01f)
	float Opacity = 1.0f;

	UPROPERTY(Edit, Save, Category="Events", DisplayName="Target Tag")
	FString EventTargetTag;

	UPROPERTY(Edit, Save, Category="Events", DisplayName="On Click Function")
	FString OnClickFunctionName;

private:
	FEditorRmlUiManager* OwnerManager = nullptr;
};

UCLASS()
class URmlUiPanelEditObject : public URmlUiElementEditObject
{
public:
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Style", DisplayName="Background Color", Type=Color4)
	FVector4 BackgroundColor = FVector4(0.12f, 0.16f, 0.22f, 0.86f);
};

UCLASS()
class URmlUiTextEditObject : public URmlUiElementEditObject
{
public:
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Style", DisplayName="Text")
	FString Text;

	UPROPERTY(Edit, Save, Category="Style", DisplayName="Text Color", Type=Color4)
	FVector4 TextColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Style", DisplayName="Font Size", Min=1.0f, Max=256.0f, Speed=1.0f)
	float FontSize = 24.0f;

	UPROPERTY(Edit, Save, Category="Style", DisplayName="Font Family")
	FString FontFamily = "Maplestory";

	UPROPERTY(Edit, Save, Category="Style", DisplayName="Bold")
	bool bBold = false;
};

UCLASS()
class URmlUiButtonEditObject : public URmlUiTextEditObject
{
public:
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Style", DisplayName="Background Color", Type=Color4)
	FVector4 BackgroundColor = FVector4(0.10f, 0.28f, 0.55f, 0.92f);
};

UCLASS()
class URmlUiImageEditObject : public URmlUiElementEditObject
{
public:
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Style", DisplayName="Image Path")
	FString ImagePath;
};

struct FRmlUiElementData
{
	ERmlUiElementType Type = ERmlUiElementType::Button;
	FString ElementId;
	float X = 100.0f;
	float Y = 100.0f;
	float Width = 160.0f;
	float Height = 48.0f;
	int32 ZOrder = 0;
	bool bVisible = true;
	bool bLocked = false;
	float Opacity = 1.0f;
	FVector4 BackgroundColor = FVector4(0.12f, 0.16f, 0.22f, 0.86f);
	FString Text;
	FVector4 TextColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	float FontSize = 24.0f;
	FString FontFamily = "Maplestory";
	bool bBold = false;
	FString ImagePath;
	FString OnClickFunctionName;
	FString EventTargetTag;
};

class FRmlUiElementGizmoTarget final : public IGizmoTransformTarget
{
public:
	void SetManager(FEditorRmlUiManager* InManager) { Manager = InManager; }

	bool IsValid() const override;
	UWorld* GetWorld() const override;
	FVector GetWorldLocation() const override;
	FRotator GetWorldRotation() const override;
	FQuat GetWorldQuat() const override;
	FVector GetWorldScale() const override;
	void SetWorldLocation(const FVector& NewLocation) override;
	void SetWorldRotation(const FRotator& NewRotation) override;
	void SetWorldRotation(const FQuat& NewQuat) override;
	void SetWorldScale(const FVector& NewScale) override;
	void AddWorldOffset(const FVector& Delta) override;
	void AddWorldRotation(const FQuat& Delta, bool bWorldSpace) override;
	void AddScaleDelta(const FVector& Delta) override;

private:
	FEditorRmlUiManager* Manager = nullptr;
};

class FEditorRmlUiManager : public FGCObject
{
	friend class FRmlUiElementGizmoTarget;

public:
	void Initialize(UEditorEngine* InEditor, FSelectionManager* InSelectionManager);
	void Shutdown();

	void CreateNewDocument();
	void OpenDocument(const FString& InDocumentPath);
	void AddElement(ERmlUiElementType Type);
	void DeleteSelectedElement();
	void DuplicateSelectedElement();
	void RenderViewportOverlay(FLevelEditorViewportClient* ViewportClient);

	void OnDocumentObjectChanged(URmlUiDocumentEditObject* Object);
	void OnElementObjectChanged(URmlUiElementEditObject* Object);
	void MoveSelectedElementByPixels(float DeltaX, float DeltaY);
	void ResizeSelectedElement(float NewX, float NewY, float NewWidth, float NewHeight);

	bool HasActiveDocument() const { return !DocumentPath.empty(); }
	bool HasSelectedElement() const { return SelectedElementIndex >= 0 && SelectedElementIndex < static_cast<int32>(Elements.size()); }
	UWorld* GetWorld() const;
	FVector GetSelectedElementWorldLocation() const;
	FLevelEditorViewportClient* GetActiveViewportClient() const { return ActiveViewportClient; }

	const char* GetReferencerName() const override { return "FEditorRmlUiManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	FRmlUiElementData* GetSelectedElement();
	const FRmlUiElementData* GetSelectedElement() const;
	void EnsureEditObjects();
	void SelectDocument();
	void SelectElementByIndex(int32 Index);
	void ApplyElementToEditObject(const FRmlUiElementData& Element);
	void ApplyEditObjectToElement(FRmlUiElementData& Element) const;
	void ApplyDocumentObject();
	bool LoadDocumentFromFile();
	bool SaveDocumentToFile() const;
	void RefreshPreview();
	void ClosePreview();
	FString MakeUniqueElementId(ERmlUiElementType Type) const;
	std::filesystem::path MakeUniqueDocumentPath() const;
	void MarkChanged(bool bReloadPreview);
	void RenderElementSelectionRects(FLevelEditorViewportClient* ViewportClient);
	void RenderResizeHandles(FLevelEditorViewportClient* ViewportClient);
	void SyncGizmoTarget();
	FVector ScreenToWorld(float ScreenX, float ScreenY) const;
	FQuat GetCameraAlignedGizmoRotation() const;
	void WorldDeltaToPixelDelta(const FVector& Delta, float& OutDeltaX, float& OutDeltaY) const;
	int32 FindElementIndexById(const FString& ElementId) const;

private:
	UEditorEngine* Editor = nullptr;
	FSelectionManager* SelectionManager = nullptr;
	FLevelEditorViewportClient* ActiveViewportClient = nullptr;
	URmlUiDocumentEditObject* DocumentEditObject = nullptr;
	URmlUiElementEditObject* ElementEditObject = nullptr;
	URmlUiButtonEditObject* ButtonEditObject = nullptr;
	URmlUiImageEditObject* ImageEditObject = nullptr;
	URmlUiTextEditObject* TextEditObject = nullptr;
	URmlUiPanelEditObject* PanelEditObject = nullptr;
	UUserWidget* PreviewWidget = nullptr;
	FRmlUiElementGizmoTarget GizmoTarget;

	FString DocumentPath;
	float CanvasWidth = 1920.0f;
	float CanvasHeight = 1080.0f;
	float PreviewScale = 1.0f;
	TArray<FRmlUiElementData> Elements;
	int32 SelectedElementIndex = -1;
};
