#include "UI/UserWidget.h"

#include "Object/Reflection/ObjectFactory.h"
#include "UI/UIManager.h"

#include <cfloat>
#include <cmath>

namespace
{
constexpr const char* UI_NAV_SELECTED_CLASS = "ui-nav-selected";

FString EscapeRmlText(const FString& Text)
{
	FString Escaped;
	Escaped.reserve(Text.size());
	for (const char Ch : Text)
	{
		switch (Ch)
		{
		case '&': Escaped += "&amp;"; break;
		case '<': Escaped += "&lt;"; break;
		case '>': Escaped += "&gt;"; break;
		case '"': Escaped += "&quot;"; break;
		case '\'': Escaped += "&#39;"; break;
		default: Escaped += Ch; break;
		}
	}
	return Escaped;
}

bool IsTruthyAttribute(const Rml::String& Value)
{
	return Value == "true" || Value == "1";
}

float SquaredDistance(float X, float Y)
{
	return X * X + Y * Y;
}
}

FWidgetClickEventListener::FWidgetClickEventListener(FString InElementId, sol::protected_function InCallback)
	: ElementId(std::move(InElementId))
	, EventName("click")
	, Callback(std::move(InCallback))
{
}

FWidgetClickEventListener::FWidgetClickEventListener(FString InElementId, FString InTargetTag, FString InFunctionName, FString InEventName)
	: ElementId(std::move(InElementId))
	, EventName(std::move(InEventName))
	, TargetTag(std::move(InTargetTag))
	, FunctionName(std::move(InFunctionName))
{
}

void FWidgetClickEventListener::ProcessEvent(Rml::Event& /*Event*/)
{
	Execute();
}

void FWidgetClickEventListener::Execute()
{
	if (Callback.valid())
	{
		FScopedGarbageCollectionBlocker GCBlocker;
		sol::protected_function_result Result = Callback();
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("[Lua] UI click callback error: %s", Err.what());
		}
		return;
	}

	if (!TargetTag.empty() && !FunctionName.empty())
	{
		UUIManager::Get().DispatchTaggedActorClick(TargetTag, FunctionName);
	}
}

FWidgetEventListener::FWidgetEventListener(FString InElementId, FString InEventName, sol::protected_function InCallback)
	: ElementId(std::move(InElementId))
	, EventName(std::move(InEventName))
	, Callback(std::move(InCallback))
{
}

void FWidgetEventListener::ProcessEvent(Rml::Event& Event)
{
	if (!Callback.valid())
	{
		return;
	}

	FScopedGarbageCollectionBlocker GCBlocker;
	sol::state_view Lua(Callback.lua_state());
	sol::table EventTable = Lua.create_table();

	EventTable["type"] = Event.GetType();
	EventTable["mouse_x"] = Event.GetParameter<float>("mouse_x", -1.0f);
	EventTable["mouse_y"] = Event.GetParameter<float>("mouse_y", -1.0f);

	Rml::Element* Element = Event.GetCurrentElement();
	if (!Element)
	{
		Element = Event.GetTargetElement();
	}

	if (Element)
	{
		EventTable["element_id"] = Element->GetId();
		EventTable["element_left"] = Element->GetAbsoluteLeft();
		EventTable["element_top"] = Element->GetAbsoluteTop();
		EventTable["element_width"] = Element->GetOffsetWidth();
		EventTable["element_height"] = Element->GetOffsetHeight();
	}

	if (Rml::Element* Target = Event.GetTargetElement())
	{
		EventTable["target_id"] = Target->GetId();
	}

	sol::protected_function_result Result = Callback(EventTable);
	if (!Result.valid())
	{
		sol::error Err = Result;
		UE_LOG("[Lua] UI event callback error: %s", Err.what());
	}
}

void UUserWidget::BeginDestroy()
{
	// Rml::ElementDocument and Rml event listeners are external runtime resources,
	// not UObject references. GC can destroy a widget without going through the
	// regular UIManager shutdown path, so detach listeners and release the document
	// handle before the UObject enters PendingKill/Garbage state.
	ClearEventListeners();
	ReleasePendingBindings();
	if (Document)
	{
		Document->Close();
		ClearDocument();
	}
	bInViewport = false;
	UObject::BeginDestroy();
}


void UUserWidget::Initialize(APlayerController* InOwningPlayer, const FString& InDocumentPath)
{
	OwningPlayer = InOwningPlayer;
	DocumentPath = InDocumentPath;
}

