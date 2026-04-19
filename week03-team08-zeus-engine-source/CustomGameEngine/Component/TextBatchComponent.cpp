#include "TextBatchComponent.h"
#include "TextComponent.h"
#include "Component/CameraComponent.h"
#include "Renderer/TextRenderObject.h"
#include "Renderer/Renderer.h"
#include "Renderer/Material.h"
#include "ResourceManager.h"
#include "World.h"

REGISTER_CLASS(UTextBatch)

UTextBatch::UTextBatch()
{
    type = EPrimitiveType::Text;
    Name = FName("TextBatch");
}

UTextBatch::~UTextBatch() {}

void UTextBatch::Update(float deltaTime)
{
    const bool bShowUUID = URenderer::GetRenderState().ShowFlags & static_cast<uint32>(EShowFlag::UUID);
    const bool bShowText = URenderer::GetRenderState().ShowFlags & static_cast<uint32>(EShowFlag::Text);

    if (!bShowUUID && !bShowText)   
    {
        if (bWasShowingUUID)
        {
            Clear();
            bWasShowingUUID = false;
			GetWorld().bTextLabelDirty = true;
        }
        MarkRenderStateDirty();
        return;
	}

	const bool bShowFlagsChanged = (bPrevShowUUID != bShowUUID) || (bPrevShowText != bShowText);

    if (!GetWorld().bTextLabelDirty && !bShowFlagsChanged && bWasShowingUUID)
    {
        MarkRenderStateDirty();
		bPrevShowUUID = bShowUUID;
		bPrevShowText = bShowText;

        for (auto& [font, group] : FontMap)
        {
            for (FTextEntry& Entry : group.Entries)
            {
                if (Entry.bUUID)
                    Entry.bVisible = bShowUUID;
                else
                    Entry.bVisible = bShowText;
			}
		}

        return;
    }

    GetWorld().bTextLabelDirty = false;
    Clear();

    if (UScene* activeScene = GetWorld().GetActiveScene())
    {
        for (USceneComponent* s : activeScene->SceneComponents)
        {
            if (!s) continue;

            UTextComponent* text = Cast<UTextComponent>(s);
            if (text && text->bIsVisible)
            {
                Submit(L"Asset/Font/Pretendard-Medium.ttf",
                    text->GetText(), FColor::White(), 0.8f,
                    text->GetRelativeLocation(),
                    text->GetRelativeRotation(),
                    text->GetRelativeScale3D(),
                    false, text);
            }

			UPrimitiveComponent* primitive = Cast<UPrimitiveComponent>(s);
			if (!primitive || !primitive->bIsVisible) continue;

			FVector position = primitive->GetRelativeLocation();
			position.z = primitive->ComputeWorldBoundingBox().MaxZ;
            Submit(L"Asset/Font/ChosunCentennial_ttf.png",
                "UUID: " + std::to_string(primitive->UUID), FColor::White(), 0.8f,
                position + FVector(0, 0, 1.5f),
                FRotator(), FVector::One(),
                true);
        }
	}

    bWasShowingUUID = true;
    bPrevShowUUID = bShowUUID;
    bPrevShowText = bShowText;
    MarkRenderStateDirty();
}

void UTextBatch::Submit(const FWString& Font, const FString& Text, const FColor& Color, float Height, FVector Position, FRotator Rotation, FVector Scale, bool bUUID, UTextComponent* comp)
{
    const bool bShowUUID = URenderer::GetRenderState().ShowFlags & static_cast<uint32>(EShowFlag::UUID);
    const bool bShowText = URenderer::GetRenderState().ShowFlags & static_cast<uint32>(EShowFlag::Text);

	TArray<FTextEntry>& Entries = FontMap[Font].Entries;

    FTextEntry entry;
    entry.Text = Text;
    entry.Position = Position;
    entry.Scale = Scale;
    entry.Rotation = Rotation;
    entry.Color = Color;
    entry.Height = Height;
	entry.bUUID = bUUID;
    
    if (comp)
    {
        entry.TextComponent = comp;
    }

    if (entry.bUUID)
        entry.bVisible = bShowUUID;
    else
		entry.bVisible = bShowText;
    
    Entries.push_back(entry);
    
    FontMap[Font].bIsDirty = true;
}

void UTextBatch::Clear()
{
    if (FontMap.empty())
        return;

    for (auto& [font, group] : FontMap)
    {
        group.Entries.clear();
    }
    bEntriesDirty = true;
}

void UTextBatch::CreateRenderObjects()
{
    ResourceManager* RM = ResourceManager::GetInstance();

    TextRenderObject* dynamicTextRenderObj = new TextRenderObject();
	dynamicTextRenderObj->Geometry = &RM->GetGeometry("TextBatch");
	dynamicTextRenderObj->Material = &RM->GetMaterial(L"Asset/Shader/ShaderFontFixed.hlsl");
	dynamicTextRenderObj->FontAtlas = &RM->GetDynamicFontAtlas(L"Asset/Font/Pretendard-Medium.ttf");
    dynamicTextRenderObj->ShowFlag = EShowFlag::All;
	dynamicTextRenderObj->PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    dynamicTextRenderObj->bDepthEnabled = true;
	dynamicTextRenderObj->bBackfaceCulling = false;
    dynamicTextRenderObj->Color = FColor::White();

	TextRenderObject* staticTextRenderObj = new TextRenderObject();
	staticTextRenderObj->Geometry = &RM->GetGeometry("TextBatch");
	staticTextRenderObj->Material = &RM->GetMaterial(L"Asset/Shader/ShaderFont.hlsl");
	staticTextRenderObj->FontAtlas = &RM->GetStaticFontAtlas(L"Asset/Font/ChosunCentennial_ttf.png");
	staticTextRenderObj->ShowFlag = EShowFlag::All;
	staticTextRenderObj->PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	staticTextRenderObj->bDepthEnabled = false;
	staticTextRenderObj->Color = FColor::White();

    RenderObjs.push_back(dynamicTextRenderObj);
	RenderObjs.push_back(staticTextRenderObj);

	FFontGroup& dynamicFontGroup = FontMap[L"Asset/Font/Pretendard-Medium.ttf"];
	FFontGroup& staticFontGroup = FontMap[L"Asset/Font/ChosunCentennial_ttf.png"];
	dynamicFontGroup.RenderObject = dynamicTextRenderObj;
	staticFontGroup.RenderObject = staticTextRenderObj;
}

void UTextBatch::UpdateRenderObjects()
{
    if (FontMap.empty()) return;

    for (auto& [font, group] : FontMap)
    {
        if (group.RenderObject == nullptr)
            continue;

        for (FTextEntry& Entry : group.Entries)
        {
            group.RenderObject->FontAtlas->EnsureTextUTF8(Entry.Text);
		}

        group.RenderObject->BuildFromEntries(group.Entries);
    }
}
