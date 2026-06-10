#pragma once

class UWorld;
struct FFrameContext;
class FLineGeometry;

namespace CollisionDebugDraw
{
	void AppendCollisionWireframes(UWorld* World, const FFrameContext& Frame, FLineGeometry& OutLines);
}
