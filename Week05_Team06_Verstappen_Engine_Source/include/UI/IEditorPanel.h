#pragma once
#include <UI/EditorTypes.h>

namespace UI
{
    /**
     * 모든 에디터 패널이 따라야 하는 공통 인터페이스.
     */
    class IEditorPanel
    {
    public:
        virtual ~IEditorPanel() = default;

        virtual bool Initialize(const FEditorContext& InContext) = 0;
        virtual void Update(const FEditorContext& InContext, float InDeltaTime) = 0;
        virtual void Draw(const FEditorContext& InContext) = 0;
        virtual EEditorPanelType GetPanelType() const = 0;
        virtual const char* GetPanelName() const = 0;
    };
}
