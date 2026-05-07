#pragma once

#include "Component/PrimitiveComponent.h"
#include "Render/Common/RenderTypes.h"
#include "Math/Color.h"

/* @brief 단순 도형(Box/Sphere/Capsule) 컴포넌트, 메시를 그리지 않고 충돌체 역할만 한다. */
class UShapeComponent : public UPrimitiveComponent
{
public:
    DECLARE_CLASS(UShapeComponent, UPrimitiveComponent)

    UShapeComponent();

    // ─────────────── Property Window & Serializer ───────────────
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;

    // ─────────────── Getter ─────────────────────────────────────
    EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_Line; }
    bool IsRaycastTarget() const override { return false; }
    const FColor& GetShapeColor() const { return ShapeColor; }
    float GetLineThickness() const { return LineThickness; }
    bool GetDrawOnlyIfSelected() const { return bDrawOnlyIfSelected; }

    // ─────────────── Shape Update ───────────────────────────────
    virtual void UpdateBodySetup();

protected:
    FColor ShapeColor = FColor(uint32(223), uint32(149), uint32(157), uint32(255)); // UE5 기본값: Pastel Pink
    float LineThickness = 1.0f;
    bool bDrawOnlyIfSelected = false;
};
