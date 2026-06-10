#pragma once

#include "Object/Object.h"
#include "Core/Logging/Log.h"
#include "Object/GarbageCollection.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Source/Engine/UI/UserWidget.generated.h"
#include <sol/sol.hpp>
#include <utility>

#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif
#include <RmlUi/Core.h>

class APlayerController;
class FWidgetClickEventListener;
class FWidgetEventListener;
namespace Rml { class ElementDocument; }

enum class EUIRenderLayout : uint8_t
{
	ScaledDesign = 0,
	ScreenHud = 1,
};

class FWidgetClickEventListener final : public Rml::EventListener
{
public:
	FWidgetClickEventListener(FString InElementId, sol::protected_function InCallback);
	FWidgetClickEventListener(FString InElementId, FString InTargetTag, FString InFunctionName, FString InEventName = "click");

	void ProcessEvent(Rml::Event& Event) override;
	void Execute();

	const FString& GetElementId() const { return ElementId; }
	const FString& GetEventName() const { return EventName; }

private:
	FString ElementId;
	FString EventName;
	sol::protected_function Callback;
	FString TargetTag;
	FString FunctionName;
};

class FWidgetEventListener final : public Rml::EventListener
{
public:
	FWidgetEventListener(FString InElementId, FString InEventName, sol::protected_function InCallback);

	void ProcessEvent(Rml::Event& Event) override;

	const FString& GetElementId() const { return ElementId; }
	const FString& GetEventName() const { return EventName; }

private:
	FString ElementId;
	FString EventName;
	sol::protected_function Callback;
};

UCLASS()
class UUserWidget : public UObject
{
public:
	GENERATED_BODY()
	UUserWidget() = default;
	~UUserWidget() override = default;
	void BeginDestroy() override;

	void Initialize(APlayerController* InOwningPlayer, const FString& InDocumentPath);
	UFUNCTION(Callable, Category="UI")
	void AddToViewport(int32 InZOrder = 0);
	UFUNCTION(Callable, Category="UI")
	void RemoveFromParent();
	void BindClick(const FString& ElementId, sol::protected_function Callback);
	void BindEvent(const FString& ElementId, const FString& EventName, sol::protected_function Callback);
	void RegisterEventListeners();
	void ClearEventListeners();
	void ReleasePendingBindings();
	bool NavigateSelection(int32 DirectionX, int32 DirectionY);
	bool ActivateNavigationSelection();
	bool ActivateCloseNavigationTarget();
	void ClearNavigationSelection();
	void ClearAllNavigationHighlightStates();
	void SetGamepadNavigationHighlightEnabled(bool bEnabled);
	UFUNCTION(Callable, Category="UI")
	void SetText(const FString& ElementId, const FString& Text);
	UFUNCTION(Callable, Category="UI")
	bool SetProperty(const FString& ElementId, const FString& PropertyName, const FString& Value);
	UFUNCTION(Callable, Category="UI")
	bool SetAttribute(const FString& ElementId, const FString& AttributeName, const FString& Value);

	UFUNCTION(Pure, Category="UI")
	APlayerController* GetOwningPlayer() const { return OwningPlayer; }
	UFUNCTION(Pure, Category="UI")
	const FString& GetDocumentPath() const { return DocumentPath; }
	UFUNCTION(Pure, Category="UI")
	int32 GetZOrder() const { return ZOrder; }
	UFUNCTION(Pure, Category="UI")
	bool IsInViewport() const { return bInViewport; }
	UFUNCTION(Pure, Category="UI")
	bool IsDocumentLoaded() const { return bDocumentLoaded; }
	EUIRenderLayout GetLayoutMode() const { return LayoutMode; }
	void SetLayoutMode(EUIRenderLayout InLayoutMode) { LayoutMode = InLayoutMode; }
	Rml::ElementDocument* GetDocument() const { return Document; }

