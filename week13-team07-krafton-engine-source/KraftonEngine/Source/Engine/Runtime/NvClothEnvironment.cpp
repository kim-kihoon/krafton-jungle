#include "Runtime/NvClothEnvironment.h"
#include "Core/Logging/Log.h"
#include <NvCloth/Callbacks.h>
#include <NvCloth/Factory.h>
#include <NvCloth/DxContextManagerCallback.h>
#include <PxPhysicsAPI.h>
#include <mutex>
#include "Runtime/Engine.h"

class FNvClothAssertHandler : public nv::cloth::PxAssertHandler
{
public:
	void operator()(const char* exp, const char* file, int line, bool& ignore) override
	{
		ignore = false;
		UE_LOG("[NvCloth Assert] %s (%s:%d)", exp ? exp : "", file ? file : "", line);
	}
};

class FNvClothErrorCallback : public physx::PxErrorCallback
{
public:
	void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
	{
		const char* Severity = "Info";
		if (code == physx::PxErrorCode::eABORT || code == physx::PxErrorCode::eOUT_OF_MEMORY) Severity = "Fatal";
		else if (code == physx::PxErrorCode::eINTERNAL_ERROR || code == physx::PxErrorCode::eINVALID_OPERATION) Severity = "Error";
		else if (code == physx::PxErrorCode::eINVALID_PARAMETER || code == physx::PxErrorCode::ePERF_WARNING || code == physx::PxErrorCode::eDEBUG_WARNING) Severity = "Warning";

		UE_LOG("[NvCloth %s] %s (%s:%d)", Severity, message ? message : "", file ? file : "", line);
	}
};

class FClothDx11ContextManager : public nv::cloth::DxContextManagerCallback
{
public:
	FClothDx11ContextManager(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
		: Device(InDevice), Context(InContext) {}
	void acquireContext() override { Mutex.lock(); }
	void releaseContext() override { Mutex.unlock(); }
	ID3D11Device* getDevice() const override { return Device; }
	ID3D11DeviceContext* getContext() const override { return Context; }
	bool synchronizeResources() const override { return false; }

private:
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* Context = nullptr;
	mutable std::recursive_mutex Mutex;
};

static physx::PxDefaultAllocator GNvClothAllocator;
static FNvClothErrorCallback GNvClothErrorCallback;
static FNvClothAssertHandler GNvClothAssertHandler;
static bool GbNvClothInitialized = false;

static FClothDx11ContextManager* GDxContextManager = nullptr;
static nv::cloth::Factory* GClothFactory = nullptr;

bool FNvClothEnvironment::Initialize()
{
	if (GbNvClothInitialized) return true;

	nv::cloth::InitializeNvCloth(&GNvClothAllocator, &GNvClothErrorCallback, &GNvClothAssertHandler, nullptr);

	if (GEngine)
	{
		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		ID3D11DeviceContext* Context = GEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
		if (Device && Context)
		{
			GDxContextManager = new FClothDx11ContextManager(Device, Context);
			GClothFactory = NvClothCreateFactoryDX11(GDxContextManager);
		}
	}

	if (!GClothFactory)
	{
		UE_LOG("[NvCloth Fatal] DX11 Factory 생성 실패. CPU 폴백이 비활성화되었습니다.");
		if (GDxContextManager) { delete GDxContextManager; GDxContextManager = nullptr; }
		return false;
	}

	GbNvClothInitialized = true;
	return true;
}

void FNvClothEnvironment::Shutdown()
{
	if (GClothFactory)
	{
		NvClothDestroyFactory(GClothFactory);
		GClothFactory = nullptr;
	}
	if (GDxContextManager)
	{
		delete GDxContextManager;
		GDxContextManager = nullptr;
	}
	GbNvClothInitialized = false;
}

bool FNvClothEnvironment::IsInitialized() { return GbNvClothInitialized; }
nv::cloth::Factory* FNvClothEnvironment::GetFactory() { return GClothFactory; }