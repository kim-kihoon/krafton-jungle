#pragma once

#include "Core/CoreTypes.h"

#include <cstdint>

enum class ESkinningMode : uint8
{
    CPU,
    GPUVertexShader,
    GPUComputeSkinCache
};

class FSkinningRuntimeSettings
{
public:
    static ESkinningMode GetMode()
    {
        return GetModeStorage();
    }

    static uint64 GetRevision()
    {
        return GetRevisionStorage();
    }

    static void SetMode(ESkinningMode InMode)
    {
        if (InMode == ESkinningMode::GPUComputeSkinCache)
        {
            InMode = ESkinningMode::GPUVertexShader;
        }

        ESkinningMode& Mode = GetModeStorage();
        if (Mode == InMode)
        {
            return;
        }

        Mode = InMode;
        ++GetRevisionStorage();
    }

    static const char* ToString(ESkinningMode InMode)
    {
        switch (InMode)
        {
        case ESkinningMode::CPU:
            return "CPU";
        case ESkinningMode::GPUVertexShader:
            return "GPU";
        case ESkinningMode::GPUComputeSkinCache:
            return "GPUComputeSkinCache";
        default:
            return "Unknown";
        }
    }

private:
    static ESkinningMode& GetModeStorage()
    {
        static ESkinningMode Mode = ESkinningMode::CPU;
        return Mode;
    }

    static uint64& GetRevisionStorage()
    {
        static uint64 Revision = 0;
        return Revision;
    }
};
