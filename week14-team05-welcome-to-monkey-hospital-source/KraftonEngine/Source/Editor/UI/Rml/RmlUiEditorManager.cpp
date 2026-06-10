#include "Editor/UI/Rml/RmlUiEditorManager.h"

#include "Component/Debug/GizmoComponent.h"
#include "Editor/EditorEngine.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/UI/ContentBrowser/ContentItem.h"
#include "Editor/Viewport/Level/LevelEditorViewportClient.h"
#include "Engine/Platform/Paths.h"
#include "GameFramework/World.h"
#include "Math/MathUtils.h"
#include "Math/Matrix.h"
#include "Object/Reflection/ObjectFactory.h"
#include "UI/UIManager.h"
#include "UI/UserWidget.h"
#include "Viewport/Viewport.h"

#include "ImGui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace
{
	constexpr float DefaultCanvasWidth = 1920.0f;
	constexpr float DefaultCanvasHeight = 1080.0f;
	constexpr float GizmoPlaneDistance = 10.0f;

	const char* TypeToString(ERmlUiElementType Type)
	{
		switch (Type)
		{
		case ERmlUiElementType::Panel: return "Panel";
		case ERmlUiElementType::Button: return "Button";
		case ERmlUiElementType::Image: return "Image";
		case ERmlUiElementType::Text: return "Text";
		default: return "Canvas";
		}
	}

	ERmlUiElementType TypeFromString(const FString& Type)
	{
		if (Type == "Panel") return ERmlUiElementType::Panel;
		if (Type == "Image") return ERmlUiElementType::Image;
		if (Type == "Text") return ERmlUiElementType::Text;
		return ERmlUiElementType::Button;
	}

	const char* TypePrefix(ERmlUiElementType Type)
	{
		switch (Type)
		{
		case ERmlUiElementType::Panel: return "panel";
		case ERmlUiElementType::Image: return "image";
		case ERmlUiElementType::Text: return "text";
		case ERmlUiElementType::Button: return "button";
		default: return "element";
		}
	}

	FString EscapeAttribute(const FString& Value)
	{
		FString Out;
		Out.reserve(Value.size());
		for (char Ch : Value)
		{
			switch (Ch)
			{
			case '&': Out += "&amp;"; break;
			case '"': Out += "&quot;"; break;
			case '<': Out += "&lt;"; break;
			case '>': Out += "&gt;"; break;
			default: Out.push_back(Ch); break;
			}
		}
		return Out;
	}

	FString UnescapeAttribute(FString Value)
	{
		auto ReplaceAll = [](FString& Target, const FString& From, const FString& To)
		{
			size_t Pos = 0;
			while ((Pos = Target.find(From, Pos)) != FString::npos)
			{
				Target.replace(Pos, From.size(), To);
				Pos += To.size();
			}
		};
		ReplaceAll(Value, "&quot;", "\"");
		ReplaceAll(Value, "&lt;", "<");
		ReplaceAll(Value, "&gt;", ">");
		ReplaceAll(Value, "&amp;", "&");
		return Value;
	}

	FString Attr(const FString& Line, const char* Name, const FString& DefaultValue = FString())
	{
		const std::regex Pattern(FString(Name) + "=\"([^\"]*)\"");
		std::smatch Match;
		if (std::regex_search(Line, Match, Pattern) && Match.size() > 1)
		{
			return UnescapeAttribute(Match[1].str());
		}
		return DefaultValue;
	}

	float AttrFloat(const FString& Line, const char* Name, float DefaultValue)
	{
		const FString Text = Attr(Line, Name);
		if (Text.empty())
		{
			return DefaultValue;
		}
		try
		{
			return std::stof(Text);
		}
		catch (...)
		{
			return DefaultValue;
		}
	}

	int32 AttrInt(const FString& Line, const char* Name, int32 DefaultValue)
	{
		const FString Text = Attr(Line, Name);
		if (Text.empty())
		{
			return DefaultValue;
		}
		try
		{
			return static_cast<int32>(std::stoi(Text));
		}
		catch (...)
		{
			return DefaultValue;
		}
	}

	bool AttrBool(const FString& Line, const char* Name, bool DefaultValue)
	{
		const FString Text = Attr(Line, Name);
		if (Text.empty())
		{
			return DefaultValue;
		}
		return Text == "true" || Text == "1";
	}

	FVector4 AttrColor(const FString& Line, const char* Name, const FVector4& DefaultValue)
	{
		const FString Text = Attr(Line, Name);
		if (Text.empty())
		{
			return DefaultValue;
		}

		FVector4 Result = DefaultValue;
		std::stringstream Stream(Text);
		char Comma = 0;
		if (Stream >> Result.X >> Comma >> Result.Y >> Comma >> Result.Z >> Comma >> Result.W)
		{
			return Result;
		}
		return DefaultValue;
	}

	FString ColorToAttr(const FVector4& Color)
	{
		std::ostringstream Out;
		Out << std::fixed << std::setprecision(3)
			<< Color.X << "," << Color.Y << "," << Color.Z << "," << Color.W;
		return Out.str();
	}

	FString ColorToCss(const FVector4& Color)
	{
		const int R = static_cast<int>(std::round(Clamp(Color.X, 0.0f, 1.0f) * 255.0f));
		const int G = static_cast<int>(std::round(Clamp(Color.Y, 0.0f, 1.0f) * 255.0f));
		const int B = static_cast<int>(std::round(Clamp(Color.Z, 0.0f, 1.0f) * 255.0f));
		const int A = static_cast<int>(std::round(Clamp(Color.W, 0.0f, 1.0f) * 255.0f));
		std::ostringstream Out;
		Out << "rgba(" << R << "," << G << "," << B << "," << A << ")";
		return Out.str();
	}

	FString ProjectRelativePath(const std::filesystem::path& Path)
	{
		std::error_code Ec;
		const std::filesystem::path Root(FPaths::RootDir());
		const std::filesystem::path Rel = std::filesystem::relative(Path, Root, Ec);
		if (!Ec)
		{
			return FPaths::ToUtf8(Rel.generic_wstring());
		}
		return FPaths::ToUtf8(Path.generic_wstring());
	}

	std::filesystem::path ToProjectPath(const FString& Path)
	{
		std::filesystem::path Result(FPaths::ToWide(Path));
		if (Result.is_relative())
		{
			Result = std::filesystem::path(FPaths::RootDir()) / Result;
		}
		return Result.lexically_normal();
	}

	FString ReadFileToString(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		if (!In)
		{
			return FString();
		}
		std::ostringstream Buffer;
		Buffer << In.rdbuf();
		return Buffer.str();
	}

	bool WriteStringToFile(const std::filesystem::path& Path, const FString& Text)
	{
		std::filesystem::create_directories(Path.parent_path());
		std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
		if (!Out)
		{
			return false;
		}
		Out.write(Text.data(), static_cast<std::streamsize>(Text.size()));
		return true;
	}

	FRmlUiElementData MakeDefaultElement(ERmlUiElementType Type)
	{
		FRmlUiElementData Element;
		Element.Type = Type;
		Element.X = 160.0f;
		Element.Y = 120.0f;
		Element.ZOrder = 0;
		Element.Opacity = 1.0f;
		Element.bVisible = true;
		Element.bLocked = false;

		switch (Type)
		{
		case ERmlUiElementType::Panel:
			Element.Width = 320.0f;
			Element.Height = 180.0f;
			Element.Text = "";
			Element.BackgroundColor = FVector4(0.08f, 0.10f, 0.14f, 0.82f);
			break;
		case ERmlUiElementType::Image:
			Element.Width = 240.0f;
			Element.Height = 160.0f;
			Element.Text = "";
			Element.BackgroundColor = FVector4(0.16f, 0.18f, 0.22f, 0.85f);
			break;
		case ERmlUiElementType::Text:
			Element.Width = 220.0f;
			Element.Height = 42.0f;
			Element.Text = "Text";
			Element.BackgroundColor = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
			Element.FontSize = 28.0f;
			break;
		case ERmlUiElementType::Button:
		default:
			Element.Width = 180.0f;
			Element.Height = 52.0f;
			Element.Text = "Button";
			Element.BackgroundColor = FVector4(0.10f, 0.28f, 0.55f, 0.92f);
			break;
		}
		return Element;
	}

	FString BuildElementRml(const FRmlUiElementData& Element)
	{
		const FString Display = Element.bVisible ? "block" : "none";
		std::ostringstream Style;
		Style << "position:absolute;"
			<< "left:" << Element.X << "px;"
			<< "top:" << Element.Y << "px;"
			<< "width:" << Element.Width << "px;"
			<< "height:" << Element.Height << "px;"
			<< "z-index:" << Element.ZOrder << ";"
			<< "display:" << Display << ";"
			<< "opacity:" << Clamp(Element.Opacity, 0.0f, 1.0f) << ";"
			<< "background-color:" << ColorToCss(Element.BackgroundColor) << ";"
			<< "color:" << ColorToCss(Element.TextColor) << ";"
			<< "font-size:" << Element.FontSize << "px;"
			<< "font-family:" << (Element.FontFamily.empty() ? "Maplestory" : Element.FontFamily) << ";"
			<< "font-weight:" << (Element.bBold ? "bold" : "normal") << ";";

		const FString CommonAttrs =
			"id=\"" + EscapeAttribute(Element.ElementId) + "\" "
			"data-rml-editor=\"1\" "
			"data-type=\"" + TypeToString(Element.Type) + "\" "
			"data-x=\"" + std::to_string(Element.X) + "\" "
			"data-y=\"" + std::to_string(Element.Y) + "\" "
			"data-width=\"" + std::to_string(Element.Width) + "\" "
			"data-height=\"" + std::to_string(Element.Height) + "\" "
			"data-z=\"" + std::to_string(Element.ZOrder) + "\" "
			"data-visible=\"" + FString(Element.bVisible ? "true" : "false") + "\" "
			"data-locked=\"" + FString(Element.bLocked ? "true" : "false") + "\" "
			"data-opacity=\"" + std::to_string(Element.Opacity) + "\" "
			"data-bg=\"" + ColorToAttr(Element.BackgroundColor) + "\" "
			"data-text=\"" + EscapeAttribute(Element.Text) + "\" "
			"data-text-color=\"" + ColorToAttr(Element.TextColor) + "\" "
			"data-font-size=\"" + std::to_string(Element.FontSize) + "\" "
			"data-font-family=\"" + EscapeAttribute(Element.FontFamily) + "\" "
			"data-bold=\"" + FString(Element.bBold ? "true" : "false") + "\" "
			"data-image=\"" + EscapeAttribute(Element.ImagePath) + "\" "
			"data-target-tag=\"" + EscapeAttribute(Element.EventTargetTag) + "\" "
			"data-on-click=\"" + EscapeAttribute(Element.OnClickFunctionName) + "\" "
			"style=\"" + Style.str() + "\"";

		if (Element.Type == ERmlUiElementType::Button || Element.Type == ERmlUiElementType::Text)
		{
			return "\t\t<div " + CommonAttrs + ">" + EscapeAttribute(Element.Text) + "</div>\n";
		}
		if (Element.Type == ERmlUiElementType::Image)
		{
			if (!Element.ImagePath.empty())
			{
				return "\t\t<img " + CommonAttrs + " src=\"" + EscapeAttribute(Element.ImagePath) + "\" />\n";
			}
			return "\t\t<div " + CommonAttrs + "></div>\n";
		}
		return "\t\t<div " + CommonAttrs + "></div>\n";
	}

	FRmlUiElementData ParseElementLine(const FString& Line)
	{
		FRmlUiElementData Element = MakeDefaultElement(TypeFromString(Attr(Line, "data-type", "Button")));
		Element.ElementId = Attr(Line, "id");
		Element.X = AttrFloat(Line, "data-x", Element.X);
		Element.Y = AttrFloat(Line, "data-y", Element.Y);
		Element.Width = AttrFloat(Line, "data-width", Element.Width);
		Element.Height = AttrFloat(Line, "data-height", Element.Height);
		Element.ZOrder = AttrInt(Line, "data-z", Element.ZOrder);
		Element.bVisible = AttrBool(Line, "data-visible", Element.bVisible);
		Element.bLocked = AttrBool(Line, "data-locked", Element.bLocked);
		Element.Opacity = AttrFloat(Line, "data-opacity", Element.Opacity);
		Element.BackgroundColor = AttrColor(Line, "data-bg", Element.BackgroundColor);
		Element.Text = Attr(Line, "data-text", Element.Text);
		Element.TextColor = AttrColor(Line, "data-text-color", Element.TextColor);
		Element.FontSize = AttrFloat(Line, "data-font-size", Element.FontSize);
		Element.FontFamily = Attr(Line, "data-font-family", Element.FontFamily);
		Element.bBold = AttrBool(Line, "data-bold", Element.bBold);
		Element.ImagePath = Attr(Line, "data-image", Element.ImagePath);
		Element.EventTargetTag = Attr(Line, "data-target-tag", Element.EventTargetTag);
		Element.OnClickFunctionName = Attr(Line, "data-on-click", Element.OnClickFunctionName);
		return Element;
	}
}

