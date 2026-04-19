#pragma once
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"

struct EditorPanelRect
{
    ImVec2 Pos = ImVec2(0.0f, 0.0f);
    ImVec2 Size = ImVec2(0.0f, 0.0f);
};

struct EditorLayout
{
    EditorPanelRect Toolbar;
    EditorPanelRect SceneManager;
    EditorPanelRect ControlPanel;
    EditorPanelRect PropertyPanel;
    EditorPanelRect Console;
    EditorPanelRect StatPanel;
};

struct EditorPanelVisibility
{
    bool bShowStatPanel = true;
    bool bShowSceneManager = true;
    bool bShowPropertyPanel = true;
    bool bShowControlPanel = true;
    bool bShowToolbar = true;
    bool bShowConsole = true;
};

inline EditorPanelVisibility& GetEditorPanelVisibility()
{
    static EditorPanelVisibility visibility;
    return visibility;
}

inline float ClampFloat(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

inline int CountVisible(bool a, bool b, bool c)
{
    return (a ? 1 : 0) + (b ? 1 : 0) + (c ? 1 : 0);
}

inline EditorLayout GetEditorLayout(const EditorPanelVisibility& visibility)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 rootPos = vp->WorkPos;
    const ImVec2 rootSize = vp->WorkSize;

    const float gap = 0.0f;

    const float leftW = rootSize.x * 0.22f;
    const float rightW = rootSize.x * 0.24f;

    const float statsH = visibility.bShowStatPanel
        ? ClampFloat(rootSize.y * 0.10f, 90.0f, 140.0f)
        : 0.0f;

    const float toolbarH = visibility.bShowToolbar
        ? ClampFloat(rootSize.y * 0.06f, 36.0f, 64.0f)
        : 0.0f;

    const float consoleH = visibility.bShowConsole
        ? ClampFloat(rootSize.y * 0.28f, 180.0f, 320.0f)
        : 0.0f;

    const float bodyY = rootPos.y;
    const float bodyH = rootSize.y - consoleH - (visibility.bShowConsole ? gap : 0.0f);

    EditorLayout layout{};

    // Left column: Stat -> SceneManager
    float leftY = bodyY;

    if (visibility.bShowStatPanel)
    {
        layout.StatPanel = {
            ImVec2(rootPos.x, leftY),
            ImVec2(leftW, statsH)
        };

        leftY += statsH;
        if (visibility.bShowSceneManager)
            leftY += gap;
    }

    if (visibility.bShowSceneManager)
    {
        const float sceneManagerH = (bodyY + bodyH) - leftY;
        layout.SceneManager = {
            ImVec2(rootPos.x, leftY),
            ImVec2(leftW, sceneManagerH > 0.0f ? sceneManagerH : 0.0f)
        };
    }

    // Right column: Property -> Control -> Toolbar
    const int rightVisibleCount = CountVisible(
        visibility.bShowPropertyPanel,
        visibility.bShowControlPanel,
        visibility.bShowToolbar
    );

    const int rightGapCount = rightVisibleCount > 0 ? (rightVisibleCount - 1) : 0;
    const float rightFixedH = toolbarH;
    float rightFlexibleH = bodyH - rightFixedH - rightGapCount * gap;
    if (rightFlexibleH < 0.0f)
        rightFlexibleH = 0.0f;

    float propertyH = 0.0f;
    float controlH = 0.0f;

    if (visibility.bShowPropertyPanel && visibility.bShowControlPanel)
    {
        propertyH = rightFlexibleH * 0.30f;
        controlH = rightFlexibleH - propertyH;
    }
    else if (visibility.bShowPropertyPanel)
    {
        propertyH = rightFlexibleH;
    }
    else if (visibility.bShowControlPanel)
    {
        controlH = rightFlexibleH;
    }

    const float rightX = rootPos.x + rootSize.x - rightW;
    float rightY = bodyY;

    if (visibility.bShowPropertyPanel)
    {
        layout.PropertyPanel = {
            ImVec2(rightX, rightY),
            ImVec2(rightW, propertyH)
        };

        rightY += propertyH;
        if (visibility.bShowControlPanel || visibility.bShowToolbar)
            rightY += gap;
    }

    if (visibility.bShowControlPanel)
    {
        layout.ControlPanel = {
            ImVec2(rightX, rightY),
            ImVec2(rightW, controlH)
        };

        rightY += controlH;
        if (visibility.bShowToolbar)
            rightY += gap;
    }

    if (visibility.bShowToolbar)
    {
        layout.Toolbar = {
            ImVec2(rightX, rightY),
            ImVec2(rightW, toolbarH)
        };
    }

    // Console
    if (visibility.bShowConsole)
    {
        layout.Console = {
            ImVec2(rootPos.x, rootPos.y + rootSize.y - consoleH),
            ImVec2(rootSize.x, consoleH)
        };
    }

    return layout;
}

inline ImGuiWindowFlags ApplyFixedWindow(const EditorPanelRect& rect, ImGuiWindowFlags extraFlags = 0)
{
    ImGui::SetNextWindowPos(rect.Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(rect.Size, ImGuiCond_Always);

    return ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        extraFlags;
}