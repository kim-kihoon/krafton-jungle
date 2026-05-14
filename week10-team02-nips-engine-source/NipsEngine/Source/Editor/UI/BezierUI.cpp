#include "BezierUI.h"

// 엔진 헤더를 ImGui보다 먼저 포함: Vector2.h 내 FVector 참조가 먼저 정의되어야 함
#include "Math/Vector.h"   // FVector 정의 (Vector2.h의 CrossProduct 반환 타입)
#include "Math/Vector2.h"  // FVector2
#include "Math/Vector4.h"  // FVector4

// ImGui 내부 API (GetCurrentWindow, ItemSize, ItemAdd, RenderFrame 등)
#include "ImGui/imgui_internal.h"

using namespace ImGui;

// ── 내부 헬퍼: Cubic Bezier 기저 계수를 사전 계산하여 커브를 샘플링 ──────────────
template <int steps>
static void bezier_table(FVector2 P[4], FVector2 results[steps + 1])
{
    static float C[(steps + 1) * 4], *K = nullptr;
    if (!K)
    {
        K = C;
        for (unsigned step = 0; step <= steps; ++step)  
        {
            float t          = (float)step / (float)steps;
            C[step * 4 + 0]  = (1 - t) * (1 - t) * (1 - t); // * P0
            C[step * 4 + 1]  = 3 * (1 - t) * (1 - t) * t;   // * P1
            C[step * 4 + 2]  = 3 * (1 - t) * t * t;          // * P2
            C[step * 4 + 3]  = t * t * t;                     // * P3
        }
    }
    for (unsigned step = 0; step <= steps; ++step)
    {
        results[step] = FVector2(
            K[step * 4 + 0] * P[0].X + K[step * 4 + 1] * P[1].X + K[step * 4 + 2] * P[2].X + K[step * 4 + 3] * P[3].X,
            K[step * 4 + 0] * P[0].Y + K[step * 4 + 1] * P[1].Y + K[step * 4 + 2] * P[2].Y + K[step * 4 + 3] * P[3].Y
        );
    }
}

