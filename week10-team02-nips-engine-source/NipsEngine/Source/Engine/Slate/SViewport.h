#pragma once
#include "SWindow.h"

class ISlateViewport;

// Viewport가 차지하는 공간을 묘사하는 Slate Widget
class SViewport : public SWindow
{

public:
	// Get Set
	void SetViewportInterface(ISlateViewport* InInterface) { ViewportInterface = InInterface; }
	ISlateViewport* GetViewportInterface() const { return ViewportInterface; }

private:
	ISlateViewport* ViewportInterface = nullptr;
};