void UUserWidget::AddToViewport(int32 InZOrder)
{
	ZOrder = InZOrder;
	bInViewport = true;
	UUIManager::Get().AddToViewport(this, InZOrder);
}

void UUserWidget::RemoveFromParent()
{
	UUIManager::Get().RemoveFromViewport(this);
	bInViewport = false;
}

void UUserWidget::BindClick(const FString& ElementId, sol::protected_function Callback)
{
	PendingClickBindings.push_back({ ElementId, Callback });
	if (IsDocumentLoaded())
	{
		RegisterEventListeners();
	}
}

void UUserWidget::BindEvent(const FString& ElementId, const FString& EventName, sol::protected_function Callback)
{
	PendingEventBindings.push_back({ ElementId, EventName, Callback });
	if (IsDocumentLoaded())
	{
		RegisterEventListeners();
	}
}

void UUserWidget::RegisterEventListeners()
{
	if (!Document)
	{
		return;
	}

	ClearEventListeners();

	for (const auto& Binding : PendingClickBindings)
	{
		Rml::Element* Element = Document->GetElementById(Binding.first);
		if (!Element)
		{
			UE_LOG("[RmlUi] Click target not found: %s", Binding.first.c_str());
			continue;
		}

		auto* Listener = new FWidgetClickEventListener(Binding.first, Binding.second);
		Element->AddEventListener(Listener->GetEventName().c_str(), Listener);
		ClickListeners.push_back(Listener);
	}

	for (const FWidgetEventBinding& Binding : PendingEventBindings)
	{
		Rml::Element* Element = Document->GetElementById(Binding.ElementId);
		if (!Element)
		{
			UE_LOG("[RmlUi] Event target not found: %s", Binding.ElementId.c_str());
			continue;
		}

		auto* Listener = new FWidgetEventListener(Binding.ElementId, Binding.EventName, Binding.Callback);
		Element->AddEventListener(Binding.EventName.c_str(), Listener);
		EventListeners.push_back(Listener);
	}

	RegisterDeclarativeEventListeners(Document);
	RefreshNavigationButtons();
}

void UUserWidget::RegisterDeclarativeEventListeners(Rml::Element* Root)
{
	if (!Root)
	{
		return;
	}

	const Rml::String FunctionName = Root->GetAttribute<Rml::String>("data-on-click", "");
	const Rml::String TargetTag = Root->GetAttribute<Rml::String>("data-target-tag", "");
	const Rml::String ElementId = Root->GetId();
	if (!FunctionName.empty() && !TargetTag.empty() && !ElementId.empty())
	{
		auto* Listener = new FWidgetClickEventListener(ElementId, TargetTag, FunctionName, "mousedown");
		Root->AddEventListener(Listener->GetEventName().c_str(), Listener);
		ClickListeners.push_back(Listener);
	}

	const int ChildCount = Root->GetNumChildren();
	for (int ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		RegisterDeclarativeEventListeners(Root->GetChild(ChildIndex));
	}
}

void UUserWidget::ClearEventListeners()
{
	if (Document)
	{
		for (FWidgetEventListener* Listener : EventListeners)
		{
			if (!Listener)
			{
				continue;
			}

			Rml::Element* Element = Document->GetElementById(Listener->GetElementId());
			if (Element)
			{
				Element->RemoveEventListener(Listener->GetEventName().c_str(), Listener);
			}
		}

		for (FWidgetClickEventListener* Listener : ClickListeners)
		{
			if (!Listener)
			{
				continue;
			}

			Rml::Element* Element = Document->GetElementById(Listener->GetElementId());
			if (Element)
			{
				Element->RemoveEventListener(Listener->GetEventName().c_str(), Listener);
			}
		}
	}

	for (FWidgetEventListener* Listener : EventListeners)
	{
		delete Listener;
	}
	EventListeners.clear();

	for (FWidgetClickEventListener* Listener : ClickListeners)
	{
		delete Listener;
	}
	ClickListeners.clear();

	ClearNavigationSelection();
	NavigationButtons.clear();
}

void UUserWidget::ReleasePendingBindings()
{
	PendingClickBindings.clear();
	PendingEventBindings.clear();
}

