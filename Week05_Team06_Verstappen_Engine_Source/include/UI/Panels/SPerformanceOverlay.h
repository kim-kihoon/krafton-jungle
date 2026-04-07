#pragma once
#include <UI/IEditorPanel.h>

namespace UI
{
    /**
     * 프레임 성능 및 Picking 측정값을 노출하는 Overlay 패널.
     */
    class SPerformanceOverlay final : public IEditorPanel
    {
    public:
        bool Initialize(const FEditorContext& InContext) override;
        void Update(const FEditorContext& InContext, float InDeltaTime) override;
        void Draw(const FEditorContext& InContext) override;
        EEditorPanelType GetPanelType() const override;
        const char* GetPanelName() const override;

    private:
        bool bApplyDefaultPlacement = true;
    };
}
