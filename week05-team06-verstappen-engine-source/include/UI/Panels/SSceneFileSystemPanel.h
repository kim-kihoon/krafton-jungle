#pragma once
#include <UI/IEditorPanel.h>

namespace UI
{
    /**
     * Scene 바이너리 저장 및 로드 요청을 전달하는 패널.
     */
    class SSceneFileSystemPanel final : public IEditorPanel
    {
    public:
        bool Initialize(const FEditorContext& InContext) override;
        void Update(const FEditorContext& InContext, float InDeltaTime) override;
        void Draw(const FEditorContext& InContext) override;
        EEditorPanelType GetPanelType() const override;
        const char* GetPanelName() const override;

    private:
        void ProcessFileRequests(const FEditorContext& InContext) const;
    };
}