bool UUserWidget::NavigateSelection(int32 DirectionX, int32 DirectionY)
{
	if (!Document || (DirectionX == 0 && DirectionY == 0))
	{
		return false;
	}

	RefreshNavigationButtons();
	if (NavigationButtons.empty())
	{
		return false;
	}

	if (NavigationSelectionIndex < 0 || NavigationSelectionIndex >= static_cast<int32>(NavigationButtons.size()))
	{
		NavigationSelectionIndex = 0;
		ApplyNavigationSelection();
		return true;
	}

	const FNavigationButton& Current = NavigationButtons[NavigationSelectionIndex];
	const float DirectionLength = std::sqrt(static_cast<float>(DirectionX * DirectionX + DirectionY * DirectionY));
	const float DirX = static_cast<float>(DirectionX) / DirectionLength;
	const float DirY = static_cast<float>(DirectionY) / DirectionLength;

	int32 BestIndex = -1;
	float BestScore = FLT_MAX;
	for (int32 Index = 0; Index < static_cast<int32>(NavigationButtons.size()); ++Index)
	{
		if (Index == NavigationSelectionIndex)
		{
			continue;
		}

		const FNavigationButton& Candidate = NavigationButtons[Index];
		const float DeltaX = Candidate.CenterX - Current.CenterX;
		const float DeltaY = Candidate.CenterY - Current.CenterY;
		const float Dot = DeltaX * DirX + DeltaY * DirY;
		if (Dot <= 0.0f)
		{
			continue;
		}

		const float DistanceSquared = SquaredDistance(DeltaX, DeltaY);
		const float PerpendicularSquared = (std::max)(0.0f, DistanceSquared - Dot * Dot);
		const float Score = PerpendicularSquared * 4.0f + Dot;
		if (Score < BestScore)
		{
			BestScore = Score;
			BestIndex = Index;
		}
	}

	if (BestIndex < 0)
	{
		return false;
	}

	NavigationSelectionIndex = BestIndex;
	ApplyNavigationSelection();
	return true;
}

bool UUserWidget::ActivateNavigationSelection()
{
	if (!Document)
	{
		return false;
	}

	RefreshNavigationButtons();
	if (NavigationButtons.empty())
	{
		return false;
	}

	if (NavigationSelectionIndex < 0 || NavigationSelectionIndex >= static_cast<int32>(NavigationButtons.size()))
	{
		NavigationSelectionIndex = 0;
		ApplyNavigationSelection();
	}

	return ActivateNavigationElement(NavigationButtons[NavigationSelectionIndex].ElementId);
}

bool UUserWidget::ActivateCloseNavigationTarget()
{
	if (!Document)
	{
		return false;
	}

	RefreshNavigationButtons();
	const int32 CloseIndex = FindCloseNavigationButtonIndex();
	if (CloseIndex < 0)
	{
		return false;
	}

	NavigationSelectionIndex = CloseIndex;
	ApplyNavigationSelection();
	return ActivateNavigationElement(NavigationButtons[CloseIndex].ElementId);
}

void UUserWidget::ClearNavigationSelection()
{
	if (NavigationSelectionIndex >= 0 && NavigationSelectionIndex < static_cast<int32>(NavigationButtons.size()))
	{
		RestoreNavigationButtonStyle(NavigationButtons[NavigationSelectionIndex].ElementId);
	}
	NavigationSelectionIndex = -1;
}

void UUserWidget::ClearAllNavigationHighlightStates()
{
	if (!Document)
	{
		NavigationSelectionIndex = -1;
		return;
	}

	RefreshNavigationButtons();
	for (const FNavigationButton& Button : NavigationButtons)
	{
		RestoreNavigationButtonStyle(Button.ElementId);
	}
	NavigationSelectionIndex = -1;
}

void UUserWidget::RefreshNavigationButtons()
{
	if (!Document)
	{
		ClearNavigationSelection();
		NavigationButtons.clear();
		return;
	}

	const FString PreviousSelection = NavigationSelectionIndex >= 0 && NavigationSelectionIndex < static_cast<int32>(NavigationButtons.size())
		? NavigationButtons[NavigationSelectionIndex].ElementId
		: FString();

	if (!PreviousSelection.empty())
	{
		RestoreNavigationButtonStyle(PreviousSelection);
	}

	NavigationButtons.clear();
	CollectNavigationButtons(Document);

	NavigationSelectionIndex = -1;
	for (int32 Index = 0; Index < static_cast<int32>(NavigationButtons.size()); ++Index)
	{
		if (NavigationButtons[Index].ElementId == PreviousSelection)
		{
			NavigationSelectionIndex = Index;
			break;
		}
	}

}

