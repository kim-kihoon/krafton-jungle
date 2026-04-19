#pragma once
#include <UI/IEditorPanel.h>

namespace UI
{
    /**
     * 에디터 내부 상태 메시지를 노출하는 콘솔 패널.
     */
    class SConsolePanel final : public IEditorPanel
    {
    public:
        bool Initialize(const FEditorContext& InContext) override;
        void Update(const FEditorContext& InContext, float InDeltaTime) override;
        void Draw(const FEditorContext& InContext) override;
        EEditorPanelType GetPanelType() const override;
        const char* GetPanelName() const override;
    };
}
