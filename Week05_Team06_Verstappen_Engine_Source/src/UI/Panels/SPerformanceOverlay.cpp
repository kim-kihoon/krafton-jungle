
#include <UI/Panels/SPerformanceOverlay.h>
#include <Core/PlatformTime.h>
#include <algorithm>
#include <imgui.h>

namespace UI
{
    bool SPerformanceOverlay::Initialize(const FEditorContext& InContext)
    {
        (void)InContext;
        return true;
    }

    void SPerformanceOverlay::Update(const FEditorContext& InContext, float InDeltaTime)
    {
        (void)InContext;
        (void)InDeltaTime;
    }

    void SPerformanceOverlay::Draw(const FEditorContext& InContext)
    {
        if (!InContext.ViewportState || !InContext.ViewportState->bShowStats || !InContext.ViewportState->Rect.IsValid() || !InContext.FrameData)
        {
            return;
        }

        const FEditorViewportRect& Rect = InContext.ViewportState->Rect;
        ImGuiViewport* MainViewport = ImGui::GetMainViewport();

        if (bApplyDefaultPlacement)
        {
            ImGui::SetNextWindowPos(
                ImVec2(
                    MainViewport->Pos.x + Rect.X + Rect.Width * 0.5f,
                    MainViewport->Pos.y + Rect.Y + 18.0f),
                ImGuiCond_Always,
                ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowSize(
                ImVec2((std::max)(420.0f, Rect.Width * 0.34f), (std::max)(300.0f, Rect.Height * 0.34f)),
                ImGuiCond_Always);
            bApplyDefaultPlacement = false;
        }
        ImGui::SetNextWindowBgAlpha(0.78f);

        const ImGuiWindowFlags Flags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoCollapse;

        if (!ImGui::Begin("Stats", nullptr, Flags))
        {
            ImGui::End();
            return;
        }

        const Core::FFramePerformanceMetrics& Metrics = InContext.FrameData->PerformanceMetrics;
        const double LastPickingMS = Core::FPlatformTime::ToMilliseconds(Metrics.LastPickingCycles);
        const double TotalPickingMS = Core::FPlatformTime::ToMilliseconds(Metrics.TotalPickingCycles);
        const double AveragePickingMS =
            (Metrics.TotalPickCount > 0) ? (TotalPickingMS / static_cast<double>(Metrics.TotalPickCount)) : 0.0;

        const double GridLastPickingMS = Core::FPlatformTime::ToMilliseconds(Metrics.GridLastPickingCycles);
        const double GridTotalPickingMS = Core::FPlatformTime::ToMilliseconds(Metrics.GridTotalPickingCycles);
        const double GridAveragePickingMS =
            (Metrics.GridTotalPickCount > 0) ? (GridTotalPickingMS / static_cast<double>(Metrics.GridTotalPickCount)) : 0.0;

        const double BVHLastPickingMS = Core::FPlatformTime::ToMilliseconds(Metrics.BVHLastPickingCycles);
        const double BVHTotalPickingMS = Core::FPlatformTime::ToMilliseconds(Metrics.BVHTotalPickingCycles);
        const double BVHAveragePickingMS =
            (Metrics.BVHTotalPickCount > 0) ? (BVHTotalPickingMS / static_cast<double>(Metrics.BVHTotalPickCount)) : 0.0;

        const char* StructureName = (Metrics.CurrentStructure == Core::ESpatialStructure::UniformGrid) ? "Uniform Grid" : "BVH";

        ImGui::Text("Frame Index: %llu", static_cast<unsigned long long>(Metrics.FrameIndex));
        ImGui::Text("Structure: %s", StructureName);
        ImGui::Separator();
        ImGui::Text("Objects: %u", InContext.FrameData->TotalObjectCount);
        ImGui::Text("Visible: %u", InContext.FrameData->VisibleObjectCount);
        ImGui::Text("Selection Count: %u", InContext.FrameData->SelectedObjectCount);
        if (InContext.FrameData->bHasSelection)
        {
            ImGui::Text("Primary Selection: %u", InContext.FrameData->SelectedObjectIndex);
        }
        ImGui::Separator();

        ImGui::Text("Total Picks: %llu", static_cast<unsigned long long>(Metrics.TotalPickCount));
        ImGui::Text("Picking Last: %.4f ms", LastPickingMS);
        ImGui::Text("Picking Avg: %.4f ms", AveragePickingMS);
        ImGui::Text("Picking Total: %.2f ms", TotalPickingMS);
        ImGui::Text("Grid Pick: last %.4f / avg %.4f / total %.2f ms", GridLastPickingMS, GridAveragePickingMS, GridTotalPickingMS);
        ImGui::Text("BVH Pick: last %.4f / avg %.4f / total %.2f ms", BVHLastPickingMS, BVHAveragePickingMS, BVHTotalPickingMS);
        ImGui::Text("Grid Tests: cells %u / objects %u", Metrics.GridCellTestCount, Metrics.GridObjectAABBTestCount);
        ImGui::Text("BVH Tests: nodes %u / objects %u", Metrics.BVHNodeTestCount, Metrics.ObjectAABBTestCount);
        ImGui::Separator();
        ImGui::Text("Split: %.3f ms", Metrics.SplitTime);
        ImGui::Text("Prepass: %.3f ms", Metrics.PrepassTime);
        ImGui::Text("Hi-Z: %.3f ms", Metrics.HiZTime);
        ImGui::Text("Cull: %.3f ms", Metrics.CullTime);
        ImGui::Text("Draw: %.3f ms", Metrics.DrawTime);
        ImGui::Text("Draw Count: %u", Metrics.DrawCount);
        ImGui::Text("Prev Visible: %u", Metrics.PrevVisible);
        ImGui::Text("Prev Invisible: %u", Metrics.PrevInvisible);
        ImGui::Text("Total Objects (Render Stat): %u", Metrics.TotalObjectsCount);
        ImGui::End();
    }

    EEditorPanelType SPerformanceOverlay::GetPanelType() const
    {
        return EEditorPanelType::Overlay;
    }

    const char* SPerformanceOverlay::GetPanelName() const
    {
        return "Performance Overlay";
    }
}
