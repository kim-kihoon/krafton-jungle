#include "Physics/PhysXVehicleRuntime.h"

#include "Core/Logging/Log.h"
#include "Core/Types/CollisionTypes.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysicsTypes.h"

#include <PxPhysicsAPI.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleSDK.h>
#include <vehicle/PxVehicleUtilControl.h>
#include <vehicle/PxVehicleUtilSetup.h>
#include <vehicle/PxVehicleUpdate.h>

namespace
{
    constexpr uint32 VehicleWheelCount        = 4;
    constexpr uint32 VehicleSurfaceTypeTarmac = 0;
    constexpr uint32 VehicleTireTypeNormal    = 0;

    enum class EDrive4WWheelSlot : uint8
    {
        FrontLeft  = 0,
        FrontRight = 1,
        RearLeft   = 2,
        RearRight  = 3,
    };

    bool IsVehicleHandleIndexValid(uint32 Index, size_t Size)
    {
        return Index != UINT32_MAX && static_cast<size_t>(Index) < Size;
    }

    physx::PxQueryHitType::Enum VehicleRaycastPreFilter(physx::PxFilterData QueryFilterData, physx::PxFilterData ObjectFilterData, const void*, physx::PxU32, physx::PxHitFlags&)
    {
        if (QueryFilterData.word3 != 0 && QueryFilterData.word3 == ObjectFilterData.word3)
        {
            return physx::PxQueryHitType::eNONE;
        }

        const uint32 ShapeObjectBit = 1u << GetPhysicsFilterObjectType(ObjectFilterData.word0);

        if ((ShapeObjectBit & QueryFilterData.word0) == 0)
        {
            return physx::PxQueryHitType::eNONE;
        }

        if ((ObjectFilterData.word0 & PhysicsFilter_IsTrigger) != 0)
        {
            return physx::PxQueryHitType::eNONE;
        }

        const bool bQueryEnabled =
            (ObjectFilterData.word0 & (PhysicsFilter_QueryOnly | PhysicsFilter_QueryAndPhysics)) != 0;
        if (!bQueryEnabled)
        {
            return physx::PxQueryHitType::eNONE;
        }

        return physx::PxQueryHitType::eBLOCK;
    }

    physx::PxF32 ComputeWheelMOI(float Mass, float Radius)
    {
        return 0.5f * Mass * Radius * Radius;
    }

    physx::PxVehicleDrivableSurfaceToTireFrictionPairs* CreateVehicleFrictionPairs(physx::PxMaterial* DefaultMaterial, float TireFriction)
    {
        if (!DefaultMaterial)
        {
            return nullptr;
        }

        physx::PxVehicleDrivableSurfaceType SurfaceTypes[1];
        SurfaceTypes[0].mType = VehicleSurfaceTypeTarmac;

        const physx::PxMaterial* SurfaceMaterials[1];
        SurfaceMaterials[0] = DefaultMaterial;

        physx::PxVehicleDrivableSurfaceToTireFrictionPairs* Pairs = physx::PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, 1);
        if (!Pairs)
        {
            return nullptr;
        }