void URmlUiDocumentEditObject::PostEditProperty(const char* PropertyName)
{
	UObject::PostEditProperty(PropertyName);
	if (OwnerManager)
	{
		OwnerManager->OnDocumentObjectChanged(this);
	}
}

void URmlUiElementEditObject::PostEditProperty(const char* PropertyName)
{
	UObject::PostEditProperty(PropertyName);
	if (OwnerManager)
	{
		OwnerManager->OnElementObjectChanged(this);
	}
}

bool FRmlUiElementGizmoTarget::IsValid() const
{
	return Manager && Manager->HasSelectedElement();
}

UWorld* FRmlUiElementGizmoTarget::GetWorld() const
{
	return Manager ? Manager->GetWorld() : nullptr;
}

FVector FRmlUiElementGizmoTarget::GetWorldLocation() const
{
	return Manager ? Manager->GetSelectedElementWorldLocation() : FVector::ZeroVector;
}

FRotator FRmlUiElementGizmoTarget::GetWorldRotation() const
{
	return GetWorldQuat().ToRotator();
}

FQuat FRmlUiElementGizmoTarget::GetWorldQuat() const
{
	return Manager ? Manager->GetCameraAlignedGizmoRotation() : FQuat::Identity;
}

FVector FRmlUiElementGizmoTarget::GetWorldScale() const
{
	return FVector::OneVector;
}

void FRmlUiElementGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
	const FVector Delta = NewLocation - GetWorldLocation();
	AddWorldOffset(Delta);
}

void FRmlUiElementGizmoTarget::SetWorldRotation(const FRotator& /*NewRotation*/)
{
}

void FRmlUiElementGizmoTarget::SetWorldRotation(const FQuat& /*NewQuat*/)
{
}

void FRmlUiElementGizmoTarget::SetWorldScale(const FVector& /*NewScale*/)
{
}

void FRmlUiElementGizmoTarget::AddWorldOffset(const FVector& Delta)
{
	if (!Manager)
	{
		return;
	}

	float PixelX = 0.0f;
	float PixelY = 0.0f;
	Manager->WorldDeltaToPixelDelta(Delta, PixelX, PixelY);
	Manager->MoveSelectedElementByPixels(PixelX, PixelY);
}

void FRmlUiElementGizmoTarget::AddWorldRotation(const FQuat& /*Delta*/, bool /*bWorldSpace*/)
{
}

void FRmlUiElementGizmoTarget::AddScaleDelta(const FVector& /*Delta*/)
{
}

void FEditorRmlUiManager::Initialize(UEditorEngine* InEditor, FSelectionManager* InSelectionManager)
{
	Editor = InEditor;
	SelectionManager = InSelectionManager;
	GizmoTarget.SetManager(this);
	EnsureEditObjects();
}

void FEditorRmlUiManager::Shutdown()
{
	ClosePreview();
	Elements.clear();
	DocumentPath.clear();
	SelectedElementIndex = -1;

	if (DocumentEditObject)
	{
		UObjectManager::Get().DestroyObject(DocumentEditObject);
		DocumentEditObject = nullptr;
	}
	if (ElementEditObject)
	{
		ElementEditObject = nullptr;
	}
	if (ButtonEditObject)
	{
		UObjectManager::Get().DestroyObject(ButtonEditObject);
		ButtonEditObject = nullptr;
	}
	if (ImageEditObject)
	{
		UObjectManager::Get().DestroyObject(ImageEditObject);
		ImageEditObject = nullptr;
	}
	if (TextEditObject)
	{
		UObjectManager::Get().DestroyObject(TextEditObject);
		TextEditObject = nullptr;
	}
	if (PanelEditObject)
	{
		UObjectManager::Get().DestroyObject(PanelEditObject);
		PanelEditObject = nullptr;
	}

	Editor = nullptr;
	SelectionManager = nullptr;
	ActiveViewportClient = nullptr;
}

void FEditorRmlUiManager::CreateNewDocument()
{
	const std::filesystem::path Path = MakeUniqueDocumentPath();
	DocumentPath = ProjectRelativePath(Path);
	CanvasWidth = DefaultCanvasWidth;
	CanvasHeight = DefaultCanvasHeight;
	PreviewScale = 1.0f;
	Elements.clear();
	SelectedElementIndex = -1;
	SaveDocumentToFile();
	RefreshPreview();
	SelectDocument();
	if (Editor)
	{
		Editor->RefreshContentBrowser();
	}
}

