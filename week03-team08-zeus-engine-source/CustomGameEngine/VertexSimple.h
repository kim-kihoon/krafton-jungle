#pragma once

// 정점 구조체 선언
struct FVertexSimple
{
    float x, y, z;    // Position
    float r, g, b, a; // Color

    float nx, ny, nz;
};

struct FVertexTex
{
    float x, y, z;
    float u, v;
};