        Pairs->setup(1, 1, SurfaceMaterials, SurfaceTypes);
        Pairs->setTypePairFriction(VehicleSurfaceTypeTarmac, VehicleTireTypeNormal, TireFriction);
        return Pairs;
    }

    void SetupVehicleWheelSimData(const FVehicleDesc& Desc, physx::PxVehicleWheelsSimData& WheelsSimData)
    {
        physx::PxVehicleWheelData      WheelData[VehicleWheelCount];
        physx::PxVehicleTireData       TireData[VehicleWheelCount];
        physx::PxVehicleSuspensionData SuspensionData[VehicleWheelCount];

        physx::PxVec3 WheelCenterActorOffsets[VehicleWheelCount];
        physx::PxVec3 WheelCenterCMOffsets[VehicleWheelCount];
        physx::PxVec3 SuspensionTravelDirections[VehicleWheelCount];
        physx::PxVec3 SuspensionForceAppCMOffsets[VehicleWheelCount];
        physx::PxVec3 TireForceAppCMOffsets[VehicleWheelCount];

        const physx::PxVec3 ChassisCMOffset = ToPxVec3(Desc.ChassisCMOffset);

        for (uint32 i = 0; i < VehicleWheelCount; ++i)
        {
            const FVehicleWheelDesc& SrcWheel = Desc.Wheels[i];

            WheelData[i].mMass               = SrcWheel.Mass;
            WheelData[i].mRadius             = SrcWheel.Radius;
            WheelData[i].mWidth              = SrcWheel.Width;
            WheelData[i].mMOI                = SrcWheel.MOI > 0.0f ? SrcWheel.MOI : ComputeWheelMOI(SrcWheel.Mass, SrcWheel.Radius);
            WheelData[i].mMaxBrakeTorque     = SrcWheel.MaxBrakeTorque;
            WheelData[i].mMaxHandBrakeTorque = SrcWheel.MaxHandbrakeTorque;
            WheelData[i].mMaxSteer           = SrcWheel.MaxSteerRadians;

            TireData[i].mType = SrcWheel.TireType;

            WheelCenterActorOffsets[i]    = ToPxVec3(SrcWheel.LocalPosition);
            WheelCenterCMOffsets[i]       = WheelCenterActorOffsets[i] - ChassisCMOffset;
            SuspensionTravelDirections[i] = physx::PxVec3(0.0f, 0.0f, -1.0f);

            SuspensionForceAppCMOffsets[i] = physx::PxVec3(WheelCenterCMOffsets[i].x, WheelCenterCMOffsets[i].y, -0.3f);
            TireForceAppCMOffsets[i]       = SuspensionForceAppCMOffsets[i];
        }

        physx::PxF32 SprungMasses[VehicleWheelCount];
        physx::PxVehicleComputeSprungMasses(VehicleWheelCount, WheelCenterActorOffsets, ChassisCMOffset, Desc.ChassisMass, 2, SprungMasses);

        for (uint32 i = 0; i < VehicleWheelCount; ++i)
        {
            const FVehicleWheelDesc& SrcWheel = Desc.Wheels[i];

            SuspensionData[i].mMaxCompression   = SrcWheel.SuspensionMaxCompression;
            SuspensionData[i].mMaxDroop         = SrcWheel.SuspensionMaxDroop;
            SuspensionData[i].mSpringStrength   = SrcWheel.SuspensionSpringStrength;
            SuspensionData[i].mSpringDamperRate = SrcWheel.SuspensionSpringDamperRate;
            SuspensionData[i].mSprungMass       = SprungMasses[i];

            SuspensionData[i].mCamberAtRest           = 0.0f;
            SuspensionData[i].mCamberAtMaxCompression = -0.01f;
            SuspensionData[i].mCamberAtMaxDroop       = 0.01f;
        }

        const uint32 DrivableMask = Desc.DrivableSurfaceMask != 0
                                        ? Desc.DrivableSurfaceMask
                                        : ObjectTypeBit(ECollisionChannel::WorldStatic);

        physx::PxFilterData QueryFilterData;
        QueryFilterData.word0 = DrivableMask;
        QueryFilterData.word1 = 0;
        QueryFilterData.word2 = 0;
        QueryFilterData.word3 = Desc.Owner.ActorId;

        for (uint32 i = 0; i < VehicleWheelCount; ++i)
        {
            WheelsSimData.setWheelData(i, WheelData[i]);
            WheelsSimData.setTireData(i, TireData[i]);
            WheelsSimData.setSuspensionData(i, SuspensionData[i]);

            WheelsSimData.setWheelCentreOffset(i, WheelCenterCMOffsets[i]);
            WheelsSimData.setSuspTravelDirection(i, SuspensionTravelDirections[i]);
            WheelsSimData.setSuspForceAppPointOffset(i, SuspensionForceAppCMOffsets[i]);
            WheelsSimData.setTireForceAppPointOffset(i, TireForceAppCMOffsets[i]);
            WheelsSimData.setSceneQueryFilterData(i, QueryFilterData);

            WheelsSimData.setWheelShapeMapping(i, -1);
        }
    }

    physx::PxVehicleDriveSimData4W CreateDriveSimData4W(const FVehicleDesc& Desc)
    {
        physx::PxVehicleDriveSimData4W DriveData;

        physx::PxVehicleDifferential4WData DiffData;
        if (Desc.bFrontWheelDrive && Desc.bRearWheelDrive)
        {
            DiffData.mType = physx::PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
        }
        else if (Desc.bFrontWheelDrive)
        {
            DiffData.mType = physx::PxVehicleDifferential4WData::eDIFF_TYPE_LS_FRONTWD;
        }
        else
        {
            DiffData.mType = physx::PxVehicleDifferential4WData::eDIFF_TYPE_LS_REARWD;
        }
        DriveData.setDiffData(DiffData);

        physx::PxVehicleEngineData EngineData;
        EngineData.mPeakTorque = Desc.EnginePeakTorque;
        EngineData.mMaxOmega   = Desc.EngineMaxOmega;
        DriveData.setEngineData(EngineData);

        physx::PxVehicleGearsData GearsData;
        DriveData.setGearsData(GearsData);

        physx::PxVehicleClutchData ClutchData;
        ClutchData.mStrength = Desc.ClutchStrength;
        DriveData.setClutchData(ClutchData);

        const FVector& FL = Desc.Wheels[static_cast<int32>(EDrive4WWheelSlot::FrontLeft)].LocalPosition;
        const FVector& FR = Desc.Wheels[static_cast<int32>(EDrive4WWheelSlot::FrontRight)].LocalPosition;
        const FVector& RL = Desc.Wheels[static_cast<int32>(EDrive4WWheelSlot::RearLeft)].LocalPosition;
        const FVector& RR = Desc.Wheels[static_cast<int32>(EDrive4WWheelSlot::RearRight)].LocalPosition;

        physx::PxVehicleAckermannGeometryData AckermannData;
        AckermannData.mAccuracy       = 1.0f;
        AckermannData.mAxleSeparation = std::abs(FL.X - RL.X);
        AckermannData.mFrontWidth     = std::abs(FL.Y - FR.Y);
        AckermannData.mRearWidth      = std::abs(RL.Y - RR.Y);
        DriveData.setAckermannGeometryData(AckermannData);
        return DriveData;
    }

    physx::PxRigidDynamic* CreateVehicleChassisActor(physx::PxPhysics* Physics, physx::PxMaterial* Material, const FVehicleDesc& Desc)
    {
        if (!Physics || !Material)
        {
            return nullptr;
        }

        physx::PxRigidDynamic* Actor = Physics->createRigidDynamic(ToPxTransform(Desc.WorldTransform));
        if (!Actor)
        {
            return nullptr;
        }

        physx::PxShape* ChassisShape = Physics->createShape(physx::PxBoxGeometry(ToPxVec3(Desc.ChassisHalfExtents)), *Material);
        if (!ChassisShape)
        {
            Actor->release();
            return nullptr;
        }

        physx::PxTransform ChassisShapeLocalPose(ToPxVec3(Desc.ChassisShapeLocalOffset), physx::PxQuat(physx::PxIdentity));
        ChassisShape->setLocalPose(ChassisShapeLocalPose);

        physx::PxFilterData FilterData;
        FilterData.word0 = static_cast<uint32>(ECollisionChannel::Pawn) | PhysicsFilter_QueryAndPhysics;
        FilterData.word1 = 0xFFFFFFFFu;
        FilterData.word2 = 0;
        FilterData.word3 = Desc.Owner.ActorId;

        ChassisShape->setSimulationFilterData(FilterData);
        ChassisShape->setQueryFilterData(FilterData);

        Actor->attachShape(*ChassisShape);
        ChassisShape->release();

        const physx::PxVec3 LocalCOM = ToPxVec3(Desc.ChassisCMOffset);
        physx::PxRigidBodyExt::setMassAndUpdateInertia(*Actor, Desc.ChassisMass, &LocalCOM);
        Actor->setCMassLocalPose(physx::PxTransform(LocalCOM));

        Actor->setLinearDamping(0.05f);
        Actor->setAngularDamping(0.05f);
        Actor->setMaxAngularVelocity(100.0f);
        Actor->setSolverIterationCounts(8, 2);

        return Actor;
    }

    physx::PxBatchQuery* CreateVehicleBatchQuery(physx::PxScene* Scene, FPhysXVehicleInstance& Instance)
    {
        if (!Scene)
        {
            return nullptr;
        }

        Instance.RaycastResults.resize(VehicleWheelCount);
        Instance.RaycastHitBuffer.resize(VehicleWheelCount);
        Instance.WheelQueryResults.resize(VehicleWheelCount);

        physx::PxBatchQueryDesc Desc(VehicleWheelCount, 0, 0);
        Desc.queryMemory.userRaycastResultBuffer = Instance.RaycastResults.data();
        Desc.queryMemory.userRaycastTouchBuffer  = Instance.RaycastHitBuffer.data();
        Desc.queryMemory.raycastTouchBufferSize  = VehicleWheelCount;
        Desc.preFilterShader                     = VehicleRaycastPreFilter;

        return Scene->createBatchQuery(Desc);
    }

    physx::PxFixedSizeLookupTable<8> CreateSteerVsForwardSpeedTable()
    {
        physx::PxF32 Data[16] = { 0.0f, 0.75f, 5.0f, 0.75f, 30.0f, 0.125f, 120.0f, 0.10f, PX_MAX_F32, PX_MAX_F32, PX_MAX_F32, PX_MAX_F32, PX_MAX_F32, PX_MAX_F32, PX_MAX_F32, PX_MAX_F32 };
        return physx::PxFixedSizeLookupTable<8>(Data, 4);
    }

    const physx::PxVehiclePadSmoothingData GVehiclePadSmoothingData = { { 6.0f, 6.0f, 12.0f, 2.5f, 2.5f }, { 10.0f, 10.0f, 12.0f, 5.0f, 5.0f } };
}

