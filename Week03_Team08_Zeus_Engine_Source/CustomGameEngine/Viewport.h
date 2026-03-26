#pragma once
#include "EngineTypes.h"
#include "MulticastDelegate.h"

DECLARE_MULTICAST_DELEGATE(FOnResizeDelegate, int32, int32)

struct FViewport
{
	int32 X;
	int32 Y;
	int32 Width;
	int32 Height;

	FViewport();
	FViewport(int32 InX, int32 InY, int32 InWidth, int32 InHeight);

	void SetViewport(int32 InWidth, int32 InHeight);

	static FOnResizeDelegate OnResizeDelegate;
};