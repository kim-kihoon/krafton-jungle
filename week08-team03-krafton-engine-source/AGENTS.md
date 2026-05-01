# AGENTS.md

## Project Summary

This repository is a C++ DirectX 11 3D scene editor engine using ImGui.
It uses an Actor/Component architecture, custom UObject-style RTTI, JSON scene serialization, raycasting object selection, editor gizmos, and a multi-pass rendering pipeline.

There is also an ObjViewDebug configuration for a standalone OBJ mesh viewer.

## Build Commands

Use these commands from the repository root:

```bash
msbuild KraftonEngine.sln /p:Configuration=Debug /p:Platform=x64
msbuild KraftonEngine.sln /p:Configuration=Release /p:Platform=x64
msbuild KraftonEngine.sln /p:Configuration=ObjViewDebug /p:Platform=x64
python Scripts/GenerateProjectFiles.py
```

Output executable:

```text
KraftonEngine/Bin/<Configuration>/KraftonEngine.exe
```

There are no configured tests or lint tools.

## Coding Rules

- Use C++20 for x64.
- Use UTF-8 BOM for C++ and header files.
- Use tab indentation, tab size 4.
- Use UTF-8 without BOM for HLSL files.
- Headers should use project-rooted include paths, such as:

```cpp
#include "Engine/Core/InputSystem.h"
```

Naming:

- `F` prefix for structs/data types.
- `U` prefix for UObject-derived classes.
- `A` prefix for Actors.
- `E` prefix for enums.

## Architecture Notes

The engine uses:

- `DECLARE_CLASS` / `DEFINE_CLASS` based custom RTTI.
- `UObjectManager` for lifecycle and auto-naming.
- `FName` as pooled, case-insensitive identifiers.
- Actor/Component scene structure.
- JSON `.Scene` serialization through `FSceneSaveManager`.

Rendering follows this pattern:

```text
RenderCollector -> RenderBus -> Renderer
```

Render pass order:

```text
Opaque -> Font -> SubUV -> Translucent -> StencilMask -> Outline -> Editor -> Grid -> DepthLess
```

When adding a new render pass, prefer adding an entry to the `FPassRenderState` table instead of scattering state setup logic.

## Key Directories

- `KraftonEngine/Source/Engine/`: Core engine systems.
- `KraftonEngine/Source/Editor/`: ImGui editor layer.
- `KraftonEngine/Source/ObjViewer/`: Standalone mesh viewer.
- `KraftonEngine/Shaders/`: HLSL shaders.
- `KraftonEngine/Asset/`: Runtime assets.
- `KraftonEngine/Data/`: Source mesh data.

## Shadow Mapping Task Context

For shadow mapping work, refer to `GEMINI.md`.

Important requirements:

- Render shadows only when `ULightComponentBase::bCastShadows == true`.
- Disable shadow rendering entirely in Unlit view mode.
- Treat all lights and objects as movable; do not implement baked shadows.
- Support:
  - 1 Directional Light.
  - Multiple Point Lights.
  - Multiple Spot Lights.
- Point Light shadows require cube-map style rendering, likely `TextureCubeArray`.
- Provide editor/debug support:
  - Light perspective override.
  - Depth map visualization.
  - Shadow resolution scale.
  - Shadow memory/stat display.
- Implement bias controls:
  - `ShadowBias`.
  - `ShadowSlopeBias`.
- Basic filtering target: PCF.
- Optional advanced targets:
  - VSM.
  - Shadow Atlas.
  - CSM.

## Codex Operating Rules

Before modifying code:

- Identify the smallest set of files required.
- Do not scan the whole repository unless explicitly asked.
- Explain the intended change briefly.
- Prefer small, reviewable patches.

After editing, summarize:

- Files changed.
- Reason for change.
- Build command to verify.

When the task is broad, first inspect only headers or high-level entry files, then request or inspect implementation files as needed.

## Recommended Prompt Patterns

For shadow mapping tasks:

```text
Use AGENTS.md as the base instruction.
For this task, also read GEMINI.md.
Do not scan the whole repository yet.
First identify the minimum files needed to implement Directional Light shadow map rendering.
```

For planning a focused code change:

```text
Use AGENTS.md.
Read only the files related to ULightComponentBase and the render pass registration.
Do not modify code yet. First explain the implementation plan.
```