void FPhysXVehicleRuntime::Initialize(physx::PxPhysics* InPhysics, physx::PxScene* InScene, physx::PxMaterial* InDefaultMaterial)
{
    Physics = InPhysics;
    Scene = InScene;
    DefaultMaterial = InDefaultMaterial;
}

void FPhysXVehicleRuntime::Shutdown()
{
    for (auto& VehiclePtr : Vehicles)
    {
        if (!VehiclePtr)
        {
            continue;
        }

        if (VehiclePtr->FrictionPairs)
        {
            VehiclePtr->FrictionPairs->release();
            VehiclePtr->FrictionPairs = nullptr;
        }

        if (VehiclePtr->BatchQuery)
        {
            VehiclePtr->BatchQuery->release();
            VehiclePtr->BatchQuery = nullptr;
        }

        if (VehiclePtr->Vehicle)
        {
            VehiclePtr->Vehicle->free();
            VehiclePtr->Vehicle = nullptr;
        }

        if (VehiclePtr->ChassisActor)
        {
            if (Scene && VehiclePtr->bChassisActorRegisteredInScene)
            {
                Scene->removeActor(*VehiclePtr->ChassisActor);
                VehiclePtr->bChassisActorRegisteredInScene = false;
            }

            VehiclePtr->ChassisActor->release();
            VehiclePtr->ChassisActor = nullptr;
        }
    }

    Vehicles.clear();
    VehicleGenerations.clear();
    ComponentToVehicle.clear();
    Physics = nullptr;
    Scene = nullptr;
    DefaultMaterial = nullptr;
}