	// 메뉴/대화창처럼 사용자가 클릭/포인팅을 해야 하는 widget 은 true 로 설정.
	// UUIManager 가 viewport 에 올라온 widget 중 하나라도 이 값이 true 면 GameViewportClient
	// 에 알려 시스템 커서를 보이고 raw mouse / clip 을 해제하도록 한다. HUD 처럼 비대화형
	// 오버레이는 false 유지.
	UFUNCTION(Callable, Category="UI")
	void SetWantsMouse(bool bInWantsMouse) { bWantsMouse = bInWantsMouse; }
	UFUNCTION(Pure, Category="UI")
	bool WantsMouse() const { return bWantsMouse; }

	UFUNCTION(Callable, Category="UI")
	void SetWantsKeyboard(bool bInWantsKeyboard) { bWantsKeyboard = bInWantsKeyboard; }
	UFUNCTION(Pure, Category="UI")
	bool WantsKeyboard() const { return bWantsKeyboard; }

	UFUNCTION(Callable, Category="UI")
	void SetWantsTextInput(bool bInWantsTextInput) { bWantsTextInput = bInWantsTextInput; }
	UFUNCTION(Pure, Category="UI")
	bool WantsTextInput() const { return bWantsTextInput; }

	UFUNCTION(Callable, Category="UI")
	void SetBlocksGameInput(bool bInBlocksGameInput) { bBlocksGameInput = bInBlocksGameInput; }
	UFUNCTION(Pure, Category="UI")
	bool BlocksGameInput() const { return bBlocksGameInput; }

	UFUNCTION(Callable, Category="UI")
	void SetBlocksGameKeyboard(bool bInBlocksGameKeyboard) { bBlocksGameKeyboard = bInBlocksGameKeyboard; }
	UFUNCTION(Pure, Category="UI")
	bool BlocksGameKeyboard() const { return bBlocksGameKeyboard; }

	UFUNCTION(Callable, Category="UI")
	void SetBlocksGameMouseLook(bool bInBlocksGameMouseLook) { bBlocksGameMouseLook = bInBlocksGameMouseLook; }
	UFUNCTION(Pure, Category="UI")
	bool BlocksGameMouseLook() const { return bBlocksGameMouseLook; }

	void MarkDocumentLoaded(Rml::ElementDocument* InDocument) { Document = InDocument; bDocumentLoaded = Document != nullptr; }
	void MarkRemovedFromViewport() { bInViewport = false; }
	void ClearDocument() { Document = nullptr; bDocumentLoaded = false; }

private:
	void RegisterDeclarativeEventListeners(Rml::Element* Root);
	void RefreshNavigationButtons();
	void CollectNavigationButtons(Rml::Element* Root);
	void ApplyNavigationSelection();
	void RestoreNavigationButtonStyle(const FString& ElementId);
	bool ActivateNavigationElement(const FString& ElementId);
	int32 FindCloseNavigationButtonIndex() const;

private:
	struct FWidgetEventBinding
	{
		FString ElementId;
		FString EventName;
		sol::protected_function Callback;
	};

	struct FNavigationButton
	{
		FString ElementId;
		float CenterX = 0.0f;
		float CenterY = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
		bool bCloseTarget = false;
	};

	TWeakObjectPtr<APlayerController> OwningPlayer;
	Rml::ElementDocument* Document = nullptr;
	FString DocumentPath;
	TArray<std::pair<FString, sol::protected_function>> PendingClickBindings;
	TArray<FWidgetClickEventListener*> ClickListeners;
	TArray<FWidgetEventBinding> PendingEventBindings;
	TArray<FWidgetEventListener*> EventListeners;
	TArray<FNavigationButton> NavigationButtons;
	int32 ZOrder = 0;
	EUIRenderLayout LayoutMode = EUIRenderLayout::ScaledDesign;
	int32 NavigationSelectionIndex = -1;
	bool bGamepadNavigationHighlightEnabled = false;
	bool bInViewport = false;
	bool bDocumentLoaded = false;
	bool bWantsMouse = false;
	bool bWantsKeyboard = false;
	bool bWantsTextInput = false;
	bool bBlocksGameInput = false;
	bool bBlocksGameKeyboard = false;
	bool bBlocksGameMouseLook = false;
};
