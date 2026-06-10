#include "Cloth/NvClothBackend.h"

#include "Core/Logging/Log.h"

#include <NvCloth/Callbacks.h>
#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h>
#include <foundation/PxAllocatorCallback.h>
#include <foundation/PxErrorCallback.h>
#include <foundation/PxErrors.h>
#include <malloc.h>

#include <mutex>

namespace
{
    class FNvClothAllocatorCallback final : public physx::PxAllocatorCallback
    {
    public:
        void* allocate(size_t Size, const char* /*TypeName*/, const char* /*Filename*/, int /*Line*/) override
        {
            return _aligned_malloc(Size, 16);
        }

        void deallocate(void* Ptr) override
        {
            _aligned_free(Ptr);
        }
    };

    class FNvClothErrorCallback final : public physx::PxErrorCallback
    {
    public:
        void reportError(physx::PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
        {
            const char* Severity = "Info";
            if (Code == physx::PxErrorCode::eABORT || Code == physx::PxErrorCode::eOUT_OF_MEMORY)
            {
                Severity = "Fatal";
            }
            else if (Code == physx::PxErrorCode::eINTERNAL_ERROR ||
                     Code == physx::PxErrorCode::eINVALID_OPERATION)
            {
                Severity = "Error";
            }
            else if (Code == physx::PxErrorCode::eINVALID_PARAMETER ||
                     Code == physx::PxErrorCode::ePERF_WARNING ||
                     Code == physx::PxErrorCode::eDEBUG_WARNING)
            {
                Severity = "Warning";
            }

            UE_LOG("[NvCloth %s] %s (%s:%d)", Severity, Message ? Message : "", File ? File : "", Line);
        }
    };

    class FNvClothAssertHandler final : public nv::cloth::PxAssertHandler
    {
    public:
        void operator()(const char* Expression, const char* File, int Line, bool& Ignore) override
        {
            UE_LOG("[NvCloth Assert] %s (%s:%d)", Expression ? Expression : "", File ? File : "", Line);
            Ignore = false;
        }
    };

    FNvClothAllocatorCallback GNvClothAllocator;
    FNvClothErrorCallback GNvClothErrorCallback;
    FNvClothAssertHandler GNvClothAssertHandler;

    std::mutex GNvClothInitMutex;
    bool GNvClothCallbacksInitialized = false;

    void EnsureNvClothCallbacksInitialized()
    {
        std::lock_guard<std::mutex> Lock(GNvClothInitMutex);
        if (GNvClothCallbacksInitialized)
        {
            return;
        }

        nv::cloth::InitializeNvCloth(
            &GNvClothAllocator,
            &GNvClothErrorCallback,
            &GNvClothAssertHandler,
            nullptr
        );
        GNvClothCallbacksInitialized = true;
    }
}

FNvClothSolver::FNvClothSolver(nv::cloth::Solver* InSolver)
    : Solver(InSolver)
{
}

FNvClothSolver::~FNvClothSolver()
{
    Reset();
}

FNvClothSolver::FNvClothSolver(FNvClothSolver&& Other) noexcept
    : Solver(Other.Solver)
{
    Other.Solver = nullptr;
}

FNvClothSolver& FNvClothSolver::operator=(FNvClothSolver&& Other) noexcept
{
    if (this != &Other)
    {
        Reset();
        Solver = Other.Solver;
        Other.Solver = nullptr;
    }
    return *this;
}

bool FNvClothSolver::IsValid() const
{
    return Solver != nullptr;
}

nv::cloth::Solver* FNvClothSolver::ReleaseNativeSolver()
{
    nv::cloth::Solver* Result = Solver;
    Solver = nullptr;
    return Result;
}

void FNvClothSolver::Reset()
{
    if (Solver)
    {
        delete Solver;
        Solver = nullptr;
    }
}

FNvClothCpuBackend::~FNvClothCpuBackend()
{
    Shutdown();
}

bool FNvClothCpuBackend::Initialize()
{
    if (Factory)
    {
        return true;
    }

    EnsureNvClothCallbacksInitialized();

    Factory = NvClothCreateFactoryCPU();
    if (!Factory)
    {
        UE_LOG("[NvCloth] Failed to create CPU factory");
        return false;
    }

    return true;
}

void FNvClothCpuBackend::Shutdown()
{
    if (Factory)
    {
        NvClothDestroyFactory(Factory);
        Factory = nullptr;
    }
}

bool FNvClothCpuBackend::IsInitialized() const
{
    return Factory != nullptr;
}

FClothBackendOutputCaps FNvClothCpuBackend::GetOutputCaps() const
{
    FClothBackendOutputCaps Caps;
    Caps.PrimaryStorage = EClothSimulationOutputStorage::CPUPositions;
    Caps.bSupportsCPUPositionReadback = true;
    Caps.bSupportsGPUResidentOutput = false;
    return Caps;
}

std::unique_ptr<IClothSolver> FNvClothCpuBackend::CreateSolver()
{
    if (!Factory && !Initialize())
    {
        return nullptr;
    }

    nv::cloth::Solver* Solver = Factory->createSolver();
    if (!Solver)
    {
        UE_LOG("[NvCloth] Failed to create CPU solver");
        return nullptr;
    }

    return std::make_unique<FNvClothSolver>(Solver);
}

bool RunNvClothCpuBackendLifecycleSmokeTest()
{
    FNvClothCpuBackend Backend;
    if (!Backend.Initialize())
    {
        return false;
    }

    std::unique_ptr<IClothSolver> Solver = Backend.CreateSolver();
    const bool bSuccess = Solver && Solver->IsValid();
    Solver.reset();
    Backend.Shutdown();
    return bSuccess;
}
