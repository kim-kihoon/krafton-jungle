#pragma once
#include <UI/IEditorPanel.h>

namespace UI
{
    /**
     * 씬 통계와 선택 상태를 표현하는 패널.
     */
    class SSceneManagerPanel final : public IEditorPanel
    {
    public:
        bool Initialize(const FEditorContext& InContext) override;
        void Update(const FEditorContext& InContext, float InDeltaTime) override;
        void Draw(const FEditorContext& InContext) override;
        EEditorPanelType GetPanelType() const override;
        const char* GetPanelName() const override;
    };
}
