#pragma once

#include "Editor/Input/BaseEditorController.h"

#include <functional>

class FPIEController : public IBaseEditorController
{
public:
	void Tick(float InDeltaTime) override;
	void OnMouseMove(float DeltaX, float DeltaY) override;
	void OnLeftMouseClick(float X, float Y) override;
	void OnLeftMouseDragEnd(float X, float Y) override;
	void OnLeftMouseButtonUp(float X, float Y) override;
	void OnRightMouseClick(float DeltaX, float DeltaY) override;
	void OnLeftMouseDrag(float X, float Y) override;
	void OnRightMouseDrag(float DeltaX, float DeltaY) override;
	void OnMiddleMouseDrag(float DeltaX, float DeltaY) override;
	void OnKeyPressed(int VK) override;
	void OnKeyDown(int VK) override;
	void OnKeyReleased(int VK) override;
	void OnWheelScrolled(float Notch) override;

	void SetEndPIECallback(std::function<void()> Callback) { OnRequestEndPIE = std::move(Callback); }
	void ClearEndPIECallback() { OnRequestEndPIE = nullptr; }

	void SetToggleInputCaptureCallback(std::function<void()> Callback) { OnRequestToggleInputCapture = std::move(Callback); }
	void ClearToggleInputCaptureCallback() { OnRequestToggleInputCapture = nullptr; }

private:
	std::function<void()> OnRequestEndPIE;
	std::function<void()> OnRequestToggleInputCapture;
};