void FEditorRmlUiManager::OpenDocument(const FString& InDocumentPath)
{
	DocumentPath = FPaths::MakeProjectRelative(InDocumentPath);
	if (DocumentPath.empty())
	{
		DocumentPath = ProjectRelativePath(ToProjectPath(InDocumentPath));
	}
	const bool bLoadedManagedDocument = LoadDocumentFromFile();
	if (bLoadedManagedDocument)
	{
		SaveDocumentToFile();
	}
	RefreshPreview();
	if (bLoadedManagedDocument && !Elements.empty())
	{
		SelectElementByIndex(0);
	}
	else
	{
		SelectDocument();
	}
}

void FEditorRmlUiManager::AddElement(ERmlUiElementType Type)
{
	if (DocumentPath.empty())
	{
		CreateNewDocument();
	}

	FRmlUiElementData Element = MakeDefaultElement(Type);
	Element.ElementId = MakeUniqueElementId(Type);
	Element.X += static_cast<float>(Elements.size()) * 20.0f;
	Element.Y += static_cast<float>(Elements.size()) * 20.0f;
	Elements.push_back(Element);
	SelectedElementIndex = static_cast<int32>(Elements.size()) - 1;
	MarkChanged(true);
	SelectElementByIndex(SelectedElementIndex);
}

void FEditorRmlUiManager::DeleteSelectedElement()
{
	if (!HasSelectedElement())
	{
		return;
	}

	Elements.erase(Elements.begin() + SelectedElementIndex);
	if (Elements.empty())
	{
		SelectedElementIndex = -1;
		MarkChanged(true);
		SelectDocument();
		return;
	}

	if (SelectedElementIndex >= static_cast<int32>(Elements.size()))
	{
		SelectedElementIndex = static_cast<int32>(Elements.size()) - 1;
	}
	MarkChanged(true);
	SelectElementByIndex(SelectedElementIndex);
}

void FEditorRmlUiManager::DuplicateSelectedElement()
{
	if (!HasSelectedElement())
	{
		return;
	}

	FRmlUiElementData Copy = Elements[SelectedElementIndex];
	Copy.ElementId = MakeUniqueElementId(Copy.Type);
	Copy.X += 20.0f;
	Copy.Y += 20.0f;
	Elements.push_back(Copy);
	SelectedElementIndex = static_cast<int32>(Elements.size()) - 1;
	MarkChanged(true);
	SelectElementByIndex(SelectedElementIndex);
}

void FEditorRmlUiManager::RenderViewportOverlay(FLevelEditorViewportClient* ViewportClient)
{
	if (!ViewportClient || !HasActiveDocument())
	{
		return;
	}

	ActiveViewportClient = ViewportClient;
	if (HasSelectedElement())
	{
		SyncGizmoTarget();
	}
	RenderElementSelectionRects(ViewportClient);
	RenderResizeHandles(ViewportClient);

	if (HasSelectedElement())
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			DeleteSelectedElement();
		}
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
		{
			DuplicateSelectedElement();
		}
	}
}

void FEditorRmlUiManager::OnDocumentObjectChanged(URmlUiDocumentEditObject* Object)
{
	if (!Object)
	{
		return;
	}

	CanvasWidth = std::max(1.0f, Object->CanvasWidth);
	CanvasHeight = std::max(1.0f, Object->CanvasHeight);
	PreviewScale = Clamp(Object->PreviewScale, 0.1f, 4.0f);
	MarkChanged(true);
	ApplyDocumentObject();
}

void FEditorRmlUiManager::OnElementObjectChanged(URmlUiElementEditObject* Object)
{
	if (!Object || !HasSelectedElement())
	{
		return;
	}

	FRmlUiElementData& Element = Elements[SelectedElementIndex];
	const FString OldId = Element.ElementId;
	const FString OldImagePath = Element.ImagePath;
	ApplyEditObjectToElement(Element);
	if (Element.ElementId.empty())
	{
		Element.ElementId = OldId;
	}
	for (int32 Index = 0; Index < static_cast<int32>(Elements.size()); ++Index)
	{
		if (Index != SelectedElementIndex && Elements[Index].ElementId == Element.ElementId)
		{
			Element.ElementId = OldId;
			break;
		}
	}

	const bool bNeedsPreviewReload = Element.ElementId != OldId || Element.ImagePath != OldImagePath;
	MarkChanged(bNeedsPreviewReload);
	ApplyElementToEditObject(Element);
}

void FEditorRmlUiManager::MoveSelectedElementByPixels(float DeltaX, float DeltaY)
{
	FRmlUiElementData* Element = GetSelectedElement();
	if (!Element || Element->bLocked)
	{
		return;
	}

	Element->X = std::max(0.0f, Element->X + DeltaX);
	Element->Y = std::max(0.0f, Element->Y + DeltaY);
	MarkChanged(false);
	ApplyElementToEditObject(*Element);
	SyncGizmoTarget();
}

void FEditorRmlUiManager::ResizeSelectedElement(float NewX, float NewY, float NewWidth, float NewHeight)
{
	FRmlUiElementData* Element = GetSelectedElement();
	if (!Element || Element->bLocked)
	{
		return;
	}

	Element->X = std::max(0.0f, NewX);
	Element->Y = std::max(0.0f, NewY);
	Element->Width = std::max(8.0f, NewWidth);
	Element->Height = std::max(8.0f, NewHeight);
	MarkChanged(false);
	ApplyElementToEditObject(*Element);
	SyncGizmoTarget();
}

UWorld* FEditorRmlUiManager::GetWorld() const
{
	return Editor ? Editor->GetWorld() : nullptr;
}

