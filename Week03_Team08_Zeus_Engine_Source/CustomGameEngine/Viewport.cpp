#include "Viewport.h"

FOnResizeDelegate FViewport::OnResizeDelegate;

FViewport::FViewport()
	: X(0), Y(0), Width(0), Height(0)
{
}

FViewport::FViewport(int32 InX, int32 InY, int32 InWidth, int32 InHeight)
	: X(InX), Y(InY), Width(InWidth), Height(InHeight)
{
}

void FViewport::SetViewport(int32 InWidth, int32 InHeight)
{
	Width = InWidth;
	Height = InHeight;
	OnResizeDelegate.Broadcast(Width, Height);
}
