#pragma once
#include "SlateUtils.h"

struct FViewportRect;

class ISlateViewport
{
public:
	virtual ~ISlateViewport() = default;

	// 렌더링
	virtual void Draw() = 0;

	// 레이아웃 / 크기 갱신
	virtual void SetRect(const FViewportRect& InRect) = 0;
	virtual const FViewportRect& GetRect() const = 0;

	// 좌표 보조
	virtual bool ContainsPoint(int32 X, int32 Y) const = 0;
	virtual void WindowToLocal(int32 X, int32 Y, int32& OutX, int32& OutY) const = 0;

};