FVector FEditorRmlUiManager::GetSelectedElementWorldLocation() const
{
	const FRmlUiElementData* Element = GetSelectedElement();
	if (!Element)
	{
		return FVector::ZeroVector;
	}

	return ScreenToWorld(Element->X + Element->Width * 0.5f, Element->Y + Element->Height * 0.5f);
}

void FEditorRmlUiManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DocumentEditObject);
	Collector.AddReferencedObject(ElementEditObject);
	Collector.AddReferencedObject(ButtonEditObject);
	Collector.AddReferencedObject(ImageEditObject);
	Collector.AddReferencedObject(TextEditObject);
	Collector.AddReferencedObject(PanelEditObject);
	Collector.AddReferencedObject(PreviewWidget);
}

FRmlUiElementData* FEditorRmlUiManager::GetSelectedElement()
{
	return HasSelectedElement() ? &Elements[SelectedElementIndex] : nullptr;
}

const FRmlUiElementData* FEditorRmlUiManager::GetSelectedElement() const
{
	return HasSelectedElement() ? &Elements[SelectedElementIndex] : nullptr;
}

void FEditorRmlUiManager::EnsureEditObjects()
{
	if (!DocumentEditObject)
	{
		DocumentEditObject = UObjectManager::Get().CreateObject<URmlUiDocumentEditObject>();
		DocumentEditObject->SetOwnerManager(this);
	}
	if (!ButtonEditObject)
	{
		ButtonEditObject = UObjectManager::Get().CreateObject<URmlUiButtonEditObject>();
		ButtonEditObject->SetOwnerManager(this);
	}
	if (!ImageEditObject)
	{
		ImageEditObject = UObjectManager::Get().CreateObject<URmlUiImageEditObject>();
		ImageEditObject->SetOwnerManager(this);
	}
	if (!TextEditObject)
	{
		TextEditObject = UObjectManager::Get().CreateObject<URmlUiTextEditObject>();
		TextEditObject->SetOwnerManager(this);
	}
	if (!PanelEditObject)
	{
		PanelEditObject = UObjectManager::Get().CreateObject<URmlUiPanelEditObject>();
		PanelEditObject->SetOwnerManager(this);
	}
}

void FEditorRmlUiManager::SelectDocument()
{
	EnsureEditObjects();
	ApplyDocumentObject();
	if (SelectionManager)
	{
		SelectionManager->ClearSelection();
		SelectionManager->SetSingleDetailTarget(FSelectionDetailTarget::FromObject(DocumentEditObject));
		if (UGizmoComponent* Gizmo = SelectionManager->GetGizmo())
		{
			Gizmo->Deactivate();
		}
	}
}

void FEditorRmlUiManager::SelectElementByIndex(int32 Index)
{
	if (Index < 0 || Index >= static_cast<int32>(Elements.size()))
	{
		SelectedElementIndex = -1;
		SelectDocument();
		return;
	}

	SelectedElementIndex = Index;
	EnsureEditObjects();
	ApplyDocumentObject();
	switch (Elements[SelectedElementIndex].Type)
	{
	case ERmlUiElementType::Panel:
		ElementEditObject = PanelEditObject;
		break;
	case ERmlUiElementType::Image:
		ElementEditObject = ImageEditObject;
		break;
	case ERmlUiElementType::Text:
		ElementEditObject = TextEditObject;
		break;
	case ERmlUiElementType::Button:
	default:
		ElementEditObject = ButtonEditObject;
		break;
	}
	ApplyElementToEditObject(Elements[SelectedElementIndex]);
	if (SelectionManager)
	{
		SelectionManager->ClearSelection();
		SelectionManager->SetSingleDetailTarget(FSelectionDetailTarget::FromObject(ElementEditObject));
	}
	SyncGizmoTarget();
}

void FEditorRmlUiManager::ApplyElementToEditObject(const FRmlUiElementData& Element)
{
	EnsureEditObjects();
	ElementEditObject->ElementId = Element.ElementId;
	ElementEditObject->X = Element.X;
	ElementEditObject->Y = Element.Y;
	ElementEditObject->Width = Element.Width;
	ElementEditObject->Height = Element.Height;
	ElementEditObject->ZOrder = Element.ZOrder;
	ElementEditObject->bVisible = Element.bVisible;
	ElementEditObject->bLocked = Element.bLocked;
	ElementEditObject->Opacity = Element.Opacity;
	ElementEditObject->EventTargetTag = Element.EventTargetTag;
	ElementEditObject->OnClickFunctionName = Element.OnClickFunctionName;

	if (Element.Type == ERmlUiElementType::Button)
	{
		URmlUiButtonEditObject* ButtonObject = static_cast<URmlUiButtonEditObject*>(ElementEditObject);
		ButtonObject->BackgroundColor = Element.BackgroundColor;
		ButtonObject->Text = Element.Text;
		ButtonObject->TextColor = Element.TextColor;
		ButtonObject->FontSize = Element.FontSize;
		ButtonObject->FontFamily = Element.FontFamily;
		ButtonObject->bBold = Element.bBold;
	}
	else if (Element.Type == ERmlUiElementType::Text)
	{
		URmlUiTextEditObject* TextObject = static_cast<URmlUiTextEditObject*>(ElementEditObject);
		TextObject->Text = Element.Text;
		TextObject->TextColor = Element.TextColor;
		TextObject->FontSize = Element.FontSize;
		TextObject->FontFamily = Element.FontFamily;
		TextObject->bBold = Element.bBold;
	}
	else if (Element.Type == ERmlUiElementType::Image)
	{
		URmlUiImageEditObject* ImageObject = static_cast<URmlUiImageEditObject*>(ElementEditObject);
		ImageObject->ImagePath = Element.ImagePath;
	}
	else if (Element.Type == ERmlUiElementType::Panel)
	{
		URmlUiPanelEditObject* PanelObject = static_cast<URmlUiPanelEditObject*>(ElementEditObject);
		PanelObject->BackgroundColor = Element.BackgroundColor;
	}
}