FVehicleHandle FPhysXVehicleRuntime::ReserveHandle()
{
    return AllocateVehicle();
}

void FPhysXVehicleRuntime::GatherStats(FPhysicsStats& Stats) const
{
    for (const auto& VehiclePtr : Vehicles)
    {
        const FPhysXVehicleInstance* Vehicle = VehiclePtr.get();
        if (!Vehicle || !Vehicle->Vehicle)
        {
            continue;
        }

        ++Stats.NumVehicles;
        Stats.NumVehicleWheels += static_cast<int32>(VehicleWheelCount);

        for (uint32 WheelIndex = 0; WheelIndex < VehicleWheelCount && WheelIndex < Vehicle->WheelQueryResults.size(); ++WheelIndex)
        {
            if (Vehicle->WheelQueryResults[WheelIndex].isInAir)
            {
                ++Stats.NumVehicleWheelInAir;
            }
        }
    }
}

void FPhysXVehicleRuntime::BuildSnapshots(TArray<FVehicleSnapshot>& OutVehicles) const
{
    OutVehicles.clear();

    for (const auto& VehiclePtr : Vehicles)
    {
        const FPhysXVehicleInstance* Instance = VehiclePtr.get();
        if (!Instance || !Instance->Vehicle || !Instance->ChassisActor)
        {
            continue;
        }

        FVehicleSnapshot Snapshot;
        Snapshot.Vehicle = Instance->Handle;
        Snapshot.Wheels.resize(VehicleWheelCount);
        Snapshot.OwnerActorId = Instance->OwnerActorId;
        Snapshot.OwnerComponentId = Instance->OwnerComponentId;
        Snapshot.OwnerComponentGeneration = Instance->OwnerComponentGeneration;

        const physx::PxTransform ChassisPose = Instance->ChassisActor->getGlobalPose();
        const FTransform SimulationChassisWorldTransform = ToFTransform(ChassisPose);

        FQuat VisualChassisWorldRotation = SimulationChassisWorldTransform.Rotation * Instance->Desc.VisualToSimulationRotation;
        VisualChassisWorldRotation.Normalize();
        Snapshot.ChassisWorldTransform = FTransform(SimulationChassisWorldTransform.Location, VisualChassisWorldRotation, FVector::OneVector);
        Snapshot.LinearVelocity = ToFVector(Instance->ChassisActor->getLinearVelocity());
        Snapshot.AngularVelocity = ToFVector(Instance->ChassisActor->getAngularVelocity());

        for (uint32 WheelIndex = 0; WheelIndex < VehicleWheelCount; ++WheelIndex)
        {
            const FVehicleWheelDesc& WheelDesc = Instance->Desc.Wheels[WheelIndex];

            float SteerAngle = 0.0f;
            float RotationAngle = 0.0f;
            float SuspensionJounce = 0.0f;
            bool bInAir = true;
            FVector ContactPoint = FVector::ZeroVector;
            FVector ContactNormal = FVector::UpVector;

            const physx::PxVec3 LocalWheelCenter = ToPxVec3(WheelDesc.LocalPosition);
            const physx::PxVec3 LocalSuspensionDirection(0.0f, 0.0f, -1.0f);
            physx::PxTransform LocalWheelPose(LocalWheelCenter, physx::PxQuat(physx::PxIdentity));

            if (WheelIndex < Instance->WheelQueryResults.size())
            {
                const physx::PxWheelQueryResult& WheelQuery = Instance->WheelQueryResults[WheelIndex];
                SteerAngle = WheelQuery.steerAngle;
                RotationAngle = Instance->Vehicle->mWheelsDynData.getWheelRotationAngle(WheelIndex);
                SuspensionJounce = WheelQuery.suspJounce;
                bInAir = WheelQuery.isInAir;
                ContactPoint = ToFVector(WheelQuery.tireContactPoint);
                ContactNormal = ToFVector(WheelQuery.tireContactNormal);
                LocalWheelPose = WheelQuery.localPose;
            }
            physx::PxVec3 LocalWheelPosition = LocalWheelCenter;
            if (WheelIndex < Instance->WheelQueryResults.size())
            {
                LocalWheelPosition = LocalWheelPose.p;
            }
            else
            {
                LocalWheelPosition = LocalWheelCenter + LocalSuspensionDirection * SuspensionJounce;
            }

            const physx::PxQuat SteerRotation(SteerAngle, physx::PxVec3(0.0f, 0.0f, 1.0f));
            const physx::PxQuat SpinRotation(RotationAngle, physx::PxVec3(0.0f, 1.0f, 0.0f));
            LocalWheelPose = physx::PxTransform(LocalWheelPosition, SteerRotation * SpinRotation);

            const FVector VisualRestLocalPosition = Instance->Desc.VisualToSimulationRotation.Inverse().RotateVector(WheelDesc.LocalPosition);
            const FVector VisualCurrentLocalPosition = Instance->Desc.VisualToSimulationRotation.Inverse().RotateVector(ToFVector(LocalWheelPosition));
            const FVector VisualWheelWorldPosition = Snapshot.ChassisWorldTransform.Location + Snapshot.ChassisWorldTransform.Rotation.RotateVector(VisualCurrentLocalPosition);

            const FQuat VisualWheelWorldRotation = Snapshot.ChassisWorldTransform.Rotation;

            Snapshot.Wheels[WheelIndex].WheelName = WheelDesc.WheelName;
            Snapshot.Wheels[WheelIndex].WorldTransform = FTransform(VisualWheelWorldPosition, VisualWheelWorldRotation, FVector::OneVector);
            Snapshot.Wheels[WheelIndex].RestLocalPosition = VisualRestLocalPosition;
            Snapshot.Wheels[WheelIndex].CurrentLocalPosition = VisualCurrentLocalPosition;
            Snapshot.Wheels[WheelIndex].SteerAngle = SteerAngle;
            Snapshot.Wheels[WheelIndex].RotationAngle = RotationAngle;
            Snapshot.Wheels[WheelIndex].SuspensionJounce = SuspensionJounce;
            Snapshot.Wheels[WheelIndex].bInAir = bInAir;
            Snapshot.Wheels[WheelIndex].ContactPoint = ContactPoint;
            Snapshot.Wheels[WheelIndex].ContactNormal = ContactNormal;
        }

        OutVehicles.push_back(Snapshot);
    }
}

