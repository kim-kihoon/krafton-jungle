#pragma once

#include "Core/Types/CoreTypes.h"

#include <memory>

enum class EClothBackendKind : uint8
{
    CPU,
    DX11,
};

enum class EClothSimulationOutputStorage : uint8
{
    None,
    CPUPositions,
    GPUResident,
};

struct FClothBackendOutputCaps
{
    EClothSimulationOutputStorage PrimaryStorage = EClothSimulationOutputStorage::None;
    bool bSupportsCPUPositionReadback = false;
    bool bSupportsGPUResidentOutput = false;
};

class IClothSolver
{
public:
    virtual ~IClothSolver() = default;

    virtual bool IsValid() const = 0;
};

class IClothBackend
{
public:
    virtual ~IClothBackend() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsInitialized() const = 0;

    virtual EClothBackendKind GetBackendKind() const = 0;
    virtual FClothBackendOutputCaps GetOutputCaps() const = 0;

    virtual std::unique_ptr<IClothSolver> CreateSolver() = 0;
};