void FEditorRmlUiManager::ApplyEditObjectToElement(FRmlUiElementData& Element) const
{
	Element.ElementId = ElementEditObject->ElementId;
	Element.X = std::max(0.0f, ElementEditObject->X);
	Element.Y = std::max(0.0f, ElementEditObject->Y);
	Element.Width = std::max(1.0f, ElementEditObject->Width);
	Element.Height = std::max(1.0f, ElementEditObject->Height);
	Element.ZOrder = ElementEditObject->ZOrder;
	Element.bVisible = ElementEditObject->bVisible;
	Element.bLocked = ElementEditObject->bLocked;
	Element.Opacity = Clamp(ElementEditObject->Opacity, 0.0f, 1.0f);
	Element.EventTargetTag = ElementEditObject->EventTargetTag;
	Element.OnClickFunctionName = ElementEditObject->OnClickFunctionName;

	if (Element.Type == ERmlUiElementType::Button)
	{
		const URmlUiButtonEditObject* ButtonObject = static_cast<const URmlUiButtonEditObject*>(ElementEditObject);
		Element.BackgroundColor = ButtonObject->BackgroundColor;
		Element.Text = ButtonObject->Text;
		Element.TextColor = ButtonObject->TextColor;
		Element.FontSize = std::max(1.0f, ButtonObject->FontSize);
		Element.FontFamily = ButtonObject->FontFamily.empty() ? FString("Maplestory") : ButtonObject->FontFamily;
		Element.bBold = ButtonObject->bBold;
	}
	else if (Element.Type == ERmlUiElementType::Text)
	{
		const URmlUiTextEditObject* TextObject = static_cast<const URmlUiTextEditObject*>(ElementEditObject);
		Element.Text = TextObject->Text;
		Element.TextColor = TextObject->TextColor;
		Element.FontSize = std::max(1.0f, TextObject->FontSize);
		Element.FontFamily = TextObject->FontFamily.empty() ? FString("Maplestory") : TextObject->FontFamily;
		Element.bBold = TextObject->bBold;
	}
	else if (Element.Type == ERmlUiElementType::Image)
	{
		const URmlUiImageEditObject* ImageObject = static_cast<const URmlUiImageEditObject*>(ElementEditObject);
		Element.ImagePath = ImageObject->ImagePath;
	}
	else if (Element.Type == ERmlUiElementType::Panel)
	{
		const URmlUiPanelEditObject* PanelObject = static_cast<const URmlUiPanelEditObject*>(ElementEditObject);
		Element.BackgroundColor = PanelObject->BackgroundColor;
	}
}

void FEditorRmlUiManager::ApplyDocumentObject()
{
	EnsureEditObjects();
	DocumentEditObject->DocumentPath = DocumentPath;
	DocumentEditObject->CanvasWidth = CanvasWidth;
	DocumentEditObject->CanvasHeight = CanvasHeight;
	DocumentEditObject->PreviewScale = PreviewScale;
	DocumentEditObject->SelectedElementId = HasSelectedElement() ? Elements[SelectedElementIndex].ElementId : FString();
}

bool FEditorRmlUiManager::LoadDocumentFromFile()
{
	Elements.clear();
	SelectedElementIndex = -1;
	CanvasWidth = DefaultCanvasWidth;
	CanvasHeight = DefaultCanvasHeight;
	PreviewScale = 1.0f;

	const FString Text = ReadFileToString(ToProjectPath(DocumentPath));
	if (Text.empty())
	{
		return false;
	}
	if (Text.find("data-rml-ui-document=\"1\"") == FString::npos)
	{
		return false;
	}

	CanvasWidth = AttrFloat(Text, "data-canvas-width", CanvasWidth);
	CanvasHeight = AttrFloat(Text, "data-canvas-height", CanvasHeight);
	PreviewScale = AttrFloat(Text, "data-preview-scale", PreviewScale);

	std::istringstream Lines(Text);
	FString Line;
	while (std::getline(Lines, Line))
	{
		if (Line.find("data-rml-editor=\"1\"") == FString::npos)
		{
			continue;
		}

		FRmlUiElementData Element = ParseElementLine(Line);
		if (!Element.ElementId.empty())
		{
			Elements.push_back(Element);
		}
	}

	return true;
}

bool FEditorRmlUiManager::SaveDocumentToFile() const
{
	if (DocumentPath.empty())
	{
		return false;
	}

	std::ostringstream Out;
	Out << "<rml>\n"
		<< "<head>\n"
		<< "\t<title>RML UI</title>\n"
		<< "\t<style>\n"
		<< "\t\tbody { margin: 0px; width: 100%; height: 100%; font-family: Maplestory; }\n"
		<< "\t\t#ui_canvas { position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; }\n"
		<< "\t</style>\n"
		<< "</head>\n"
		<< "<body data-rml-ui-document=\"1\" data-canvas-width=\"" << CanvasWidth
		<< "\" data-canvas-height=\"" << CanvasHeight
		<< "\" data-preview-scale=\"" << PreviewScale << "\">\n"
		<< "\t<div id=\"ui_canvas\">\n";

	for (const FRmlUiElementData& Element : Elements)
	{
		Out << BuildElementRml(Element);
	}

	Out << "\t</div>\n"
		<< "</body>\n"
		<< "</rml>\n";

	return WriteStringToFile(ToProjectPath(DocumentPath), Out.str());
}

void FEditorRmlUiManager::RefreshPreview()
{
	ClosePreview();
	if (!Editor || DocumentPath.empty())
	{
		return;
	}

	PreviewWidget = UUIManager::Get().CreateWidget(nullptr, DocumentPath);
	if (!PreviewWidget)
	{
		return;
	}

	PreviewWidget->SetWantsMouse(true);
	PreviewWidget->SetBlocksGameMouseLook(false);
	PreviewWidget->AddToViewport(100);
}