FVehicleHandle FPhysXVehicleRuntime::AllocateVehicle()
{
    for (uint32 i = 0; i < static_cast<uint32>(Vehicles.size()); ++i)
    {
        if (!Vehicles[i])
        {
            const uint32 NewGeneration = ++VehicleGenerations[i];
            if (NewGeneration == 0)
            {
                ++VehicleGenerations[i];
            }

            FVehicleHandle Handle;
            Handle.Index        = i;
            Handle.Generation   = VehicleGenerations[i];
            Vehicles[i]         = std::make_unique<FPhysXVehicleInstance>();
            Vehicles[i]->Handle = Handle;
            return Handle;
        }
    }

    FVehicleHandle Handle;
    Handle.Index      = static_cast<uint32>(Vehicles.size());
    Handle.Generation = 1;

    VehicleGenerations.push_back(Handle.Generation);

    auto Instance     = std::make_unique<FPhysXVehicleInstance>();
    Instance->Handle  = Handle;
    Vehicles.push_back(std::move(Instance));
    return Handle;
}

FPhysXVehicleInstance* FPhysXVehicleRuntime::ResolveVehicle(FVehicleHandle Handle)
{
    if (!IsVehicleHandleIndexValid(Handle.Index, Vehicles.size()) ||
        !IsVehicleHandleIndexValid(Handle.Index, VehicleGenerations.size()))
    {
        return nullptr;
    }

    if (VehicleGenerations[Handle.Index] != Handle.Generation)
    {
        return nullptr;
    }

    return Vehicles[Handle.Index].get();
}

