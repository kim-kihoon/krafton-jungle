#pragma once
#include <UI/IEditorPanel.h>

namespace UI
{
    /**
     * Spawn 및 디버그 토글 요청을 관리하는 패널.
     */
    class SControlPanel final : public IEditorPanel
    {
    public:
        bool Initialize(const FEditorContext& InContext) override;
        void Update(const FEditorContext& InContext, float InDeltaTime) override;
        void Draw(const FEditorContext& InContext) override;
        EEditorPanelType GetPanelType() const override;
        const char* GetPanelName() const override;

    private:
        void ProcessSpawnRequests(const FEditorContext& InContext) const;
        void SyncDebugRenderSettings(const FEditorContext& InContext) const;
    };
}