void FEditorRmlUiManager::ClosePreview()
{
	if (PreviewWidget && IsAliveObject(PreviewWidget))
	{
		PreviewWidget->RemoveFromParent();
	}
	PreviewWidget = nullptr;
}

FString FEditorRmlUiManager::MakeUniqueElementId(ERmlUiElementType Type) const
{
	const FString Prefix = TypePrefix(Type);
	for (int32 Index = 0; Index < 100000; ++Index)
	{
		FString Candidate = Prefix + FString("_") + std::to_string(Index);
		if (FindElementIndexById(Candidate) < 0)
		{
			return Candidate;
		}
	}
	return Prefix + FString("_new");
}

std::filesystem::path FEditorRmlUiManager::MakeUniqueDocumentPath() const
{
	const std::filesystem::path Dir = std::filesystem::path(FPaths::RootDir()) / L"Content" / L"UI";
	std::filesystem::create_directories(Dir);

	for (int32 Index = 0; Index < 100000; ++Index)
	{
		const std::wstring Stem = L"UI_" + std::to_wstring(Index);
		std::filesystem::path Candidate = Dir / (Stem + L".rml");
		if (!std::filesystem::exists(Candidate))
		{
			return Candidate;
		}
	}
	return Dir / L"UI_New.rml";
}

void FEditorRmlUiManager::MarkChanged(bool bReloadPreview)
{
	SaveDocumentToFile();
	if (bReloadPreview)
	{
		RefreshPreview();
	}
	else if (PreviewWidget && HasSelectedElement())
	{
		const FRmlUiElementData& Element = Elements[SelectedElementIndex];
		PreviewWidget->SetProperty(Element.ElementId, "left", std::to_string(Element.X) + "px");
		PreviewWidget->SetProperty(Element.ElementId, "top", std::to_string(Element.Y) + "px");
		PreviewWidget->SetProperty(Element.ElementId, "width", std::to_string(Element.Width) + "px");
		PreviewWidget->SetProperty(Element.ElementId, "height", std::to_string(Element.Height) + "px");
		PreviewWidget->SetProperty(Element.ElementId, "display", Element.bVisible ? "block" : "none");
		PreviewWidget->SetProperty(Element.ElementId, "opacity", std::to_string(Element.Opacity));
		PreviewWidget->SetProperty(Element.ElementId, "background-color", ColorToCss(Element.BackgroundColor));
		PreviewWidget->SetProperty(Element.ElementId, "color", ColorToCss(Element.TextColor));
		PreviewWidget->SetProperty(Element.ElementId, "font-size", std::to_string(Element.FontSize) + "px");
		PreviewWidget->SetProperty(Element.ElementId, "font-family", Element.FontFamily.empty() ? "Maplestory" : Element.FontFamily);
		PreviewWidget->SetProperty(Element.ElementId, "font-weight", Element.bBold ? "bold" : "normal");
		if (Element.Type == ERmlUiElementType::Button || Element.Type == ERmlUiElementType::Text)
		{
			PreviewWidget->SetText(Element.ElementId, Element.Text);
		}
	}
}

void FEditorRmlUiManager::RenderElementSelectionRects(FLevelEditorViewportClient* ViewportClient)
{
	if (!ViewportClient)
	{
		return;
	}

	const FRect& Rect = ViewportClient->GetViewportScreenRect();
	ImDrawList* DrawList = ImGui::GetForegroundDrawList();
	for (int32 Index = 0; Index < static_cast<int32>(Elements.size()); ++Index)
	{
		const FRmlUiElementData& Element = Elements[Index];
		if (!Element.bVisible)
		{
			continue;
		}

		const ImVec2 Min(Rect.X + Element.X, Rect.Y + Element.Y);
		const ImVec2 Max(Min.x + Element.Width, Min.y + Element.Height);
		const bool bSelected = Index == SelectedElementIndex;
		DrawList->AddRect(Min, Max, bSelected ? IM_COL32(255, 210, 80, 255) : IM_COL32(90, 170, 255, 160), 0.0f, 0, bSelected ? 2.0f : 1.0f);

		ImGui::SetCursorScreenPos(Min);
		ImGui::InvisibleButton((Element.ElementId + "##RmlElementPick").c_str(), ImVec2(Element.Width, Element.Height));
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			SelectElementByIndex(Index);
		}

		if (Element.Type == ERmlUiElementType::Image && ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PNGElement"))
			{
				if (Payload->Data && Payload->DataSize == sizeof(FContentItem))
				{
					const FContentItem* Item = static_cast<const FContentItem*>(Payload->Data);
					Elements[Index].ImagePath = ProjectRelativePath(Item->Path);
					SelectElementByIndex(Index);
					MarkChanged(true);
				}
			}
			ImGui::EndDragDropTarget();
		}
	}
}