const FPhysXVehicleInstance* FPhysXVehicleRuntime::ResolveVehicle(FVehicleHandle Handle) const
{
    if (!IsVehicleHandleIndexValid(Handle.Index, Vehicles.size()) ||
        !IsVehicleHandleIndexValid(Handle.Index, VehicleGenerations.size()))
    {
        return nullptr;
    }

    if (VehicleGenerations[Handle.Index] != Handle.Generation)
    {
        return nullptr;
    }

    return Vehicles[Handle.Index].get();
}

FPhysXVehicleInstance* FPhysXVehicleRuntime::ResolveAliveVehicle(FVehicleHandle Handle)
{
    FPhysXVehicleInstance* Instance = ResolveVehicle(Handle);
    return Instance && Instance->Vehicle && Instance->ChassisActor ? Instance : nullptr;
}

const FPhysXVehicleInstance* FPhysXVehicleRuntime::ResolveAliveVehicle(FVehicleHandle Handle) const
{
    const FPhysXVehicleInstance* Instance = ResolveVehicle(Handle);
    return Instance && Instance->Vehicle && Instance->ChassisActor ? Instance : nullptr;
}

void FPhysXVehicleRuntime::FreeVehicle(FVehicleHandle Handle)
{
    if (!IsVehicleHandleIndexValid(Handle.Index, Vehicles.size()) ||
        !IsVehicleHandleIndexValid(Handle.Index, VehicleGenerations.size()))
    {
        return;
    }

    Vehicles[Handle.Index].reset();
    ++VehicleGenerations[Handle.Index];
    if (VehicleGenerations[Handle.Index] == 0)
    {
        ++VehicleGenerations[Handle.Index];
    }
}

FVehicleHandle FPhysXVehicleRuntime::CreateVehicle(const FVehicleDesc& Desc)
{
    if (!Physics || !Scene || !DefaultMaterial)
    {
        return {};
    }

    if (Desc.Wheels.size() != VehicleWheelCount)
    {
        UE_LOG("[PhysXVehicle] Drive4W backend requires exactly 4 wheels. Received %zu.", Desc.Wheels.size());
        return {};
    }

    FVehicleHandle Handle = Desc.ReservedVehicle.IsValid() ? Desc.ReservedVehicle : AllocateVehicle();

    FPhysXVehicleInstance* Instance = ResolveVehicle(Handle);
    if (!Instance)
    {
        if (Desc.ReservedVehicle.IsValid())
        {
            return {};
        }

        Handle   = AllocateVehicle();
        Instance = ResolveVehicle(Handle);
        if (!Instance)
        {
            return {};
        }
    }

    Instance->Handle = Handle;
    Instance->Desc   = Desc;

    Instance->OwnerActorId             = Desc.Owner.ActorId;
    Instance->OwnerComponentId         = Desc.Owner.ComponentId;
    Instance->OwnerComponentGeneration = Desc.Owner.ComponentGeneration;

    Instance->ChassisActor = CreateVehicleChassisActor(Physics, DefaultMaterial, Desc);
    if (!Instance->ChassisActor)
    {
        FreeVehicle(Handle);
        return {};
    }

    physx::PxVehicleWheelsSimData* WheelsSimData = physx::PxVehicleWheelsSimData::allocate(VehicleWheelCount);
    if (!WheelsSimData)
    {
        Instance->ChassisActor->release();
        Instance->ChassisActor = nullptr;
        FreeVehicle(Handle);
        return {};
    }

    SetupVehicleWheelSimData(Desc, *WheelsSimData);
    physx::PxVehicleDriveSimData4W DriveSimData = CreateDriveSimData4W(Desc);

    Instance->Vehicle = physx::PxVehicleDrive4W::allocate(VehicleWheelCount);
    if (!Instance->Vehicle)
    {
        WheelsSimData->free();
        Instance->ChassisActor->release();
        Instance->ChassisActor = nullptr;
        FreeVehicle(Handle);
        return {};
    }

    Instance->Vehicle->setup(Physics, Instance->ChassisActor, *WheelsSimData, DriveSimData, 0);
    WheelsSimData->free();

    Instance->Vehicle->setToRestState();
    Instance->Vehicle->mDriveDynData.forceGearChange(physx::PxVehicleGearsData::eFIRST);
    Instance->Vehicle->mDriveDynData.setUseAutoGears(true);
    Instance->bReverseGearActive = false;

    Instance->BatchQuery    = CreateVehicleBatchQuery(Scene, *Instance);
    Instance->FrictionPairs = CreateVehicleFrictionPairs(DefaultMaterial, Desc.TireFriction);

    if (!Instance->BatchQuery || !Instance->FrictionPairs)
    {
        DestroyVehicle(Handle);
        return {};
    }

    Instance->VehicleQueryResult.nbWheelQueryResults = VehicleWheelCount;
    Instance->VehicleQueryResult.wheelQueryResults   = Instance->WheelQueryResults.data();

    Instance->PreviousChassisTransform = Desc.WorldTransform;
    Instance->CurrentChassisTransform  = Desc.WorldTransform;
    Instance->bPendingFirstRaycast     = true;

    Scene->addActor(*Instance->ChassisActor);
    Instance->bChassisActorRegisteredInScene = true;

    if (Instance->OwnerComponentId != 0)
    {
        ComponentToVehicle[Instance->OwnerComponentId] = Handle;
    }

    UE_LOG("[PhysXVehicle] Created Drive4W Vehicle(Index=%u Gen=%u Actor=%u Component=%u)", Handle.Index, Handle.Generation, Instance->OwnerActorId, Instance->OwnerComponentId);
    return Handle;
}

