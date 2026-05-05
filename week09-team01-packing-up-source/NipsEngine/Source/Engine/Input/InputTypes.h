#pragma once

enum class EKeyInputType
{
    KeyPressed,
    KeyDown,
    KeyReleased,
    KeyNone,
};

enum class EMouseInputType
{
    E_MouseMoved,         // delta movement (DX, DY)
    E_MouseMovedAbsolute, // viewport-local absolute position (X, Y)
    E_LeftMouseClicked,   // LMB pressed down
    E_LeftMouseDragged,   // LMB held + drag threshold met
    E_LeftMouseDragEnded, // LMB drag released
    E_LeftMouseButtonUp,  // LMB released (no drag / below threshold)
    E_RightMouseClicked,
    E_RightMouseDragged,
    E_MiddleMouseDragged,
    E_MouseWheelScrolled,
};