void FEditorRmlUiManager::RenderResizeHandles(FLevelEditorViewportClient* ViewportClient)
{
	FRmlUiElementData* Element = GetSelectedElement();
	if (!ViewportClient || !Element || Element->bLocked || !Element->bVisible)
	{
		return;
	}

	const FRect& Rect = ViewportClient->GetViewportScreenRect();
	const float X = Rect.X + Element->X;
	const float Y = Rect.Y + Element->Y;
	const float W = Element->Width;
	const float H = Element->Height;
	const float Handle = 8.0f;

	struct FHandleDesc
	{
		const char* Id;
		float X;
		float Y;
		float DXSign;
		float DYSign;
		float DWSign;
		float DHSign;
	};

	const FHandleDesc Handles[] = {
		{ "NW", X, Y, 1.0f, 1.0f, -1.0f, -1.0f },
		{ "N", X + W * 0.5f, Y, 0.0f, 1.0f, 0.0f, -1.0f },
		{ "NE", X + W, Y, 0.0f, 1.0f, 1.0f, -1.0f },
		{ "E", X + W, Y + H * 0.5f, 0.0f, 0.0f, 1.0f, 0.0f },
		{ "SE", X + W, Y + H, 0.0f, 0.0f, 1.0f, 1.0f },
		{ "S", X + W * 0.5f, Y + H, 0.0f, 0.0f, 0.0f, 1.0f },
		{ "SW", X, Y + H, 1.0f, 0.0f, -1.0f, 1.0f },
		{ "W", X, Y + H * 0.5f, 1.0f, 0.0f, -1.0f, 0.0f },
	};

	ImDrawList* DrawList = ImGui::GetForegroundDrawList();
	for (const FHandleDesc& Desc : Handles)
	{
		const ImVec2 Min(Desc.X - Handle * 0.5f, Desc.Y - Handle * 0.5f);
		const ImVec2 Max(Desc.X + Handle * 0.5f, Desc.Y + Handle * 0.5f);
		DrawList->AddRectFilled(Min, Max, IM_COL32(255, 210, 80, 255));
		DrawList->AddRect(Min, Max, IM_COL32(30, 30, 30, 255));

		ImGui::SetCursorScreenPos(Min);
		ImGui::InvisibleButton((FString("##RmlResize") + Desc.Id).c_str(), ImVec2(Handle, Handle));
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const ImVec2 Delta = ImGui::GetIO().MouseDelta;
			const float NewX = Element->X + Delta.x * Desc.DXSign;
			const float NewY = Element->Y + Delta.y * Desc.DYSign;
			const float NewW = Element->Width + Delta.x * Desc.DWSign;
			const float NewH = Element->Height + Delta.y * Desc.DHSign;
			ResizeSelectedElement(NewX, NewY, NewW, NewH);
		}
	}
}

void FEditorRmlUiManager::SyncGizmoTarget()
{
	if (!SelectionManager)
	{
		return;
	}

	UGizmoComponent* Gizmo = SelectionManager->GetGizmo();
	if (!Gizmo)
	{
		return;
	}

	if (HasSelectedElement())
	{
		Gizmo->SetSelectedActors(nullptr);
		Gizmo->SetTarget(&GizmoTarget);
		Gizmo->SetWorldSpace(false);
		Gizmo->UpdateGizmoTransform();
	}
	else
	{
		Gizmo->Deactivate();
	}
}

FVector FEditorRmlUiManager::ScreenToWorld(float ScreenX, float ScreenY) const
{
	FLevelEditorViewportClient* ViewportClient = ActiveViewportClient ? ActiveViewportClient : (Editor ? Editor->GetActiveViewport() : nullptr);
	if (!ViewportClient || !ViewportClient->GetViewport())
	{
		return FVector::ZeroVector;
	}

	FMinimalViewInfo POV;
	ViewportClient->GetCameraView(POV);
	const float Width = static_cast<float>(ViewportClient->GetViewport()->GetWidth());
	const float Height = static_cast<float>(ViewportClient->GetViewport()->GetHeight());
	const FRay Ray = POV.DeprojectScreenToWorld(ScreenX, ScreenY, Width, Height);
	return Ray.Origin + Ray.Direction.Normalized() * GizmoPlaneDistance;
}

FQuat FEditorRmlUiManager::GetCameraAlignedGizmoRotation() const
{
	FLevelEditorViewportClient* ViewportClient = ActiveViewportClient ? ActiveViewportClient : (Editor ? Editor->GetActiveViewport() : nullptr);
	if (!ViewportClient)
	{
		return FQuat::Identity;
	}

	FMinimalViewInfo POV;
	ViewportClient->GetCameraView(POV);
	FVector ScreenRight = POV.Rotation.GetRightVector();
	FVector ScreenUp = POV.Rotation.GetUpVector();
	if (ScreenRight.IsNearlyZero() || ScreenUp.IsNearlyZero())
	{
		return FQuat::Identity;
	}
	ScreenRight.Normalize();
	ScreenUp.Normalize();

	FVector ScreenForward = ScreenRight.Cross(ScreenUp);
	if (ScreenForward.IsNearlyZero())
	{
		return FQuat::Identity;
	}
	ScreenForward.Normalize();

	const FMatrix RotationMatrix(
		ScreenRight.X, ScreenRight.Y, ScreenRight.Z, 0.0f,
		ScreenUp.X, ScreenUp.Y, ScreenUp.Z, 0.0f,
		ScreenForward.X, ScreenForward.Y, ScreenForward.Z, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
	return FQuat::FromMatrix(RotationMatrix);
}

void FEditorRmlUiManager::WorldDeltaToPixelDelta(const FVector& Delta, float& OutDeltaX, float& OutDeltaY) const
{
	OutDeltaX = 0.0f;
	OutDeltaY = 0.0f;

	FLevelEditorViewportClient* ViewportClient = ActiveViewportClient ? ActiveViewportClient : (Editor ? Editor->GetActiveViewport() : nullptr);
	if (!ViewportClient || !ViewportClient->GetViewport())
	{
		return;
	}

	FMinimalViewInfo POV;
	ViewportClient->GetCameraView(POV);
	const FVector Right = POV.Rotation.GetRightVector();
	const FVector Up = POV.Rotation.GetUpVector();
	const float Width = static_cast<float>(ViewportClient->GetViewport()->GetWidth());
	const float Height = static_cast<float>(ViewportClient->GetViewport()->GetHeight());
	const float PixelsPerWorld = std::max(20.0f, std::min(Width, Height) / 20.0f);
	OutDeltaX = Delta.Dot(Right) * PixelsPerWorld;
	OutDeltaY = -Delta.Dot(Up) * PixelsPerWorld;
}

int32 FEditorRmlUiManager::FindElementIndexById(const FString& ElementId) const
{
	for (int32 Index = 0; Index < static_cast<int32>(Elements.size()); ++Index)
	{
		if (Elements[Index].ElementId == ElementId)
		{
			return Index;
		}
	}
	return -1;
}