void FPhysXVehicleRuntime::DestroyVehicle(FVehicleHandle Vehicle)
{
    FPhysXVehicleInstance* Instance = ResolveVehicle(Vehicle);
    if (!Instance)
    {
        return;
    }

    for (auto It = ComponentToVehicle.begin(); It != ComponentToVehicle.end();)
    {
        if (It->second == Vehicle)
        {
            It = ComponentToVehicle.erase(It);
        }
        else
        {
            ++It;
        }
    }

    if (Instance->FrictionPairs)
    {
        Instance->FrictionPairs->release();
        Instance->FrictionPairs = nullptr;
    }

    if (Instance->BatchQuery)
    {
        Instance->BatchQuery->release();
        Instance->BatchQuery = nullptr;
    }

    if (Instance->Vehicle)
    {
        Instance->Vehicle->free();
        Instance->Vehicle = nullptr;
    }

    if (Instance->ChassisActor)
    {
        if (Scene && Instance->bChassisActorRegisteredInScene)
        {
            Scene->removeActor(*Instance->ChassisActor);
            Instance->bChassisActorRegisteredInScene = false;
        }

        Instance->ChassisActor->release();
        Instance->ChassisActor = nullptr;
    }

    FreeVehicle(Vehicle);
}

void FPhysXVehicleRuntime::SetVehicleInput(FVehicleHandle Vehicle, const FVehicleInputState& Input)
{
    FPhysXVehicleInstance* Instance = ResolveAliveVehicle(Vehicle);
    if (!Instance)
    {
        return;
    }

    Instance->Input.Throttle  = std::clamp(Input.Throttle, -1.0f, 1.0f);
    Instance->Input.Brake     = std::clamp(Input.Brake, 0.0f, 1.0f);
    Instance->Input.Steering  = std::clamp(Input.Steering, -1.0f, 1.0f);
    Instance->Input.Handbrake = std::clamp(Input.Handbrake, 0.0f, 1.0f);
}

void FPhysXVehicleRuntime::ResetVehicle(FVehicleHandle Vehicle, const FTransform& WorldTransform)
{
    FPhysXVehicleInstance* Instance = ResolveAliveVehicle(Vehicle);
    if (!Instance)
    {
        return;
    }

    Instance->ChassisActor->setGlobalPose(ToPxTransform(WorldTransform));
    Instance->ChassisActor->setLinearVelocity(physx::PxVec3(0.0f));
    Instance->ChassisActor->setAngularVelocity(physx::PxVec3(0.0f));

    Instance->Vehicle->setToRestState();
    Instance->Vehicle->mDriveDynData.setUseAutoGears(true);
    Instance->Vehicle->mDriveDynData.forceGearChange(physx::PxVehicleGearsData::eFIRST);
    Instance->bReverseGearActive = false;

    Instance->PreviousChassisTransform = WorldTransform;
    Instance->CurrentChassisTransform  = WorldTransform;
    Instance->bPendingFirstRaycast     = true;
}