void UUserWidget::SetGamepadNavigationHighlightEnabled(bool bEnabled)
{
	if (bGamepadNavigationHighlightEnabled == bEnabled)
	{
		return;
	}

	bGamepadNavigationHighlightEnabled = bEnabled;
	if (!bGamepadNavigationHighlightEnabled)
	{
		for (const FNavigationButton& Button : NavigationButtons)
		{
			RestoreNavigationButtonStyle(Button.ElementId);
		}
	}
	else if (NavigationSelectionIndex >= 0)
	{
		ApplyNavigationSelection();
	}
}

void UUserWidget::CollectNavigationButtons(Rml::Element* Root)
{
	if (!Root)
	{
		return;
	}

	const Rml::String ElementId = Root->GetId();
	const Rml::String ElementType = Root->GetAttribute<Rml::String>("data-type", "");
	const bool bVisible = Root->GetAttribute<Rml::String>("data-visible", "true") != "false";
	const bool bLocked = IsTruthyAttribute(Root->GetAttribute<Rml::String>("data-locked", "false"));
	if (!ElementId.empty() && ElementType == "Button" && bVisible && !bLocked)
	{
		FNavigationButton Button;
		Button.ElementId = ElementId;
		Button.CenterX = Root->GetAbsoluteLeft() + Root->GetOffsetWidth() * 0.5f;
		Button.CenterY = Root->GetAbsoluteTop() + Root->GetOffsetHeight() * 0.5f;
		Button.Width = Root->GetOffsetWidth();
		Button.Height = Root->GetOffsetHeight();
		Button.bCloseTarget = Root->GetAttribute<Rml::String>("data-on-click", "") == "ClosePopup";
		NavigationButtons.push_back(Button);
	}

	const int ChildCount = Root->GetNumChildren();
	for (int ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		CollectNavigationButtons(Root->GetChild(ChildIndex));
	}
}

void UUserWidget::ApplyNavigationSelection()
{
	if (!Document)
	{
		return;
	}

	for (const FNavigationButton& Button : NavigationButtons)
	{
		RestoreNavigationButtonStyle(Button.ElementId);
	}

	if (!bGamepadNavigationHighlightEnabled ||
		NavigationSelectionIndex < 0 ||
		NavigationSelectionIndex >= static_cast<int32>(NavigationButtons.size()))
	{
		return;
	}

	const FNavigationButton& Button = NavigationButtons[NavigationSelectionIndex];
	Rml::Element* Element = Document->GetElementById(Button.ElementId.c_str());
	if (!Element)
	{
		return;
	}

	Element->SetClass(UI_NAV_SELECTED_CLASS, true);
}

void UUserWidget::RestoreNavigationButtonStyle(const FString& ElementId)
{
	if (!Document || ElementId.empty())
	{
		return;
	}

	Rml::Element* Element = Document->GetElementById(ElementId.c_str());
	if (!Element)
	{
		return;
	}

	Element->SetClass(UI_NAV_SELECTED_CLASS, false);
}

bool UUserWidget::ActivateNavigationElement(const FString& ElementId)
{
	if (ElementId.empty())
	{
		return false;
	}

	for (FWidgetClickEventListener* Listener : ClickListeners)
	{
		if (Listener && Listener->GetElementId() == ElementId)
		{
			Listener->Execute();
			return true;
		}
	}

	return false;
}

int32 UUserWidget::FindCloseNavigationButtonIndex() const
{
	for (int32 Index = 0; Index < static_cast<int32>(NavigationButtons.size()); ++Index)
	{
		if (NavigationButtons[Index].bCloseTarget)
		{
			return Index;
		}
	}
	return -1;
}

void UUserWidget::SetText(const FString& ElementId, const FString& Text)
{
	if (!Document)
	{
		return;
	}

	Rml::Element* Element = Document->GetElementById(ElementId);
	if (!Element)
	{
		UE_LOG("[RmlUi] Text target not found: %s", ElementId.c_str());
		return;
	}

	const FString EscapedText = EscapeRmlText(Text);
	Element->SetInnerRML(EscapedText.c_str());
}

bool UUserWidget::SetProperty(const FString& ElementId, const FString& PropertyName, const FString& Value)
{
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(ElementId);
	if (!Element)
	{
		UE_LOG("[RmlUi] Property target not found: %s", ElementId.c_str());
		return false;
	}

	return Element->SetProperty(PropertyName.c_str(), Value.c_str());
}

bool UUserWidget::SetAttribute(const FString& ElementId, const FString& AttributeName, const FString& Value)
{
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(ElementId);
	if (!Element)
	{
		UE_LOG("[RmlUi] Attribute target not found: %s", ElementId.c_str());
		return false;
	}

	Element->SetAttribute(AttributeName.c_str(), Value.c_str());
	return true;
}
