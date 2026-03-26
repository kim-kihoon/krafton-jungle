#include "TextComponent.h"
#include "World.h"
#include "ImGui/imgui.h"
#include "Renderer/Geometry.h"

static char TextBuffer[256];

UTextComponent::UTextComponent()
{
	type = EPrimitiveType::Text;
	Name = FName("Text");
	SetText(FString("New Text"));

	VResource = new FGeometry();
}

UTextComponent::~UTextComponent()
{
	if (VResource != nullptr)
	{
		delete VResource;
		VResource = nullptr;
	}
}

void UTextComponent::UpdateRenderObjects()
{
	GetWorld().bTextLabelDirty = true;
}

void UTextComponent::DrawProperties()
{
	UPrimitiveComponent::DrawProperties();

	strncpy_s(TextBuffer, Text.c_str(), sizeof(TextBuffer));

	if (ImGui::InputText("Text", TextBuffer, sizeof(TextBuffer)))
	{
		SetText(TextBuffer);
	}
}

void UTextComponent::SetVertices(TArray<FVector>& vertices, TArray<uint32>& indices)
{
	for (size_t i = 0; i < vertices.size(); ++i)
	{
		vertices[i] = GetRelativeMatrix().Inverse().TransformPoint(vertices[i]);
	}

	VResource->Vertices = vertices;
	VResource->VertexCount = static_cast<UINT>(vertices.size());
	VResource->Indices = indices;
	VResource->IndexCount = static_cast<UINT>(indices.size());

	LocalBoundingBox = FBoundingBox::FromPoints(VResource->Vertices);
}

json::JSON UTextComponent::Serialize()
{
	json::JSON jsonObj = UPrimitiveComponent::Serialize();
	jsonObj["Text"] = Text;

	return jsonObj;
}

void UTextComponent::Deserialize(json::JSON jsonObj)
{
	UPrimitiveComponent::Deserialize(jsonObj);
	SetText(jsonObj["Text"].ToString());
}

REGISTER_CLASS(UTextComponent);