void FPhysXVehicleRuntime::PreSimulate(float InFixedDt)
{
    LastRaycastMs = 0.0f;

    if (Vehicles.empty())
    {
        return;
    }

    using FClock = std::chrono::high_resolution_clock;
    const auto DurationMs = [](FClock::time_point A, FClock::time_point B)
    {
        return std::chrono::duration<float, std::milli>(B - A).count();
    };

    physx::PxFixedSizeLookupTable<8> SteerTable = CreateSteerVsForwardSpeedTable();

    for (auto& VehiclePtr : Vehicles)
    {
        FPhysXVehicleInstance* Instance = VehiclePtr.get();
        if (!Instance || !Instance->Vehicle || !Instance->BatchQuery || !Instance->FrictionPairs)
        {
            continue;
        }

        const float SignedThrottle = std::clamp(Instance->Input.Throttle, -1.0f, 1.0f);
        const float ThrottleAbs    = std::abs(SignedThrottle);
        const float DeadZone       = 0.01f;
        const float SwitchSpeed    = std::max(0.0f, Instance->Desc.ReverseGearSwitchSpeed);
        const bool  bReverseEnabled = Instance->Desc.bEnableReverseGear;

        const physx::PxTransform ChassisPose = Instance->ChassisActor->getGlobalPose();
        const physx::PxVec3 ForwardAxis = ChassisPose.q.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f));
        const float ForwardSpeed = Instance->ChassisActor->getLinearVelocity().dot(ForwardAxis);

        float AnalogAccel = 0.0f;
        float AnalogBrake = std::clamp(Instance->Input.Brake, 0.0f, 1.0f);

        auto SetReverseGearActive = [Instance](bool bReverse)
        {
            if (Instance->bReverseGearActive == bReverse)
            {
                return;
            }

            Instance->Vehicle->mDriveDynData.setUseAutoGears(!bReverse);
            Instance->Vehicle->mDriveDynData.forceGearChange(
                bReverse ? physx::PxVehicleGearsData::eREVERSE : physx::PxVehicleGearsData::eFIRST);
            Instance->bReverseGearActive = bReverse;
        };

        if (SignedThrottle < -DeadZone)
        {
            if (!bReverseEnabled)
            {
                SetReverseGearActive(false);
                AnalogBrake = std::max(AnalogBrake, ThrottleAbs);
            }
            else if (ForwardSpeed > SwitchSpeed)
            {
                SetReverseGearActive(false);
                AnalogBrake = std::max(AnalogBrake, ThrottleAbs);
            }
            else
            {
                SetReverseGearActive(true);
                AnalogAccel = ThrottleAbs;
            }
        }
        else if (SignedThrottle > DeadZone)
        {
            if (ForwardSpeed < -SwitchSpeed)
            {
                AnalogBrake = std::max(AnalogBrake, ThrottleAbs);
            }
            else
            {
                SetReverseGearActive(false);
                AnalogAccel = SignedThrottle;
            }
        }
        else if (!bReverseEnabled && Instance->bReverseGearActive)
        {
            SetReverseGearActive(false);
        }

        Instance->RawInput.setAnalogAccel(AnalogAccel);
        Instance->RawInput.setAnalogBrake(AnalogBrake);
        Instance->RawInput.setAnalogSteer(Instance->Input.Steering);
        Instance->RawInput.setAnalogHandbrake(Instance->Input.Handbrake);

        const bool bVehicleInAir = !Instance->bPendingFirstRaycast && physx::PxVehicleIsInAir(Instance->VehicleQueryResult);
        physx::PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs(GVehiclePadSmoothingData, SteerTable, Instance->RawInput, InFixedDt, bVehicleInAir, *Instance->Vehicle);

        physx::PxVehicleWheels* VehicleWheels[1] = { Instance->Vehicle };
        const auto RaycastStart = FClock::now();
        physx::PxVehicleSuspensionRaycasts(Instance->BatchQuery, 1, VehicleWheels, VehicleWheelCount, Instance->RaycastResults.data());
        LastRaycastMs += DurationMs(RaycastStart, FClock::now());

        Instance->bPendingFirstRaycast = false;

        const physx::PxVec3 Gravity = Scene ? Scene->getGravity() : physx::PxVec3(0.0f, 0.0f, -9.81f);
        physx::PxVehicleUpdates(InFixedDt, Gravity, *Instance->FrictionPairs, 1, VehicleWheels, &Instance->VehicleQueryResult);
    }
}
