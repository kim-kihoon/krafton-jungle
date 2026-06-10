#pragma once

#include "Cloth/ClothBackend.h"

namespace nv
{
namespace cloth
{
    class Factory;
    class Solver;
}
}

class FNvClothSolver final : public IClothSolver
{
public:
    explicit FNvClothSolver(nv::cloth::Solver* InSolver);
    ~FNvClothSolver() override;

    FNvClothSolver(const FNvClothSolver&) = delete;
    FNvClothSolver& operator=(const FNvClothSolver&) = delete;

    FNvClothSolver(FNvClothSolver&& Other) noexcept;
    FNvClothSolver& operator=(FNvClothSolver&& Other) noexcept;

    bool IsValid() const override;

    nv::cloth::Solver* GetNativeSolver() const { return Solver; }
    nv::cloth::Solver* ReleaseNativeSolver();
    void Reset();

private:
    nv::cloth::Solver* Solver = nullptr;
};

class FNvClothCpuBackend final : public IClothBackend
{
public:
    FNvClothCpuBackend() = default;
    ~FNvClothCpuBackend() override;

    FNvClothCpuBackend(const FNvClothCpuBackend&) = delete;
    FNvClothCpuBackend& operator=(const FNvClothCpuBackend&) = delete;

    bool Initialize() override;
    void Shutdown() override;
    bool IsInitialized() const override;

    EClothBackendKind GetBackendKind() const override { return EClothBackendKind::CPU; }
    FClothBackendOutputCaps GetOutputCaps() const override;

    std::unique_ptr<IClothSolver> CreateSolver() override;

    nv::cloth::Factory* GetNativeFactory() const { return Factory; }

private:
    nv::cloth::Factory* Factory = nullptr;
};

bool RunNvClothCpuBackendLifecycleSmokeTest();

