#pragma once
#include "EngineTypes.h"

enum class EViewMode : uint32
{
	Lit = 0,
	Unlit = 1,
	Wireframe = 2,
	Normals = 3,
	Depth = 4
};

enum class EShowFlag : uint32
{
	None = 0,
	StaticMesh = 1 << 0,
	Gizmo = 1 << 1,
	Grid = 1 << 2,
	Axis = 1 << 3,
	BoundingBox = 1 << 4,
	Camera = 1 << 5,
	Debug = 1 << 6,
	Text = 1 << 7,
	UUID = 1 << 8,
	All = 0xFFFFFFFF
};

struct FRenderState
{
	EViewMode ViewMode = EViewMode::Lit;
	uint32 ShowFlags = static_cast<uint32>(EShowFlag::All) & ~static_cast<uint32>(EShowFlag::Debug) & ~static_cast<uint32>(EShowFlag::UUID);
};