// ── ImGui 커브 에디터 위젯 ────────────────────────────────────────────────────────
int BezierUI::Bezier(const char* label, float cp[4])
{
    constexpr int   SMOOTHNESS  = 64; // 커브 분할 수
    constexpr float CURVE_WIDTH = 4.f; // 커브 선 두께
    constexpr float LINE_WIDTH  = 1.f; // 핸들 연결선 두께
    constexpr float GRAB_RADIUS = 6.f; // 핸들 원 반지름
    constexpr float GRAB_BORDER = 2.f; // 핸들 원 테두리 두께

    const ImGuiStyle& Style    = GetStyle();
    ImDrawList*       DrawList = GetWindowDrawList();
    ImGuiWindow*      Window   = GetCurrentWindow();
    if (Window->SkipItems)
        return false;

    PushID(label);

    // 헤더 슬라이더 — label은 이미 PushID로 스코프됐으므로 "##v" 사용
    int changed = SliderFloat4("##v", cp, 0, 1, "%.3f");
    bool hovered = IsItemActive() || IsItemHovered();
    Dummy(ImVec2(0, 3));

    // 캔버스 크기 — 순수 데이터이므로 FVector2 사용
    const float    avail  = GetContentRegionAvail().x;
    const float    dim    = ImMin(avail, 128.f);
    const FVector2 Canvas(dim, dim);

    // ImRect — ItemSize/ItemAdd/RenderFrame 이 직접 요구하므로 유지
    ImRect bb(Window->DC.CursorPos, ImVec2(Window->DC.CursorPos.x + Canvas.X, Window->DC.CursorPos.y + Canvas.Y));
    ItemSize(bb);
    if (!ItemAdd(bb, NULL))
    {
        PopID();
        return changed;
    }

    // IsHovered(ImRect, id) 는 ImGui 1.78 이후 제거됨 → IsMouseHoveringRect 사용
    hovered |= IsMouseHoveringRect(bb.Min, bb.Max);

    RenderFrame(bb.Min, bb.Max, GetColorU32(ImGuiCol_FrameBg, 1), true, Style.FrameRounding);

    // 배경 그리드
    const float bbW = bb.Max.x - bb.Min.x;
    const float bbH = bb.Max.y - bb.Min.y;

    for (int i = 0; i <= (int)Canvas.X; i += ((int)Canvas.X / 4))
    {
        DrawList->AddLine(
            ImVec2(bb.Min.x + i, bb.Min.y),
            ImVec2(bb.Min.x + i, bb.Max.y),
            GetColorU32(ImGuiCol_TextDisabled));
    }
    for (int i = 0; i <= (int)Canvas.Y; i += ((int)Canvas.Y / 4))
    {
        DrawList->AddLine(
            ImVec2(bb.Min.x, bb.Min.y + i),
            ImVec2(bb.Max.x, bb.Min.y + i),
            GetColorU32(ImGuiCol_TextDisabled));
    }

    // 커브 샘플링 — FVector2: 순수 수학 데이터
    FVector2 Q[4] = { { 0.f, cp[4] }, { cp[0], cp[1] }, { cp[2], cp[3] }, { 1.f, cp[5] } };
    FVector2 results[SMOOTHNESS + 1];
    bezier_table<SMOOTHNESS>(Q, results);

	{
        ImVec2 p0_pos(bb.Min.x, (1.f - cp[4]) * bbH + bb.Min.y);
        SetCursorScreenPos(ImVec2(p0_pos.x - GRAB_RADIUS, p0_pos.y - GRAB_RADIUS));
        InvisibleButton("##P0", ImVec2(2 * GRAB_RADIUS, 2 * GRAB_RADIUS));
        if (IsItemActive() && IsMouseDragging(0))
        {
            cp[4] -= GetIO().MouseDelta.y / Canvas.Y;
            cp[4] = ImClamp(cp[4], 0.0f, 1.0f);
            changed = true;
        }
        DrawList->AddCircleFilled(p0_pos, GRAB_RADIUS, GetColorU32(ImGuiCol_PlotLinesHovered));
    }

	{
        ImVec2 p3_pos(bb.Max.x, (1.f - cp[5]) * bbH + bb.Min.y);
        SetCursorScreenPos(ImVec2(p3_pos.x - GRAB_RADIUS, p3_pos.y - GRAB_RADIUS));
        InvisibleButton("##P3", ImVec2(2 * GRAB_RADIUS, 2 * GRAB_RADIUS));
        if (IsItemActive() && IsMouseDragging(0))
        {
            cp[5] -= GetIO().MouseDelta.y / Canvas.Y;
            cp[5] = ImClamp(cp[5], 0.0f, 1.0f);
            changed = true;
        }
        DrawList->AddCircleFilled(p3_pos, GRAB_RADIUS, GetColorU32(ImGuiCol_PlotLinesHovered));
    }

    {
        // 컨트롤 포인트 핸들 (2개)
        for (int i = 0; i < 2; ++i)
        {
            PushID(i);

            // 커브 공간(0~1) → 스크린 공간. ImVec2는 draw API 인자이므로 유지
            ImVec2 pos(
                cp[i * 2 + 0] * bbW + bb.Min.x,
                (1.f - cp[i * 2 + 1]) * bbH + bb.Min.y
            );
            SetCursorScreenPos(ImVec2(pos.x - GRAB_RADIUS, pos.y - GRAB_RADIUS));

            InvisibleButton("##handle", ImVec2(2 * GRAB_RADIUS, 2 * GRAB_RADIUS));

            if (IsItemActive() || IsItemHovered())
                SetTooltip("(%4.3f, %4.3f)", cp[i * 2 + 0], cp[i * 2 + 1]);

            if (IsItemActive() && IsMouseDragging(0))
            {
                cp[i * 2 + 0] += GetIO().MouseDelta.x / Canvas.X;
                cp[i * 2 + 1] -= GetIO().MouseDelta.y / Canvas.Y;

				cp[i * 2 + 0] = ImClamp(cp[i * 2 + 0], 0.0f, 1.0f);
                cp[i * 2 + 1] = ImClamp(cp[i * 2 + 1], 0.0f, 1.0f);
                changed = true;
            }

			PopID();
        }

        if (hovered || changed)
            DrawList->PushClipRectFullScreen();

        // 커브 선 그리기
        {
            ImColor color(GetStyle().Colors[ImGuiCol_PlotLines]);
            for (int i = 0; i < SMOOTHNESS; ++i)
            {
                // FVector2 — 커브 공간 포인트 (순수 수학)
                FVector2 p(results[i + 0].X, 1.f - results[i + 0].Y);
                FVector2 q(results[i + 1].X, 1.f - results[i + 1].Y);
                // ImVec2 — 스크린 공간 변환 후 draw API에 전달
                ImVec2 r(p.X * bbW + bb.Min.x, p.Y * bbH + bb.Min.y);
                ImVec2 s(q.X * bbW + bb.Min.x, q.Y * bbH + bb.Min.y);
                DrawList->AddLine(r, s, color, CURVE_WIDTH);
            }
        }

        // 핸들 선 + 원 그리기
        // FVector4 — 색상 데이터 (X=R, Y=G, Z=B, W=A)
        float    luma = IsItemActive() || IsItemHovered() ? 0.5f : 1.0f;
        FVector4 pink(1.00f, 0.00f, 0.75f, luma);
        FVector4 cyan(0.00f, 0.75f, 1.00f, luma);
        const ImVec4& src = GetStyle().Colors[ImGuiCol_Text];
        FVector4 white(src.x, src.y, src.z, src.w);

        // ImVec2 — 스크린 공간 핸들 위치 (draw API 인자)
        ImVec2 p1(cp[0] * bbW + bb.Min.x, (1.f - cp[1]) * bbH + bb.Min.y);
        ImVec2 p2(cp[2] * bbW + bb.Min.x, (1.f - cp[3]) * bbH + bb.Min.y);

        DrawList->AddLine(ImVec2(bb.Min.x, bb.Max.y), p1, ImColor(white.X, white.Y, white.Z, white.W), LINE_WIDTH);
        DrawList->AddLine(ImVec2(bb.Max.x, bb.Min.y), p2, ImColor(white.X, white.Y, white.Z, white.W), LINE_WIDTH);
        DrawList->AddCircleFilled(p1, GRAB_RADIUS,                ImColor(white.X, white.Y, white.Z, white.W));
        DrawList->AddCircleFilled(p1, GRAB_RADIUS - GRAB_BORDER,  ImColor(pink.X,  pink.Y,  pink.Z,  pink.W));
        DrawList->AddCircleFilled(p2, GRAB_RADIUS,                ImColor(white.X, white.Y, white.Z, white.W));
        DrawList->AddCircleFilled(p2, GRAB_RADIUS - GRAB_BORDER,  ImColor(cyan.X,  cyan.Y,  cyan.Z,  cyan.W));

        if (hovered || changed)
            DrawList->PopClipRect();

        SetCursorScreenPos(ImVec2(bb.Min.x, bb.Max.y + GRAB_RADIUS));
    }

    PopID();
    return changed;
}
