#pragma once
#include <UI/IEditorPanel.h>

namespace UI
{
    /**
     * 선택된 오브젝트의 속성 편집 진입점을 제공하는 패널.
     */
    class SPropertyWindowPanel final : public IEditorPanel
    {
    public:
        bool Initialize(const FEditorContext& InContext) override;
        void Update(const FEditorContext& InContext, float InDeltaTime) override;
        void Draw(const FEditorContext& InContext) override;
        EEditorPanelType GetPanelType() const override;
        const char* GetPanelName() const override;
    };
}
