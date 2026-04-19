#pragma once

#include "EngineTypes.h"
#include "PrimitiveComponent.h"
#include "TextTypes.h"

struct TextRenderObject;

struct FFontGroup
{
    TextRenderObject* RenderObject;
    TArray<FTextEntry> Entries;
    bool bIsDirty = false;
};

class UTextBatch : public UPrimitiveComponent
{
    DECLARE_OBJECT(UTextBatch, UPrimitiveComponent)
public:
    UTextBatch();
    ~UTextBatch();

    void Update(float deltaTime) override;
    void Submit(const FWString& Font, const FString& Text, const FColor& Color, float Height,
        FVector Position = FVector::Zero(),
        FRotator Rotation = FRotator(),
        FVector Scale = FVector::One(),
        bool bUUID = true,
        UTextComponent* comp = nullptr);
    void Clear();

    virtual void CreateRenderObjects() override;
    virtual void UpdateRenderObjects() override;

private:
    TMap<FWString, FFontGroup> FontMap;
    bool bEntriesDirty = true;

    bool bPrevShowUUID = false;
    bool bPrevShowText = false;
    bool bWasShowingUUID = false;

public:
    bool bUseStaticAtlas = false;
};
