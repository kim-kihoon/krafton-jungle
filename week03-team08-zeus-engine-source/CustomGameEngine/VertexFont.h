#pragma once

struct FVertexFont
{
    float x, y, z;
    float offsetR, offsetU;
    float u, v;

    FVertexFont() = default;

    FVertexFont(float inX, float inY, float inZ, float inOffsetR, float inOffsetU, float inU, float inV)
        : x(inX), y(inY), z(inZ), offsetR(inOffsetR), offsetU(inOffsetU), u(inU), v(inV)
    {
    }
};