#include "Particle/ParticleSystemManager.h"

#include "Asset/AssetPackage.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshManager.h"
#include "Object/ObjectFactory.h"
#include "Particle/BeamModule/ParticleModuleBeamNoise.h"
#include "Particle/BeamModule/ParticleModuleBeamSource.h"
#include "Particle/BeamModule/ParticleModuleBeamTarget.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"
#include "Particle/ParticleSpriteEmitter.h"
#include "Particle/ParticleSystem.h"
#include "Particle/TypeData/ParticleModuleTypeDataBeam2.h"
#include "Particle/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Platform/Paths.h"
#include "Runtime/Engine.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
constexpr int32 ParticleSystemVersion = 4;

namespace ParticleKeys
{
	static constexpr const char* Version = "Version";
	static constexpr const char* Emitters = "Emitters";
	static constexpr const char* LODDistances = "LODDistances";
	static constexpr const char* Name = "Name";
	static constexpr const char* InitialAllocationCount = "InitialAllocationCount";
	static constexpr const char* PeakActiveParticles = "PeakActiveParticles";
	static constexpr const char* LODLevels = "LODLevels";
	static constexpr const char* Level = "Level";
	static constexpr const char* bEnabled = "bEnabled";
	static constexpr const char* Required = "Required";
	static constexpr const char* Spawn = "Spawn";
	static constexpr const char* TypeData = "TypeData";
	static constexpr const char* Modules = "Modules";
	static constexpr const char* Type = "Type";
	static constexpr const char* Material = "Material";
	static constexpr const char* EmitterOrigin = "EmitterOrigin";
	static constexpr const char* ScreenAlignment = "ScreenAlignment";
	static constexpr const char* SubImagesHorizontal = "SubImages_Horizontal";
	static constexpr const char* SubImagesVertical = "SubImages_Vertical";
	static constexpr const char* AlphaSource = "AlphaSource";
	static constexpr const char* AlphaThreshold = "AlphaThreshold";
	static constexpr const char* AlphaPower = "AlphaPower";
	static constexpr const char* ColorIntensity = "ColorIntensity";
	static constexpr const char* SortMode = "SortMode";
	static constexpr const char* EmitterDuration = "EmitterDuration";
	static constexpr const char* MaxDrawCount = "MaxDrawCount";
	static constexpr const char* bUseLocalSpace = "bUseLocalSpace";
	static constexpr const char* bKillOnDeactivate = "bKillOnDeactivate";
	static constexpr const char* bKillOnCompleted = "bKillOnCompleted";
	static constexpr const char* Rate = "Rate";
	static constexpr const char* RateDistribution = "RateDistribution";
	static constexpr const char* BurstList = "BurstList";
	static constexpr const char* Count = "Count";
	static constexpr const char* CountLow = "CountLow";
	static constexpr const char* Time = "Time";
	static constexpr const char* ParticleBurstMethod = "ParticleBurstMethod";
	static constexpr const char* Lifetime = "Lifetime";
	static constexpr const char* LifetimeMin = "LifetimeMin";
	static constexpr const char* LifetimeMax = "LifetimeMax";
	static constexpr const char* LifetimeDistribution = "LifetimeDistribution";
	static constexpr const char* StartLocation = "StartLocation";
	static constexpr const char* StartLocationMin = "StartLocationMin";
	static constexpr const char* StartLocationMax = "StartLocationMax";
	static constexpr const char* StartLocationDistribution = "StartLocationDistribution";
	static constexpr const char* StartVelocity = "StartVelocity";
	static constexpr const char* StartVelocityMin = "StartVelocityMin";
	static constexpr const char* StartVelocityMax = "StartVelocityMax";
	static constexpr const char* StartVelocityDistribution = "StartVelocityDistribution";
	static constexpr const char* StartRotation = "StartRotation";
	static constexpr const char* StartRotationMin = "StartRotationMin";
	static constexpr const char* StartRotationMax = "StartRotationMax";
	static constexpr const char* StartRotationDistribution = "StartRotationDistribution";
	static constexpr const char* StartRotationRate = "StartRotationRate";
	static constexpr const char* StartRotationRateMin = "StartRotationRateMin";
	static constexpr const char* StartRotationRateMax = "StartRotationRateMax";
	static constexpr const char* StartRotationRateDistribution = "StartRotationRateDistribution";
	static constexpr const char* Acceleration = "Acceleration";
	static constexpr const char* AccelerationDistribution = "AccelerationDistribution";
	static constexpr const char* Orbit = "Orbit";
	static constexpr const char* OrbitOffset = "OrbitOffset";
	static constexpr const char* OrbitOffsetDistribution = "OrbitOffsetDistribution";
	static constexpr const char* OrbitRotation = "OrbitRotation";
	static constexpr const char* OrbitRotationDistribution = "OrbitRotationDistribution";
	static constexpr const char* OrbitRotationRate = "OrbitRotationRate";
	static constexpr const char* OrbitRotationRateDistribution = "OrbitRotationRateDistribution";
	static constexpr const char* StartColor = "StartColor";
	static constexpr const char* StartColorMin = "StartColorMin";
	static constexpr const char* StartColorMax = "StartColorMax";
	static constexpr const char* StartColorDistribution = "StartColorDistribution";
	static constexpr const char* StartAlpha = "StartAlpha";
	static constexpr const char* StartAlphaMin = "StartAlphaMin";
	static constexpr const char* StartAlphaMax = "StartAlphaMax";
	static constexpr const char* StartAlphaDistribution = "StartAlphaDistribution";
	static constexpr const char* EndColor = "EndColor";
	static constexpr const char* EndAlpha = "EndAlpha";
	static constexpr const char* ColorOverLifeDistribution = "ColorOverLifeDistribution";
	static constexpr const char* AlphaOverLifeDistribution = "AlphaOverLifeDistribution";
	static constexpr const char* ColorScaleOverLife = "ColorScaleOverLife";
	static constexpr const char* AlphaScaleOverLife = "AlphaScaleOverLife";
	static constexpr const char* ColorScaleOverLifeDistribution = "ColorScaleOverLifeDistribution";
	static constexpr const char* AlphaScaleOverLifeDistribution = "AlphaScaleOverLifeDistribution";
	static constexpr const char* StartSize = "StartSize";
	static constexpr const char* StartSizeMin = "StartSizeMin";
	static constexpr const char* StartSizeMax = "StartSizeMax";
	static constexpr const char* StartSizeDistribution = "StartSizeDistribution";
	static constexpr const char* Mode = "Mode";
	static constexpr const char* Constant = "Constant";
	static constexpr const char* Min = "Min";
	static constexpr const char* Max = "Max";
	static constexpr const char* ConstantCurve = "ConstantCurve";
	static constexpr const char* MinCurve = "MinCurve";
	static constexpr const char* MaxCurve = "MaxCurve";
	static constexpr const char* Keys = "Keys";
	static constexpr const char* Value = "Value";
	static constexpr const char* InterpMode = "InterpMode";
	static constexpr const char* X = "X";
	static constexpr const char* Y = "Y";
	static constexpr const char* Z = "Z";
	static constexpr const char* Collision = "Collision";
	static constexpr const char* EventGenerator = "EventGenerator";
	static constexpr const char* EventReceiverSpawn = "EventReceiverSpawn";
	static constexpr const char* Events = "Events";
	static constexpr const char* EventType = "EventType";
	static constexpr const char* EventGeneratorType = "EventGeneratorType";
	static constexpr const char* CustomName = "CustomName";
	static constexpr const char* EventName = "EventName";
	static constexpr const char* SpawnCount = "SpawnCount";
	static constexpr const char* bSpawnOnlyOnEvent = "bSpawnOnlyOnEvent";
	static constexpr const char* SpawnLocationOffset = "SpawnLocationOffset";
	static constexpr const char* bInheritEventVelocity = "bInheritEventVelocity";
	static constexpr const char* EventVelocityScale = "EventVelocityScale";
	static constexpr const char* ParticleFrequency = "ParticleFrequency";
	static constexpr const char* FirstTimeOnly = "FirstTimeOnly";
	static constexpr const char* LastTimeOnly = "LastTimeOnly";
	static constexpr const char* UseReflectedImpactVector = "UseReflectedImpactVector";
	static constexpr const char* bUseOrbitOffset = "bUseOrbitOffset";
	static constexpr const char* TraceChannel = "TraceChannel";
	static constexpr const char* ResponseMode = "ResponseMode";
	static constexpr const char* DampingFactor = "DampingFactor";
	static constexpr const char* CollisionOffset = "CollisionOffset";
	static constexpr const char* CollisionRadiusScale = "CollisionRadiusScale";
	static constexpr const char* MaxCollisions = "MaxCollisions";
	static constexpr const char* Beam2 = "Beam2";
	static constexpr const char* BeamMethod = "BeamMethod";
	static constexpr const char* InterpolationPoints = "InterpolationPoints";
	static constexpr const char* Sheets = "Sheets";
	static constexpr const char* MaxBeamCount = "MaxBeamCount";
	static constexpr const char* Speed = "Speed";
	static constexpr const char* bAlwaysOn = "bAlwaysOn";
	static constexpr const char* UpVectorStepSize = "UpVectorStepSize";
	static constexpr const char* Distance = "Distance";
	static constexpr const char* SourcePoint = "SourcePoint";
	static constexpr const char* TargetPoint = "TargetPoint";
	static constexpr const char* SourceTangentMethod = "SourceTangentMethod";
	static constexpr const char* SourceTangent = "SourceTangent";
	static constexpr const char* TargetTangentMethod = "TargetTangentMethod";
	static constexpr const char* TargetTangent = "TargetTangent";
	static constexpr const char* Width = "Width";
	static constexpr const char* TextureTile = "TextureTile";
	static constexpr const char* TextureTileDistance = "TextureTileDistance";
	static constexpr const char* Color = "Color";
	static constexpr const char* Alpha = "Alpha";
	static constexpr const char* BranchParentName = "BranchParentName";
	static constexpr const char* TaperMethod = "TaperMethod";
	static constexpr const char* TaperFactor = "TaperFactor";
	static constexpr const char* TaperScale = "TaperScale";
	static constexpr const char* bRenderGeometry = "bRenderGeometry";
	static constexpr const char* bRenderDirectLine = "bRenderDirectLine";
	static constexpr const char* bRenderLines = "bRenderLines";
	static constexpr const char* bRenderTessellation = "bRenderTessellation";
	static constexpr const char* TargetData = "TargetData";
	static constexpr const char* TargetName = "TargetName";
	static constexpr const char* TargetPercentage = "TargetPercentage";
	static constexpr const char* SourceMethod = "SourceMethod";
	static constexpr const char* SourceName = "SourceName";
	static constexpr const char* bSourceAbsolute = "bSourceAbsolute";
	static constexpr const char* bLockSource = "bLockSource";
	static constexpr const char* Source = "Source";
	static constexpr const char* bLockSourceTangent = "bLockSourceTangent";
	static constexpr const char* SourceStrength = "SourceStrength";
	static constexpr const char* TargetMethod = "TargetMethod";
	static constexpr const char* bTargetAbsolute = "bTargetAbsolute";
	static constexpr const char* bLockTarget = "bLockTarget";
	static constexpr const char* Target = "Target";
	static constexpr const char* bLockTargetTangent = "bLockTargetTangent";
	static constexpr const char* TargetStrength = "TargetStrength";
	static constexpr const char* Frequency = "Frequency";
	static constexpr const char* NoiseFrequency = "NoiseFrequency";
	static constexpr const char* FrequencyDistance = "FrequencyDistance";
	static constexpr const char* NoiseRange = "NoiseRange";
	static constexpr const char* NoiseRangeMin = "NoiseRangeMin";
	static constexpr const char* NoiseRangeMax = "NoiseRangeMax";
	static constexpr const char* NoiseAmplitude = "NoiseAmplitude";
	static constexpr const char* NoiseSpeed = "NoiseSpeed";
	static constexpr const char* NoiseLockTime = "NoiseLockTime";
	static constexpr const char* bTargetNoise = "bTargetNoise";
	static constexpr const char* Mesh = "Mesh";
	static constexpr const char* MeshPath = "MeshPath";
	static constexpr const char* Ribbon = "Ribbon";
	static constexpr const char* MaxTessellationBetweenParticles = "MaxTessellationBetweenParticles";
	static constexpr const char* SheetsPerTrail = "SheetsPerTrail";
	static constexpr const char* MaxTrailCount = "MaxTrailCount";
	static constexpr const char* MaxParticleInTrailCount = "MaxParticleInTrailCount";
	static constexpr const char* bDeadTrailsOnDeactivate = "bDeadTrailsOnDeactivate";
	static constexpr const char* bDeadTrailsOnSourceLoss = "bDeadTrailsOnSourceLoss";
	static constexpr const char* bClipSourceSegment = "bClipSourceSegment";
	static constexpr const char* bEnablePreviousTangentRecalculation = "bEnablePreviousTangentRecalculation";
	static constexpr const char* bTangentRecalculationEveryFrame = "bTangentRecalculationEveryFrame";
	static constexpr const char* bSpawnInitialParticle = "bSpawnInitialParticle";
	static constexpr const char* RenderAxis = "RenderAxis";
	static constexpr const char* TangentSpawningScalar = "TangentSpawningScalar";
	static constexpr const char* bRenderSpawnPoints = "bRenderSpawnPoints";
	static constexpr const char* bRenderTangents = "bRenderTangents";
	static constexpr const char* TilingDistance = "TilingDistance";
	static constexpr const char* DistanceTessellationStepSize = "DistanceTessellationStepSize";
	static constexpr const char* bEnableTangentDiffInterpScale = "bEnableTangentDiffInterpScale";
	static constexpr const char* TangentTessellationScalar = "TangentTessellationScalar";
	static constexpr const char* bUseSourceEmitter = "bUseSourceEmitter";
	static constexpr const char* SourceEmitterName = "SourceEmitterName";
	static constexpr const char* SourceTrailLifetime = "SourceTrailLifetime";
	static constexpr const char* SourceSampleInterval = "SourceSampleInterval";
	static constexpr const char* SourceMinSampleDistance = "SourceMinSampleDistance";
	static constexpr const char* SourceWidthScale = "SourceWidthScale";
}

json::JSON MakeVectorJSON(const FVector& Value)
{
	return json::Array(Value.X, Value.Y, Value.Z);
}

FVector ReadVectorJSON(json::JSON& Object, const char* Key, const FVector& DefaultValue)
{
	if (!Object.hasKey(Key))
	{
		return DefaultValue;
	}

	json::JSON& Value = Object[Key];
	if (Value.JSONType() != json::JSON::Class::Array || Value.length() < 3)
	{
		return DefaultValue;
	}

	return FVector(
		static_cast<float>(Value[0].ToFloat()),
		static_cast<float>(Value[1].ToFloat()),
		static_cast<float>(Value[2].ToFloat()));
}

FVector ReadVectorOrScalarZJSON(json::JSON& Object, const char* Key, const FVector& DefaultValue)
{
	if (!Object.hasKey(Key))
	{
		return DefaultValue;
	}

	json::JSON& Value = Object[Key];
	if (Value.JSONType() == json::JSON::Class::Array && Value.length() >= 3)
	{
		return FVector(
			static_cast<float>(Value[0].ToFloat()),
			static_cast<float>(Value[1].ToFloat()),
			static_cast<float>(Value[2].ToFloat()));
	}

	if (Value.JSONType() == json::JSON::Class::Floating || Value.JSONType() == json::JSON::Class::Integral)
	{
		return FVector(DefaultValue.X, DefaultValue.Y, static_cast<float>(Value.ToFloat()));
	}

	return DefaultValue;
}

json::JSON MakeCurveJSON(const FFloatCurve& Curve)
{
	json::JSON Object = json::JSON::Make(json::JSON::Class::Object);
	Object[ParticleKeys::Value] = Curve.DefaultValue;
	json::JSON Keys = json::Array();
	for (const FCurveKey& Key : Curve.Keys)
	{
		json::JSON KeyObject = json::JSON::Make(json::JSON::Class::Object);
		KeyObject[ParticleKeys::Time] = Key.Time;
		KeyObject[ParticleKeys::Value] = Key.Value;
		KeyObject[ParticleKeys::InterpMode] = static_cast<int32>(Key.InterpMode);
		Keys.append(KeyObject);
	}
	Object[ParticleKeys::Keys] = Keys;
	return Object;
}

void ReadCurveJSON(json::JSON& Object, const char* Key, FFloatCurve& Curve)
{
	if (!Object.hasKey(Key))
	{
		return;
	}

	json::JSON& CurveObject = Object[Key];
	if (CurveObject.JSONType() != json::JSON::Class::Object)
	{
		return;
	}

	Curve.Reset();
	if (CurveObject.hasKey(ParticleKeys::Value))
	{
		Curve.DefaultValue = static_cast<float>(CurveObject[ParticleKeys::Value].ToFloat());
	}
	if (CurveObject.hasKey(ParticleKeys::Keys) && CurveObject[ParticleKeys::Keys].JSONType() == json::JSON::Class::Array)
	{
		for (auto& KeyObject : CurveObject[ParticleKeys::Keys].ArrayRange())
		{
			if (!KeyObject.hasKey(ParticleKeys::Time) || !KeyObject.hasKey(ParticleKeys::Value))
			{
				continue;
			}

			const float Time = static_cast<float>(KeyObject[ParticleKeys::Time].ToFloat());
			const float Value = static_cast<float>(KeyObject[ParticleKeys::Value].ToFloat());
			int32 InterpMode = static_cast<int32>(ECurveInterpMode::Linear);
			if (KeyObject.hasKey(ParticleKeys::InterpMode))
			{
				InterpMode = std::clamp(static_cast<int32>(KeyObject[ParticleKeys::InterpMode].ToInt()), 0, static_cast<int32>(ECurveInterpMode::Cubic));
			}
			Curve.AddKey(Time, Value, static_cast<ECurveInterpMode>(InterpMode));
		}
	}
	Curve.SortKeys();
	Curve.AutoSetTangents();
}

json::JSON MakeFloatDistributionJSON(const FParticleDistributionFloat& Distribution)
{
	json::JSON Object = json::JSON::Make(json::JSON::Class::Object);
	Object[ParticleKeys::Mode] = static_cast<int32>(Distribution.Mode);
	Object[ParticleKeys::Constant] = Distribution.Constant;
	Object[ParticleKeys::Min] = Distribution.Min;
	Object[ParticleKeys::Max] = Distribution.Max;
	Object[ParticleKeys::ConstantCurve] = MakeCurveJSON(Distribution.ConstantCurve);
	Object[ParticleKeys::MinCurve] = MakeCurveJSON(Distribution.MinCurve);
	Object[ParticleKeys::MaxCurve] = MakeCurveJSON(Distribution.MaxCurve);
	return Object;
}

void ReadFloatDistributionJSON(json::JSON& Object, const char* Key, FParticleDistributionFloat& Distribution)
{
	if (!Object.hasKey(Key))
	{
		return;
	}

	json::JSON& DistributionObject = Object[Key];
	if (DistributionObject.JSONType() != json::JSON::Class::Object)
	{
		return;
	}

	if (DistributionObject.hasKey(ParticleKeys::Mode))
	{
		const int32 Mode = std::clamp(static_cast<int32>(DistributionObject[ParticleKeys::Mode].ToInt()), 0, static_cast<int32>(EParticleDistributionMode::UniformCurve));
		Distribution.Mode = static_cast<EParticleDistributionMode>(Mode);
	}
	if (DistributionObject.hasKey(ParticleKeys::Constant)) Distribution.Constant = static_cast<float>(DistributionObject[ParticleKeys::Constant].ToFloat());
	if (DistributionObject.hasKey(ParticleKeys::Min)) Distribution.Min = static_cast<float>(DistributionObject[ParticleKeys::Min].ToFloat());
	if (DistributionObject.hasKey(ParticleKeys::Max)) Distribution.Max = static_cast<float>(DistributionObject[ParticleKeys::Max].ToFloat());
	ReadCurveJSON(DistributionObject, ParticleKeys::ConstantCurve, Distribution.ConstantCurve);
	ReadCurveJSON(DistributionObject, ParticleKeys::MinCurve, Distribution.MinCurve);
	ReadCurveJSON(DistributionObject, ParticleKeys::MaxCurve, Distribution.MaxCurve);
}

json::JSON MakeVectorDistributionJSON(const FParticleDistributionVector& Distribution)
{
	json::JSON Object = json::JSON::Make(json::JSON::Class::Object);
	Object[ParticleKeys::X] = MakeFloatDistributionJSON(Distribution.X);
	Object[ParticleKeys::Y] = MakeFloatDistributionJSON(Distribution.Y);
	Object[ParticleKeys::Z] = MakeFloatDistributionJSON(Distribution.Z);
	return Object;
}

void ReadVectorDistributionJSON(json::JSON& Object, const char* Key, FParticleDistributionVector& Distribution)
{
	if (!Object.hasKey(Key))
	{
		return;
	}

	json::JSON& DistributionObject = Object[Key];
	if (DistributionObject.JSONType() != json::JSON::Class::Object)
	{
		return;
	}

	ReadFloatDistributionJSON(DistributionObject, ParticleKeys::X, Distribution.X);
	ReadFloatDistributionJSON(DistributionObject, ParticleKeys::Y, Distribution.Y);
	ReadFloatDistributionJSON(DistributionObject, ParticleKeys::Z, Distribution.Z);
}

FString GetParticleMaterialPath(UMaterialInterface* MaterialInterface)
{
	UMaterial* Material = MaterialInterface ? MaterialInterface->GetMaterial() : nullptr;
	return Material ? FPaths::MakeProjectRelative(Material->GetAssetPathFileName()) : FString();
}

UStaticMesh* LoadParticleStaticMesh(const FString& MeshPath)
{
	if (MeshPath.empty())
	{
		return nullptr;
	}

	if (UStaticMesh* CachedMesh = FMeshManager::FindStaticMesh(MeshPath))
	{
		return CachedMesh;
	}

	ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
	return Device ? FMeshManager::LoadStaticMesh(MeshPath, Device) : nullptr;
}

bool ExpandSpriteSizeToMeshVolume(UParticleModuleSize* Size)
{
	if (!Size)
	{
		return false;
	}

	auto ExpandIfSpriteDefault = [](FVector& Value) -> bool
	{
		const float TargetUniformSize = (std::max)(Value.X, Value.Y);
		if (TargetUniformSize <= 1.0f || std::abs(Value.Z - 1.0f) > 0.001f)
		{
			return false;
		}

		Value.Z = TargetUniformSize;
		return true;
	};

	bool bChanged = false;
	bChanged |= ExpandIfSpriteDefault(Size->StartSize);
	bChanged |= ExpandIfSpriteDefault(Size->StartSizeMin);
	bChanged |= ExpandIfSpriteDefault(Size->StartSizeMax);
	return bChanged;
}

void RestoreMeshEmitterSizeDefaults(UParticleLODLevel* LOD)
{
	if (!LOD || !LOD->TypeDataModule || !LOD->TypeDataModule->IsAMeshEmitter())
	{
		return;
	}

	for (UParticleModule* Module : LOD->Modules)
	{
		ExpandSpriteSizeToMeshVolume(Cast<UParticleModuleSize>(Module));
	}
}

UParticleModuleTypeDataBase* FindLODTypeDataModule(UParticleLODLevel* LOD)
{
	if (!LOD)
	{
		return nullptr;
	}

	if (LOD->TypeDataModule)
	{
		return LOD->TypeDataModule;
	}

	for (UParticleModule* Module : LOD->Modules)
	{
		if (UParticleModuleTypeDataBase* TypeData = Cast<UParticleModuleTypeDataBase>(Module))
		{
			return TypeData;
		}
	}
	return nullptr;
}

json::JSON SerializeRequiredModule(UParticleModuleRequired* Required)
{
	json::JSON Object = json::JSON::Make(json::JSON::Class::Object);
	if (!Required)
	{
		return Object;
	}

	Object[ParticleKeys::Material] = GetParticleMaterialPath(Required->Material);
	Object[ParticleKeys::EmitterOrigin] = MakeVectorJSON(Required->EmitterOrigin);
	Object[ParticleKeys::ScreenAlignment] = static_cast<int32>(Required->ScreenAlignment);
	Object[ParticleKeys::SubImagesHorizontal] = Required->SubImages_Horizontal;
	Object[ParticleKeys::SubImagesVertical] = Required->SubImages_Vertical;
	Object[ParticleKeys::AlphaSource] = Required->AlphaSource;
	Object[ParticleKeys::AlphaThreshold] = Required->AlphaThreshold;
	Object[ParticleKeys::AlphaPower] = Required->AlphaPower;
	Object[ParticleKeys::ColorIntensity] = Required->ColorIntensity;
	Object[ParticleKeys::SortMode] = static_cast<int32>(Required->SortMode);
	Object[ParticleKeys::EmitterDuration] = Required->EmitterDuration;
	Object[ParticleKeys::MaxDrawCount] = Required->MaxDrawCount;
	Object[ParticleKeys::bUseLocalSpace] = Required->bUseLocalSpace != 0;
	Object[ParticleKeys::bKillOnDeactivate] = Required->bKillOnDeactivate != 0;
	Object[ParticleKeys::bKillOnCompleted] = Required->bKillOnCompleted != 0;
	return Object;
}

json::JSON SerializeSpawnModule(UParticleModuleSpawn* Spawn)
{
	json::JSON Object = json::JSON::Make(json::JSON::Class::Object);
	if (Spawn)
	{
		Object[ParticleKeys::Rate] = Spawn->Rate;
		Object[ParticleKeys::RateDistribution] = MakeFloatDistributionJSON(Spawn->RateDistribution);
		Object[ParticleKeys::ParticleBurstMethod] = static_cast<int32>(Spawn->ParticleBurstMethod);

		json::JSON Bursts = json::Array();
		for (const FParticleBurst& Burst : Spawn->BurstList)
		{
			json::JSON BurstObject = json::JSON::Make(json::JSON::Class::Object);
			BurstObject[ParticleKeys::Count] = Burst.Count;
			BurstObject[ParticleKeys::CountLow] = Burst.CountLow;
			BurstObject[ParticleKeys::Time] = Burst.Time;
			Bursts.append(BurstObject);
		}
		Object[ParticleKeys::BurstList] = Bursts;
	}
	return Object;
}

json::JSON SerializeTypeDataModule(UParticleModuleTypeDataBase* TypeData)
{
	json::JSON Object = json::JSON::Make(json::JSON::Class::Object);
	if (!TypeData)
	{
		return Object;
	}

	if (UParticleModuleTypeDataMesh* Mesh = Cast<UParticleModuleTypeDataMesh>(TypeData))
	{
		Object[ParticleKeys::Type] = ParticleKeys::Mesh;
		Object[ParticleKeys::MeshPath] = FPaths::MakeProjectRelative(Mesh->MeshPath);
	}
	else if (UParticleModuleTypeDataBeam2* Beam = Cast<UParticleModuleTypeDataBeam2>(TypeData))
	{
		Object[ParticleKeys::Type] = ParticleKeys::Beam2;
		Object[ParticleKeys::BeamMethod] = static_cast<int32>(Beam->BeamMethod);
		Object[ParticleKeys::InterpolationPoints] = Beam->InterpolationPoints;
		Object[ParticleKeys::Sheets] = Beam->Sheets;
		Object[ParticleKeys::MaxBeamCount] = Beam->MaxBeamCount;
		Object[ParticleKeys::Speed] = Beam->Speed;
		Object[ParticleKeys::bAlwaysOn] = Beam->bAlwaysOn;
		Object[ParticleKeys::UpVectorStepSize] = Beam->UpVectorStepSize;
		Object[ParticleKeys::Distance] = Beam->Distance;
		Object[ParticleKeys::SourcePoint] = MakeVectorJSON(Beam->SourcePoint);
		Object[ParticleKeys::TargetPoint] = MakeVectorJSON(Beam->TargetPoint);
		Object[ParticleKeys::Width] = Beam->Width;
		Object[ParticleKeys::TextureTile] = Beam->TextureTile;
		Object[ParticleKeys::TextureTileDistance] = Beam->TextureTileDistance;
		Object[ParticleKeys::Color] = MakeVectorJSON(Beam->Color);
		Object[ParticleKeys::Alpha] = Beam->Alpha;
		Object[ParticleKeys::BranchParentName] = Beam->BranchParentName.ToString();
		Object[ParticleKeys::TaperMethod] = static_cast<int32>(Beam->TaperMethod);
		Object[ParticleKeys::TaperFactor] = Beam->TaperFactor;
		Object[ParticleKeys::TaperScale] = Beam->TaperScale;
		Object[ParticleKeys::bRenderGeometry] = Beam->bRenderGeometry;
		Object[ParticleKeys::bRenderDirectLine] = Beam->bRenderDirectLine;
		Object[ParticleKeys::bRenderLines] = Beam->bRenderLines;
		Object[ParticleKeys::bRenderTessellation] = Beam->bRenderTessellation;

		json::JSON Targets = json::Array();
		for (const FBeamTargetData& Target : Beam->TargetData)
		{
			json::JSON TargetObject = json::JSON::Make(json::JSON::Class::Object);
			TargetObject[ParticleKeys::TargetName] = Target.TargetName.ToString();
			TargetObject[ParticleKeys::TargetPercentage] = Target.TargetPercentage;
			Targets.append(TargetObject);
		}
		Object[ParticleKeys::TargetData] = Targets;
	}
	else if (UParticleModuleTypeDataRibbon* Ribbon = Cast<UParticleModuleTypeDataRibbon>(TypeData))
	{
		Object[ParticleKeys::Type] = ParticleKeys::Ribbon;
		Object[ParticleKeys::MaxTessellationBetweenParticles] = Ribbon->MaxTessellationBetweenParticles;
		Object[ParticleKeys::SheetsPerTrail] = Ribbon->SheetsPerTrail;
		Object[ParticleKeys::MaxTrailCount] = Ribbon->MaxTrailCount;
		Object[ParticleKeys::MaxParticleInTrailCount] = Ribbon->MaxParticleInTrailCount;
		Object[ParticleKeys::bDeadTrailsOnDeactivate] = Ribbon->bDeadTrailsOnDeactivate;
		Object[ParticleKeys::bDeadTrailsOnSourceLoss] = Ribbon->bDeadTrailsOnSourceLoss;
		Object[ParticleKeys::bClipSourceSegment] = Ribbon->bClipSourceSegment;
		Object[ParticleKeys::bEnablePreviousTangentRecalculation] = Ribbon->bEnablePreviousTangentRecalculation;
		Object[ParticleKeys::bTangentRecalculationEveryFrame] = Ribbon->bTangentRecalculationEveryFrame;
		Object[ParticleKeys::bSpawnInitialParticle] = Ribbon->bSpawnInitialParticle;
		Object[ParticleKeys::RenderAxis] = static_cast<int32>(Ribbon->RenderAxis);
		Object[ParticleKeys::TangentSpawningScalar] = Ribbon->TangentSpawningScalar;
		Object[ParticleKeys::bRenderGeometry] = Ribbon->bRenderGeometry;
		Object[ParticleKeys::bRenderSpawnPoints] = Ribbon->bRenderSpawnPoints;
		Object[ParticleKeys::bRenderTangents] = Ribbon->bRenderTangents;
		Object[ParticleKeys::bRenderTessellation] = Ribbon->bRenderTessellation;
		Object[ParticleKeys::TilingDistance] = Ribbon->TilingDistance;
		Object[ParticleKeys::DistanceTessellationStepSize] = Ribbon->DistanceTessellationStepSize;
		Object[ParticleKeys::bEnableTangentDiffInterpScale] = Ribbon->bEnableTangentDiffInterpScale;
		Object[ParticleKeys::TangentTessellationScalar] = Ribbon->TangentTessellationScalar;
		Object[ParticleKeys::Width] = Ribbon->Width;
		Object[ParticleKeys::Color] = MakeVectorJSON(Ribbon->Color);
		Object[ParticleKeys::Alpha] = Ribbon->Alpha;
		Object[ParticleKeys::bUseSourceEmitter] = Ribbon->bUseSourceEmitter;
		Object[ParticleKeys::SourceEmitterName] = Ribbon->SourceEmitterName.ToString();
		Object[ParticleKeys::SourceTrailLifetime] = Ribbon->SourceTrailLifetime;
		Object[ParticleKeys::SourceSampleInterval] = Ribbon->SourceSampleInterval;
		Object[ParticleKeys::SourceMinSampleDistance] = Ribbon->SourceMinSampleDistance;
		Object[ParticleKeys::SourceWidthScale] = Ribbon->SourceWidthScale;
	}

	return Object;
}

const char* GetSerializableModuleType(UParticleModule* Module)
{
	if (Module->IsA<UParticleModuleLifetime>()) return "Lifetime";
	if (Module->IsA<UParticleModuleLocation>()) return "InitialLocation";
	if (Module->IsA<UParticleModuleVelocity>()) return "InitialVelocity";
	if (Module->IsA<UParticleModuleInitialRotation>()) return "InitialRotation";
	if (Module->IsA<UParticleModuleInitialRotationRate>()) return "InitialRotationRate";
	if (Module->IsA<UParticleModuleAcceleration>()) return "Acceleration";
	if (Module->IsA<UParticleModuleOrbit>()) return ParticleKeys::Orbit;
	if (Module->IsA<UParticleModuleColor>()) return "InitialColor";
	if (Module->IsA<UParticleModuleColorOverLife>()) return "ColorOverLife";
	if (Module->IsA<UParticleModuleColorScaleOverLife>()) return "ColorScaleOverLife";
	if (Module->IsA<UParticleModuleSize>()) return "InitialSize";
	if (Module->IsA<UParticleModuleBeamSource>()) return "BeamSource";
	if (Module->IsA<UParticleModuleBeamTarget>()) return "BeamTarget";
	if (Module->IsA<UParticleModuleBeamNoise>()) return "BeamNoise";
	if (Module->IsA<UParticleModuleCollision>()) return ParticleKeys::Collision;
	if (Module->IsA<UParticleModuleEventGenerator>()) return ParticleKeys::EventGenerator;
	if (Module->IsA<UParticleModuleEventReceiverSpawn>()) return ParticleKeys::EventReceiverSpawn;
	return nullptr;
}

json::JSON SerializeModule(UParticleModule* Module)
{
	json::JSON Object = json::JSON::Make(json::JSON::Class::Object);
	if (!Module)
	{
		return Object;
	}

	const char* Type = GetSerializableModuleType(Module);
	if (!Type)
	{
		return Object;
	}

	Object[ParticleKeys::Type] = Type;
	Object[ParticleKeys::bEnabled] = Module->bEnabled != 0;

	if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
	{
		Object[ParticleKeys::Rate] = Spawn->Rate;
		Object[ParticleKeys::RateDistribution] = MakeFloatDistributionJSON(Spawn->RateDistribution);
	}
	else if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
	{
		Object[ParticleKeys::Lifetime] = Lifetime->Lifetime;
		Object[ParticleKeys::LifetimeMin] = Lifetime->LifetimeMin;
		Object[ParticleKeys::LifetimeMax] = Lifetime->LifetimeMax;
		Object[ParticleKeys::LifetimeDistribution] = MakeFloatDistributionJSON(Lifetime->LifetimeDistribution);
	}
	else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
	{
		Object[ParticleKeys::StartLocation] = MakeVectorJSON(Location->StartLocation);
		Object[ParticleKeys::StartLocationMin] = MakeVectorJSON(Location->StartLocationMin);
		Object[ParticleKeys::StartLocationMax] = MakeVectorJSON(Location->StartLocationMax);
		Object[ParticleKeys::StartLocationDistribution] = MakeVectorDistributionJSON(Location->StartLocationDistribution);
	}
	else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
	{
		Object[ParticleKeys::StartVelocity] = MakeVectorJSON(Velocity->StartVelocity);
		Object[ParticleKeys::StartVelocityMin] = MakeVectorJSON(Velocity->StartVelocityMin);
		Object[ParticleKeys::StartVelocityMax] = MakeVectorJSON(Velocity->StartVelocityMax);
		Object[ParticleKeys::StartVelocityDistribution] = MakeVectorDistributionJSON(Velocity->StartVelocityDistribution);
	}
	else if (UParticleModuleInitialRotation* Rotation = Cast<UParticleModuleInitialRotation>(Module))
	{
		Object[ParticleKeys::StartRotation] = MakeVectorJSON(Rotation->StartRotationDegrees);
		Object[ParticleKeys::StartRotationMin] = MakeVectorJSON(Rotation->StartRotationDegreesMin);
		Object[ParticleKeys::StartRotationMax] = MakeVectorJSON(Rotation->StartRotationDegreesMax);
		Object[ParticleKeys::StartRotationDistribution] = MakeVectorDistributionJSON(Rotation->StartRotationDistribution);
	}
	else if (UParticleModuleInitialRotationRate* RotationRate = Cast<UParticleModuleInitialRotationRate>(Module))
	{
		Object[ParticleKeys::StartRotationRate] = MakeVectorJSON(RotationRate->StartRotationRateDegrees);
		Object[ParticleKeys::StartRotationRateMin] = MakeVectorJSON(RotationRate->StartRotationRateDegreesMin);
		Object[ParticleKeys::StartRotationRateMax] = MakeVectorJSON(RotationRate->StartRotationRateDegreesMax);
		Object[ParticleKeys::StartRotationRateDistribution] = MakeVectorDistributionJSON(RotationRate->StartRotationRateDistribution);
	}
	else if (UParticleModuleAcceleration* Acceleration = Cast<UParticleModuleAcceleration>(Module))
	{
		Object[ParticleKeys::Acceleration] = MakeVectorJSON(Acceleration->Acceleration);
		Object[ParticleKeys::AccelerationDistribution] = MakeVectorDistributionJSON(Acceleration->AccelerationDistribution);
	}
	else if (UParticleModuleOrbit* Orbit = Cast<UParticleModuleOrbit>(Module))
	{
		Object[ParticleKeys::OrbitOffset] = MakeVectorJSON(Orbit->Offset);
		Object[ParticleKeys::OrbitOffsetDistribution] = MakeVectorDistributionJSON(Orbit->OffsetDistribution);
		Object[ParticleKeys::OrbitRotation] = MakeVectorJSON(Orbit->RotationDegrees);
		Object[ParticleKeys::OrbitRotationDistribution] = MakeVectorDistributionJSON(Orbit->RotationDistribution);
		Object[ParticleKeys::OrbitRotationRate] = MakeVectorJSON(Orbit->RotationRateDegrees);
		Object[ParticleKeys::OrbitRotationRateDistribution] = MakeVectorDistributionJSON(Orbit->RotationRateDistribution);
	}
	else if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
	{
		Object[ParticleKeys::StartColor] = MakeVectorJSON(Color->StartColor);
		Object[ParticleKeys::StartColorMin] = MakeVectorJSON(Color->StartColorMin);
		Object[ParticleKeys::StartColorMax] = MakeVectorJSON(Color->StartColorMax);
		Object[ParticleKeys::StartColorDistribution] = MakeVectorDistributionJSON(Color->StartColorDistribution);
		Object[ParticleKeys::StartAlpha] = Color->StartAlpha;
		Object[ParticleKeys::StartAlphaMin] = Color->StartAlphaMin;
		Object[ParticleKeys::StartAlphaMax] = Color->StartAlphaMax;
		Object[ParticleKeys::StartAlphaDistribution] = MakeFloatDistributionJSON(Color->StartAlphaDistribution);
	}
	else if (UParticleModuleColorOverLife* ColorOverLife = Cast<UParticleModuleColorOverLife>(Module))
	{
		Object[ParticleKeys::EndColor] = MakeVectorJSON(ColorOverLife->ColorOverLife);
		Object[ParticleKeys::EndAlpha] = ColorOverLife->AlphaOverLife;
		Object[ParticleKeys::ColorOverLifeDistribution] = MakeVectorDistributionJSON(ColorOverLife->ColorOverLifeDistribution);
		Object[ParticleKeys::AlphaOverLifeDistribution] = MakeFloatDistributionJSON(ColorOverLife->AlphaOverLifeDistribution);
	}
	else if (UParticleModuleColorScaleOverLife* ColorScale = Cast<UParticleModuleColorScaleOverLife>(Module))
	{
		Object[ParticleKeys::ColorScaleOverLife] = MakeVectorJSON(ColorScale->ColorScaleOverLife);
		Object[ParticleKeys::AlphaScaleOverLife] = ColorScale->AlphaScaleOverLife;
		Object[ParticleKeys::ColorScaleOverLifeDistribution] = MakeVectorDistributionJSON(ColorScale->ColorScaleOverLifeDistribution);
		Object[ParticleKeys::AlphaScaleOverLifeDistribution] = MakeFloatDistributionJSON(ColorScale->AlphaScaleOverLifeDistribution);
	}
	else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
	{
		Object[ParticleKeys::StartSize] = MakeVectorJSON(Size->StartSize);
		Object[ParticleKeys::StartSizeMin] = MakeVectorJSON(Size->StartSizeMin);
		Object[ParticleKeys::StartSizeMax] = MakeVectorJSON(Size->StartSizeMax);
		Object[ParticleKeys::StartSizeDistribution] = MakeVectorDistributionJSON(Size->StartSizeDistribution);
	}
	else if (UParticleModuleBeamSource* Source = Cast<UParticleModuleBeamSource>(Module))
	{
		Object[ParticleKeys::SourceMethod] = static_cast<int32>(Source->SourceMethod);
		Object[ParticleKeys::SourceName] = Source->SourceName.ToString();
		Object[ParticleKeys::bSourceAbsolute] = Source->bSourceAbsolute;
		Object[ParticleKeys::bLockSource] = Source->bLockSource;
		Object[ParticleKeys::Source] = MakeVectorJSON(Source->Source);
		Object[ParticleKeys::bLockSourceTangent] = Source->bLockSourceTangent;
		Object[ParticleKeys::SourceTangent] = MakeVectorJSON(Source->SourceTangent);
		Object[ParticleKeys::SourceStrength] = Source->SourceStrength;
	}
	else if (UParticleModuleBeamTarget* Target = Cast<UParticleModuleBeamTarget>(Module))
	{
		Object[ParticleKeys::TargetMethod] = static_cast<int32>(Target->TargetMethod);
		Object[ParticleKeys::TargetName] = Target->TargetName.ToString();
		Object[ParticleKeys::bTargetAbsolute] = Target->bTargetAbsolute;
		Object[ParticleKeys::bLockTarget] = Target->bLockTarget;
		Object[ParticleKeys::Target] = MakeVectorJSON(Target->Target);
		Object[ParticleKeys::bLockTargetTangent] = Target->bLockTargetTangent;
		Object[ParticleKeys::TargetTangent] = MakeVectorJSON(Target->TargetTangent);
		Object[ParticleKeys::TargetStrength] = Target->TargetStrength;
	}
	else if (UParticleModuleBeamNoise* Noise = Cast<UParticleModuleBeamNoise>(Module))
	{
		Object[ParticleKeys::Frequency] = Noise->Frequency;
		Object[ParticleKeys::FrequencyDistance] = Noise->FrequencyDistance;
		Object[ParticleKeys::NoiseRange] = MakeVectorJSON(Noise->NoiseRange);
		Object[ParticleKeys::NoiseSpeed] = Noise->NoiseSpeed;
		Object[ParticleKeys::NoiseLockTime] = Noise->NoiseLockTime;
		Object[ParticleKeys::bTargetNoise] = Noise->bTargetNoise;
	}
	else if (UParticleModuleCollision* Collision = Cast<UParticleModuleCollision>(Module))
	{
		Object[ParticleKeys::TraceChannel] = static_cast<int32>(Collision->TraceChannel);
		Object[ParticleKeys::ResponseMode] = static_cast<int32>(Collision->ResponseMode);
		Object[ParticleKeys::DampingFactor] = Collision->DampingFactor;
		Object[ParticleKeys::CollisionOffset] = Collision->CollisionOffset;
		Object[ParticleKeys::CollisionRadiusScale] = Collision->CollisionRadiusScale;
		Object[ParticleKeys::MaxCollisions] = Collision->MaxCollisions;
	}
	else if (UParticleModuleEventGenerator* EventGenerator = Cast<UParticleModuleEventGenerator>(Module))
	{
		json::JSON Events = json::Array();
		for (const FParticleEvent_GenerateInfo& EventInfo : EventGenerator->Events)
		{
			json::JSON EventObject = json::JSON::Make(json::JSON::Class::Object);
			EventObject[ParticleKeys::EventType] = static_cast<int32>(EventInfo.Type);
			EventObject[ParticleKeys::CustomName] = EventInfo.CustomName.ToString();
			EventObject[ParticleKeys::Frequency] = EventInfo.Frequency;
			EventObject[ParticleKeys::ParticleFrequency] = EventInfo.ParticleFrequency;
			EventObject[ParticleKeys::FirstTimeOnly] = EventInfo.FirstTimeOnly;
			EventObject[ParticleKeys::LastTimeOnly] = EventInfo.LastTimeOnly;
			EventObject[ParticleKeys::UseReflectedImpactVector] = EventInfo.UseReflectedImpactVector;
			EventObject[ParticleKeys::bUseOrbitOffset] = EventInfo.bUseOrbitOffset;
			Events.append(EventObject);
		}
		Object[ParticleKeys::Events] = Events;
	}
	else if (UParticleModuleEventReceiverSpawn* EventReceiver = Cast<UParticleModuleEventReceiverSpawn>(Module))
	{
		Object[ParticleKeys::EventGeneratorType] = static_cast<int32>(EventReceiver->EventGeneratorType);
		Object[ParticleKeys::EventName] = EventReceiver->EventName.ToString();
		Object[ParticleKeys::SpawnCount] = EventReceiver->SpawnCount;
		Object[ParticleKeys::bSpawnOnlyOnEvent] = EventReceiver->bSpawnOnlyOnEvent;
		Object[ParticleKeys::SpawnLocationOffset] = MakeVectorJSON(EventReceiver->SpawnLocationOffset);
		Object[ParticleKeys::bInheritEventVelocity] = EventReceiver->bInheritEventVelocity;
		Object[ParticleKeys::EventVelocityScale] = EventReceiver->EventVelocityScale;
	}

	return Object;
}

json::JSON SerializeLODLevel(UParticleLODLevel* LOD)
{
	json::JSON Object = json::JSON::Make(json::JSON::Class::Object);
	if (!LOD)
	{
		return Object;
	}

	Object[ParticleKeys::Level] = LOD->Level;
	Object[ParticleKeys::bEnabled] = LOD->bEnabled != 0;
	Object[ParticleKeys::Required] = SerializeRequiredModule(LOD->RequiredModule);
	Object[ParticleKeys::Spawn] = SerializeSpawnModule(LOD->SpawnModule);
	json::JSON TypeDataObject = SerializeTypeDataModule(FindLODTypeDataModule(LOD));
	if (TypeDataObject.hasKey(ParticleKeys::Type))
	{
		Object[ParticleKeys::TypeData] = TypeDataObject;
	}

	json::JSON Modules = json::Array();
	for (UParticleModule* Module : LOD->Modules)
	{
		json::JSON ModuleObject = SerializeModule(Module);
		if (ModuleObject.hasKey(ParticleKeys::Type))
		{
			Modules.append(ModuleObject);
		}
	}
	Object[ParticleKeys::Modules] = Modules;
	return Object;
}

json::JSON SerializeEmitter(UParticleEmitter* Emitter)
{
	json::JSON Object = json::JSON::Make(json::JSON::Class::Object);
	if (!Emitter)
	{
		return Object;
	}

	Object[ParticleKeys::Name] = Emitter->GetEmitterName().ToString();
	Object[ParticleKeys::InitialAllocationCount] = Emitter->InitialAllocationCount;
	Object[ParticleKeys::PeakActiveParticles] = Emitter->PeakActiveParticles;

	json::JSON LODLevels = json::Array();
	for (UParticleLODLevel* LOD : Emitter->LODLevels)
	{
		LODLevels.append(SerializeLODLevel(LOD));
	}
	Object[ParticleKeys::LODLevels] = LODLevels;
	return Object;
}

json::JSON SerializeParticleSystem(UParticleSystem* ParticleSystem)
{
	json::JSON Root = json::JSON::Make(json::JSON::Class::Object);
	Root[ParticleKeys::Version] = ParticleSystemVersion;

	if (ParticleSystem)
	{
		Root[ParticleKeys::Name] = ParticleSystem->GetName();
		ParticleSystem->NormalizeLODData();
	}

	json::JSON LODDistances = json::Array();
	if (ParticleSystem)
	{
		for (float Distance : ParticleSystem->GetLODDistances())
		{
			LODDistances.append(Distance);
		}
	}
	Root[ParticleKeys::LODDistances] = LODDistances;

	json::JSON Emitters = json::Array();
	if (ParticleSystem)
	{
		for (UParticleEmitter* Emitter : ParticleSystem->Emitters)
		{
			Emitters.append(SerializeEmitter(Emitter));
		}
	}
	Root[ParticleKeys::Emitters] = Emitters;
	return Root;
}

UParticleModuleRequired* DeserializeRequiredModule(json::JSON& Object, UParticleLODLevel* Outer)
{
	UParticleModuleRequired* Required = GUObjectArray.CreateObject<UParticleModuleRequired>(Outer);
	if (Object.hasKey(ParticleKeys::Material))
	{
		const FString MaterialPath = Object[ParticleKeys::Material].ToString();
		if (!MaterialPath.empty())
		{
			Required->Material = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath);
		}
	}
	Required->EmitterOrigin = ReadVectorJSON(Object, ParticleKeys::EmitterOrigin, Required->EmitterOrigin);
	if (Object.hasKey(ParticleKeys::ScreenAlignment))
	{
		const int32 Value = static_cast<int32>(Object[ParticleKeys::ScreenAlignment].ToInt());
		Required->ScreenAlignment = static_cast<EParticleScreenAlignment>(std::clamp(Value, 0, static_cast<int32>(PSA_MAX) - 1));
	}
	if (Object.hasKey(ParticleKeys::SubImagesHorizontal)) Required->SubImages_Horizontal = std::max(1, static_cast<int32>(Object[ParticleKeys::SubImagesHorizontal].ToInt()));
	if (Object.hasKey(ParticleKeys::SubImagesVertical)) Required->SubImages_Vertical = std::max(1, static_cast<int32>(Object[ParticleKeys::SubImagesVertical].ToInt()));
	if (Object.hasKey(ParticleKeys::AlphaSource)) Required->AlphaSource = std::clamp(static_cast<int32>(Object[ParticleKeys::AlphaSource].ToInt()), 0, 1);
	if (Object.hasKey(ParticleKeys::AlphaThreshold)) Required->AlphaThreshold = std::clamp(static_cast<float>(Object[ParticleKeys::AlphaThreshold].ToFloat()), 0.0f, 1.0f);
	if (Object.hasKey(ParticleKeys::AlphaPower)) Required->AlphaPower = std::max(0.001f, static_cast<float>(Object[ParticleKeys::AlphaPower].ToFloat()));
	if (Object.hasKey(ParticleKeys::ColorIntensity)) Required->ColorIntensity = std::max(0.0f, static_cast<float>(Object[ParticleKeys::ColorIntensity].ToFloat()));
	if (Object.hasKey(ParticleKeys::SortMode))
	{
		const int32 Value = static_cast<int32>(Object[ParticleKeys::SortMode].ToInt());
		Required->SortMode = static_cast<EParticleSortMode>(std::clamp(Value, 0, static_cast<int32>(PSORTMODE_MAX) - 1));
	}
	if (Object.hasKey(ParticleKeys::EmitterDuration)) Required->EmitterDuration = std::max(0.0f, static_cast<float>(Object[ParticleKeys::EmitterDuration].ToFloat()));
	if (Object.hasKey(ParticleKeys::MaxDrawCount)) Required->MaxDrawCount = std::max(0, static_cast<int32>(Object[ParticleKeys::MaxDrawCount].ToInt()));
	if (Object.hasKey(ParticleKeys::bUseLocalSpace)) Required->bUseLocalSpace = Object[ParticleKeys::bUseLocalSpace].ToBool();
	if (Object.hasKey(ParticleKeys::bKillOnDeactivate)) Required->bKillOnDeactivate = Object[ParticleKeys::bKillOnDeactivate].ToBool();
	if (Object.hasKey(ParticleKeys::bKillOnCompleted)) Required->bKillOnCompleted = Object[ParticleKeys::bKillOnCompleted].ToBool();
	return Required;
}

UParticleModuleSpawn* DeserializeSpawnModule(json::JSON& Object, UParticleLODLevel* Outer)
{
	UParticleModuleSpawn* Spawn = GUObjectArray.CreateObject<UParticleModuleSpawn>(Outer);
	Spawn->bEnabled = true;
	if (Object.hasKey(ParticleKeys::Rate))
	{
		Spawn->Rate = std::max(0.0f, static_cast<float>(Object[ParticleKeys::Rate].ToFloat()));
	}
	Spawn->RateDistribution.SetConstant(Spawn->Rate);
	ReadFloatDistributionJSON(Object, ParticleKeys::RateDistribution, Spawn->RateDistribution);
	if (Object.hasKey(ParticleKeys::ParticleBurstMethod))
	{
		Spawn->ParticleBurstMethod = static_cast<EParticleBurstMethod>(
			std::clamp(static_cast<int32>(Object[ParticleKeys::ParticleBurstMethod].ToInt()), 0, static_cast<int32>(EPBM_MAX) - 1));
	}
	if (Object.hasKey(ParticleKeys::BurstList))
	{
		Spawn->BurstList.clear();
		for (auto& BurstObject : Object[ParticleKeys::BurstList].ArrayRange())
		{
			FParticleBurst Burst;
			if (BurstObject.hasKey(ParticleKeys::Count))
			{
				Burst.Count = std::max(0, static_cast<int32>(BurstObject[ParticleKeys::Count].ToInt()));
			}
			if (BurstObject.hasKey(ParticleKeys::CountLow))
			{
				Burst.CountLow = static_cast<int32>(BurstObject[ParticleKeys::CountLow].ToInt());
				if (Burst.CountLow > -1)
				{
					Burst.CountLow = std::clamp(Burst.CountLow, 0, Burst.Count);
				}
			}
			if (BurstObject.hasKey(ParticleKeys::Time))
			{
				Burst.Time = std::clamp(static_cast<float>(BurstObject[ParticleKeys::Time].ToFloat()), 0.0f, 1.0f);
			}
			Spawn->BurstList.push_back(Burst);
		}
	}
	return Spawn;
}

UParticleModuleTypeDataBase* DeserializeTypeDataModule(json::JSON& Object, UParticleLODLevel* Outer)
{
	if (!Object.hasKey(ParticleKeys::Type))
	{
		return nullptr;
	}

	const FString Type = Object[ParticleKeys::Type].ToString();
	if (Type == ParticleKeys::Mesh)
	{
		UParticleModuleTypeDataMesh* Mesh = GUObjectArray.CreateObject<UParticleModuleTypeDataMesh>(Outer);
		Mesh->bEnabled = true;
		if (Object.hasKey(ParticleKeys::MeshPath))
		{
			Mesh->MeshPath = FPaths::MakeProjectRelative(Object[ParticleKeys::MeshPath].ToString());
			Mesh->Mesh = LoadParticleStaticMesh(Mesh->MeshPath);
		}
		return Mesh;
	}

	if (Type == ParticleKeys::Beam2)
	{
		UParticleModuleTypeDataBeam2* Beam = GUObjectArray.CreateObject<UParticleModuleTypeDataBeam2>(Outer);
		if (Object.hasKey(ParticleKeys::BeamMethod))
		{
			const int32 Value = static_cast<int32>(Object[ParticleKeys::BeamMethod].ToInt());
			Beam->BeamMethod = static_cast<EBeam2Method>(std::clamp(Value, 0, static_cast<int32>(PEB2M_MAX) - 1));
		}
		if (Object.hasKey(ParticleKeys::InterpolationPoints)) Beam->InterpolationPoints = std::max(1, static_cast<int32>(Object[ParticleKeys::InterpolationPoints].ToInt()));
		if (Object.hasKey(ParticleKeys::Sheets)) Beam->Sheets = std::max(1, static_cast<int32>(Object[ParticleKeys::Sheets].ToInt()));
		if (Object.hasKey(ParticleKeys::MaxBeamCount)) Beam->MaxBeamCount = std::max(1, static_cast<int32>(Object[ParticleKeys::MaxBeamCount].ToInt()));
		if (Object.hasKey(ParticleKeys::Speed)) Beam->Speed = std::max(0.0f, static_cast<float>(Object[ParticleKeys::Speed].ToFloat()));
		if (Object.hasKey(ParticleKeys::bAlwaysOn)) Beam->bAlwaysOn = Object[ParticleKeys::bAlwaysOn].ToBool();
		if (Object.hasKey(ParticleKeys::UpVectorStepSize)) Beam->UpVectorStepSize = std::max(0, static_cast<int32>(Object[ParticleKeys::UpVectorStepSize].ToInt()));
		if (Object.hasKey(ParticleKeys::Distance)) Beam->Distance = std::max(0.0f, static_cast<float>(Object[ParticleKeys::Distance].ToFloat()));
		Beam->SourcePoint = ReadVectorJSON(Object, ParticleKeys::SourcePoint, Beam->SourcePoint);
		Beam->TargetPoint = ReadVectorJSON(Object, ParticleKeys::TargetPoint, Beam->TargetPoint);
		if (Object.hasKey(ParticleKeys::Width)) Beam->Width = std::max(0.0f, static_cast<float>(Object[ParticleKeys::Width].ToFloat()));
		if (Object.hasKey(ParticleKeys::TextureTile)) Beam->TextureTile = std::max(1, static_cast<int32>(Object[ParticleKeys::TextureTile].ToInt()));
		if (Object.hasKey(ParticleKeys::TextureTileDistance)) Beam->TextureTileDistance = std::max(0.0f, static_cast<float>(Object[ParticleKeys::TextureTileDistance].ToFloat()));
		Beam->Color = ReadVectorJSON(Object, ParticleKeys::Color, Beam->Color);
		if (Object.hasKey(ParticleKeys::Alpha)) Beam->Alpha = std::clamp(static_cast<float>(Object[ParticleKeys::Alpha].ToFloat()), 0.0f, 1.0f);
		if (Object.hasKey(ParticleKeys::BranchParentName)) Beam->BranchParentName = FName(Object[ParticleKeys::BranchParentName].ToString());
		if (Object.hasKey(ParticleKeys::TaperMethod))
		{
			const int32 Value = static_cast<int32>(Object[ParticleKeys::TaperMethod].ToInt());
			Beam->TaperMethod = static_cast<EBeamTaperMethod>(std::clamp(Value, 0, static_cast<int32>(PEBTM_MAX) - 1));
		}
		if (Object.hasKey(ParticleKeys::TaperFactor)) Beam->TaperFactor = std::max(0.0f, static_cast<float>(Object[ParticleKeys::TaperFactor].ToFloat()));
		if (Object.hasKey(ParticleKeys::TaperScale)) Beam->TaperScale = std::max(0.0f, static_cast<float>(Object[ParticleKeys::TaperScale].ToFloat()));
		if (Object.hasKey(ParticleKeys::bRenderGeometry)) Beam->bRenderGeometry = Object[ParticleKeys::bRenderGeometry].ToBool();
		if (Object.hasKey(ParticleKeys::bRenderDirectLine)) Beam->bRenderDirectLine = Object[ParticleKeys::bRenderDirectLine].ToBool();
		if (Object.hasKey(ParticleKeys::bRenderLines)) Beam->bRenderLines = Object[ParticleKeys::bRenderLines].ToBool();
		if (Object.hasKey(ParticleKeys::bRenderTessellation)) Beam->bRenderTessellation = Object[ParticleKeys::bRenderTessellation].ToBool();

		if (Object.hasKey(ParticleKeys::TargetData))
		{
			for (auto& TargetObject : Object[ParticleKeys::TargetData].ArrayRange())
			{
				FBeamTargetData Target;
				if (TargetObject.hasKey(ParticleKeys::TargetName))
				{
					Target.TargetName = FName(TargetObject[ParticleKeys::TargetName].ToString());
				}
				if (TargetObject.hasKey(ParticleKeys::TargetPercentage))
				{
					Target.TargetPercentage = std::clamp(static_cast<float>(TargetObject[ParticleKeys::TargetPercentage].ToFloat()), 0.0f, 100.0f);
				}
				Beam->TargetData.push_back(Target);
			}
		}

		return Beam;
	}

	if (Type == ParticleKeys::Ribbon)
	{
		UParticleModuleTypeDataRibbon* Ribbon = GUObjectArray.CreateObject<UParticleModuleTypeDataRibbon>(Outer);
		if (Object.hasKey(ParticleKeys::MaxTessellationBetweenParticles)) Ribbon->MaxTessellationBetweenParticles = std::clamp(static_cast<int32>(Object[ParticleKeys::MaxTessellationBetweenParticles].ToInt()), 0, 32);
		if (Object.hasKey(ParticleKeys::SheetsPerTrail)) Ribbon->SheetsPerTrail = std::clamp(static_cast<int32>(Object[ParticleKeys::SheetsPerTrail].ToInt()), 1, 16);
		if (Object.hasKey(ParticleKeys::MaxTrailCount)) Ribbon->MaxTrailCount = std::clamp(static_cast<int32>(Object[ParticleKeys::MaxTrailCount].ToInt()), 1, 512);
		if (Object.hasKey(ParticleKeys::MaxParticleInTrailCount)) Ribbon->MaxParticleInTrailCount = std::clamp(static_cast<int32>(Object[ParticleKeys::MaxParticleInTrailCount].ToInt()), 2, 1024);
		if (Object.hasKey(ParticleKeys::bDeadTrailsOnDeactivate)) Ribbon->bDeadTrailsOnDeactivate = Object[ParticleKeys::bDeadTrailsOnDeactivate].ToBool();
		if (Object.hasKey(ParticleKeys::bDeadTrailsOnSourceLoss)) Ribbon->bDeadTrailsOnSourceLoss = Object[ParticleKeys::bDeadTrailsOnSourceLoss].ToBool();
		if (Object.hasKey(ParticleKeys::bClipSourceSegment)) Ribbon->bClipSourceSegment = Object[ParticleKeys::bClipSourceSegment].ToBool();
		if (Object.hasKey(ParticleKeys::bEnablePreviousTangentRecalculation)) Ribbon->bEnablePreviousTangentRecalculation = Object[ParticleKeys::bEnablePreviousTangentRecalculation].ToBool();
		if (Object.hasKey(ParticleKeys::bTangentRecalculationEveryFrame)) Ribbon->bTangentRecalculationEveryFrame = Object[ParticleKeys::bTangentRecalculationEveryFrame].ToBool();
		if (Object.hasKey(ParticleKeys::bSpawnInitialParticle)) Ribbon->bSpawnInitialParticle = Object[ParticleKeys::bSpawnInitialParticle].ToBool();
		if (Object.hasKey(ParticleKeys::RenderAxis))
		{
			const int32 Value = static_cast<int32>(Object[ParticleKeys::RenderAxis].ToInt());
			Ribbon->RenderAxis = static_cast<ETrailsRenderAxisOption>(std::clamp(Value, 0, static_cast<int32>(Trails_MAX) - 1));
		}
		if (Object.hasKey(ParticleKeys::TangentSpawningScalar)) Ribbon->TangentSpawningScalar = std::max(0.0f, static_cast<float>(Object[ParticleKeys::TangentSpawningScalar].ToFloat()));
		if (Object.hasKey(ParticleKeys::bRenderGeometry)) Ribbon->bRenderGeometry = Object[ParticleKeys::bRenderGeometry].ToBool();
		if (Object.hasKey(ParticleKeys::bRenderSpawnPoints)) Ribbon->bRenderSpawnPoints = Object[ParticleKeys::bRenderSpawnPoints].ToBool();
		if (Object.hasKey(ParticleKeys::bRenderTangents)) Ribbon->bRenderTangents = Object[ParticleKeys::bRenderTangents].ToBool();
		if (Object.hasKey(ParticleKeys::bRenderTessellation)) Ribbon->bRenderTessellation = Object[ParticleKeys::bRenderTessellation].ToBool();
		if (Object.hasKey(ParticleKeys::TilingDistance)) Ribbon->TilingDistance = std::max(0.0f, static_cast<float>(Object[ParticleKeys::TilingDistance].ToFloat()));
		if (Object.hasKey(ParticleKeys::DistanceTessellationStepSize)) Ribbon->DistanceTessellationStepSize = std::max(0.0f, static_cast<float>(Object[ParticleKeys::DistanceTessellationStepSize].ToFloat()));
		if (Object.hasKey(ParticleKeys::bEnableTangentDiffInterpScale)) Ribbon->bEnableTangentDiffInterpScale = Object[ParticleKeys::bEnableTangentDiffInterpScale].ToBool();
		if (Object.hasKey(ParticleKeys::TangentTessellationScalar)) Ribbon->TangentTessellationScalar = std::max(0.0f, static_cast<float>(Object[ParticleKeys::TangentTessellationScalar].ToFloat()));
		if (Object.hasKey(ParticleKeys::Width)) Ribbon->Width = std::max(0.0f, static_cast<float>(Object[ParticleKeys::Width].ToFloat()));
		Ribbon->Color = ReadVectorJSON(Object, ParticleKeys::Color, Ribbon->Color);
		if (Object.hasKey(ParticleKeys::Alpha)) Ribbon->Alpha = std::clamp(static_cast<float>(Object[ParticleKeys::Alpha].ToFloat()), 0.0f, 1.0f);
		if (Object.hasKey(ParticleKeys::bUseSourceEmitter)) Ribbon->bUseSourceEmitter = Object[ParticleKeys::bUseSourceEmitter].ToBool();
		if (Object.hasKey(ParticleKeys::SourceEmitterName)) Ribbon->SourceEmitterName = FName(Object[ParticleKeys::SourceEmitterName].ToString());
		if (Object.hasKey(ParticleKeys::SourceTrailLifetime)) Ribbon->SourceTrailLifetime = std::max(0.001f, static_cast<float>(Object[ParticleKeys::SourceTrailLifetime].ToFloat()));
		if (Object.hasKey(ParticleKeys::SourceSampleInterval)) Ribbon->SourceSampleInterval = std::max(0.0f, static_cast<float>(Object[ParticleKeys::SourceSampleInterval].ToFloat()));
		if (Object.hasKey(ParticleKeys::SourceMinSampleDistance)) Ribbon->SourceMinSampleDistance = std::max(0.0f, static_cast<float>(Object[ParticleKeys::SourceMinSampleDistance].ToFloat()));
		if (Object.hasKey(ParticleKeys::SourceWidthScale)) Ribbon->SourceWidthScale = std::max(0.0f, static_cast<float>(Object[ParticleKeys::SourceWidthScale].ToFloat()));
		return Ribbon;
	}

	return nullptr;
}

UParticleModule* DeserializeModule(json::JSON& Object, UParticleLODLevel* Outer)
{
	if (!Object.hasKey(ParticleKeys::Type))
	{
		return nullptr;
	}

	const FString Type = Object[ParticleKeys::Type].ToString();
	UParticleModule* Module = nullptr;

	if (Type == "Lifetime")
	{
		UParticleModuleLifetime* Lifetime = GUObjectArray.CreateObject<UParticleModuleLifetime>(Outer);
		if (Object.hasKey(ParticleKeys::Lifetime))
		{
			Lifetime->Lifetime = std::max(0.0f, static_cast<float>(Object[ParticleKeys::Lifetime].ToFloat()));
			Lifetime->LifetimeMin = Lifetime->Lifetime;
			Lifetime->LifetimeMax = Lifetime->Lifetime;
		}
		if (Object.hasKey(ParticleKeys::LifetimeMin)) Lifetime->LifetimeMin = std::max(0.0f, static_cast<float>(Object[ParticleKeys::LifetimeMin].ToFloat()));
		if (Object.hasKey(ParticleKeys::LifetimeMax)) Lifetime->LifetimeMax = std::max(0.0f, static_cast<float>(Object[ParticleKeys::LifetimeMax].ToFloat()));
		Lifetime->LifetimeDistribution.SetUniform(Lifetime->LifetimeMin, Lifetime->LifetimeMax);
		ReadFloatDistributionJSON(Object, ParticleKeys::LifetimeDistribution, Lifetime->LifetimeDistribution);
		Module = Lifetime;
	}
	else if (Type == "InitialLocation")
	{
		UParticleModuleLocation* Location = GUObjectArray.CreateObject<UParticleModuleLocation>(Outer);
		Location->StartLocation = ReadVectorJSON(Object, ParticleKeys::StartLocation, Location->StartLocation);
		Location->StartLocationMin = ReadVectorJSON(Object, ParticleKeys::StartLocationMin, Location->StartLocation);
		Location->StartLocationMax = ReadVectorJSON(Object, ParticleKeys::StartLocationMax, Location->StartLocation);
		Location->StartLocationDistribution.SetUniform(Location->StartLocationMin, Location->StartLocationMax);
		ReadVectorDistributionJSON(Object, ParticleKeys::StartLocationDistribution, Location->StartLocationDistribution);
		Module = Location;
	}
	else if (Type == "InitialVelocity")
	{
		UParticleModuleVelocity* Velocity = GUObjectArray.CreateObject<UParticleModuleVelocity>(Outer);
		Velocity->StartVelocity = ReadVectorJSON(Object, ParticleKeys::StartVelocity, Velocity->StartVelocity);
		Velocity->StartVelocityMin = ReadVectorJSON(Object, ParticleKeys::StartVelocityMin, Velocity->StartVelocity);
		Velocity->StartVelocityMax = ReadVectorJSON(Object, ParticleKeys::StartVelocityMax, Velocity->StartVelocity);
		Velocity->StartVelocityDistribution.SetUniform(Velocity->StartVelocityMin, Velocity->StartVelocityMax);
		ReadVectorDistributionJSON(Object, ParticleKeys::StartVelocityDistribution, Velocity->StartVelocityDistribution);
		Module = Velocity;
	}
	else if (Type == "InitialRotation")
	{
		UParticleModuleInitialRotation* Rotation = GUObjectArray.CreateObject<UParticleModuleInitialRotation>(Outer);
		if (Object.hasKey(ParticleKeys::StartRotation))
		{
			Rotation->StartRotationDegrees = ReadVectorOrScalarZJSON(Object, ParticleKeys::StartRotation, Rotation->StartRotationDegrees);
			Rotation->StartRotationDegreesMin = Rotation->StartRotationDegrees;
			Rotation->StartRotationDegreesMax = Rotation->StartRotationDegrees;
		}
		if (Object.hasKey(ParticleKeys::StartRotationMin)) Rotation->StartRotationDegreesMin = ReadVectorOrScalarZJSON(Object, ParticleKeys::StartRotationMin, Rotation->StartRotationDegrees);
		if (Object.hasKey(ParticleKeys::StartRotationMax)) Rotation->StartRotationDegreesMax = ReadVectorOrScalarZJSON(Object, ParticleKeys::StartRotationMax, Rotation->StartRotationDegrees);
		Rotation->StartRotationDistribution.SetUniform(Rotation->StartRotationDegreesMin, Rotation->StartRotationDegreesMax);
		ReadVectorDistributionJSON(Object, ParticleKeys::StartRotationDistribution, Rotation->StartRotationDistribution);
		Module = Rotation;
	}
	else if (Type == "InitialRotationRate")
	{
		UParticleModuleInitialRotationRate* RotationRate = GUObjectArray.CreateObject<UParticleModuleInitialRotationRate>(Outer);
		if (Object.hasKey(ParticleKeys::StartRotationRate))
		{
			RotationRate->StartRotationRateDegrees = ReadVectorOrScalarZJSON(Object, ParticleKeys::StartRotationRate, RotationRate->StartRotationRateDegrees);
			RotationRate->StartRotationRateDegreesMin = RotationRate->StartRotationRateDegrees;
			RotationRate->StartRotationRateDegreesMax = RotationRate->StartRotationRateDegrees;
		}
		if (Object.hasKey(ParticleKeys::StartRotationRateMin)) RotationRate->StartRotationRateDegreesMin = ReadVectorOrScalarZJSON(Object, ParticleKeys::StartRotationRateMin, RotationRate->StartRotationRateDegrees);
		if (Object.hasKey(ParticleKeys::StartRotationRateMax)) RotationRate->StartRotationRateDegreesMax = ReadVectorOrScalarZJSON(Object, ParticleKeys::StartRotationRateMax, RotationRate->StartRotationRateDegrees);
		RotationRate->StartRotationRateDistribution.SetUniform(RotationRate->StartRotationRateDegreesMin, RotationRate->StartRotationRateDegreesMax);
		ReadVectorDistributionJSON(Object, ParticleKeys::StartRotationRateDistribution, RotationRate->StartRotationRateDistribution);
		Module = RotationRate;
	}
	else if (Type == "Acceleration")
	{
		UParticleModuleAcceleration* Acceleration = GUObjectArray.CreateObject<UParticleModuleAcceleration>(Outer);
		Acceleration->Acceleration = ReadVectorJSON(Object, ParticleKeys::Acceleration, Acceleration->Acceleration);
		Acceleration->AccelerationDistribution.SetConstant(Acceleration->Acceleration);
		ReadVectorDistributionJSON(Object, ParticleKeys::AccelerationDistribution, Acceleration->AccelerationDistribution);
		Module = Acceleration;
	}
	else if (Type == ParticleKeys::Orbit)
	{
		UParticleModuleOrbit* Orbit = GUObjectArray.CreateObject<UParticleModuleOrbit>(Outer);
		Orbit->Offset = ReadVectorJSON(Object, ParticleKeys::OrbitOffset, Orbit->Offset);
		Orbit->RotationDegrees = ReadVectorJSON(Object, ParticleKeys::OrbitRotation, Orbit->RotationDegrees);
		Orbit->RotationRateDegrees = ReadVectorJSON(Object, ParticleKeys::OrbitRotationRate, Orbit->RotationRateDegrees);
		Orbit->OffsetDistribution.SetConstant(Orbit->Offset);
		Orbit->RotationDistribution.SetConstant(Orbit->RotationDegrees);
		Orbit->RotationRateDistribution.SetConstant(Orbit->RotationRateDegrees);
		ReadVectorDistributionJSON(Object, ParticleKeys::OrbitOffsetDistribution, Orbit->OffsetDistribution);
		ReadVectorDistributionJSON(Object, ParticleKeys::OrbitRotationDistribution, Orbit->RotationDistribution);
		ReadVectorDistributionJSON(Object, ParticleKeys::OrbitRotationRateDistribution, Orbit->RotationRateDistribution);
		Module = Orbit;
	}
	else if (Type == "InitialColor")
	{
		UParticleModuleColor* Color = GUObjectArray.CreateObject<UParticleModuleColor>(Outer);
		Color->StartColor = ReadVectorJSON(Object, ParticleKeys::StartColor, Color->StartColor);
		Color->StartColorMin = ReadVectorJSON(Object, ParticleKeys::StartColorMin, Color->StartColor);
		Color->StartColorMax = ReadVectorJSON(Object, ParticleKeys::StartColorMax, Color->StartColor);
		if (Object.hasKey(ParticleKeys::StartAlpha))
		{
			Color->StartAlpha = std::clamp(static_cast<float>(Object[ParticleKeys::StartAlpha].ToFloat()), 0.0f, 1.0f);
			Color->StartAlphaMin = Color->StartAlpha;
			Color->StartAlphaMax = Color->StartAlpha;
		}
		if (Object.hasKey(ParticleKeys::StartAlphaMin)) Color->StartAlphaMin = std::clamp(static_cast<float>(Object[ParticleKeys::StartAlphaMin].ToFloat()), 0.0f, 1.0f);
		if (Object.hasKey(ParticleKeys::StartAlphaMax)) Color->StartAlphaMax = std::clamp(static_cast<float>(Object[ParticleKeys::StartAlphaMax].ToFloat()), 0.0f, 1.0f);
		Color->StartColorDistribution.SetUniform(Color->StartColorMin, Color->StartColorMax);
		Color->StartAlphaDistribution.SetUniform(Color->StartAlphaMin, Color->StartAlphaMax);
		ReadVectorDistributionJSON(Object, ParticleKeys::StartColorDistribution, Color->StartColorDistribution);
		ReadFloatDistributionJSON(Object, ParticleKeys::StartAlphaDistribution, Color->StartAlphaDistribution);
		Module = Color;
	}
	else if (Type == "ColorOverLife")
	{
		UParticleModuleColorOverLife* ColorOverLife = GUObjectArray.CreateObject<UParticleModuleColorOverLife>(Outer);
		ColorOverLife->ColorOverLife = ReadVectorJSON(Object, ParticleKeys::EndColor, ColorOverLife->ColorOverLife);
		if (Object.hasKey(ParticleKeys::EndAlpha))
		{
			ColorOverLife->AlphaOverLife = std::clamp(static_cast<float>(Object[ParticleKeys::EndAlpha].ToFloat()), 0.0f, 1.0f);
		}
		ColorOverLife->ColorOverLifeDistribution.SetConstant(ColorOverLife->ColorOverLife);
		ColorOverLife->AlphaOverLifeDistribution.SetConstant(ColorOverLife->AlphaOverLife);
		ReadVectorDistributionJSON(Object, ParticleKeys::ColorOverLifeDistribution, ColorOverLife->ColorOverLifeDistribution);
		ReadFloatDistributionJSON(Object, ParticleKeys::AlphaOverLifeDistribution, ColorOverLife->AlphaOverLifeDistribution);
		Module = ColorOverLife;
	}
	else if (Type == "ColorScaleOverLife")
	{
		UParticleModuleColorScaleOverLife* ColorScale = GUObjectArray.CreateObject<UParticleModuleColorScaleOverLife>(Outer);
		ColorScale->ColorScaleOverLife = ReadVectorJSON(Object, ParticleKeys::ColorScaleOverLife, ColorScale->ColorScaleOverLife);
		if (Object.hasKey(ParticleKeys::AlphaScaleOverLife))
		{
			ColorScale->AlphaScaleOverLife = (std::max)(0.0f, static_cast<float>(Object[ParticleKeys::AlphaScaleOverLife].ToFloat()));
		}
		ColorScale->ColorScaleOverLifeDistribution.SetConstant(ColorScale->ColorScaleOverLife);
		ColorScale->AlphaScaleOverLifeDistribution.SetConstant(ColorScale->AlphaScaleOverLife);
		ReadVectorDistributionJSON(Object, ParticleKeys::ColorScaleOverLifeDistribution, ColorScale->ColorScaleOverLifeDistribution);
		ReadFloatDistributionJSON(Object, ParticleKeys::AlphaScaleOverLifeDistribution, ColorScale->AlphaScaleOverLifeDistribution);
		Module = ColorScale;
	}
	else if (Type == "InitialSize")
	{
		UParticleModuleSize* Size = GUObjectArray.CreateObject<UParticleModuleSize>(Outer);
		Size->StartSize = ReadVectorJSON(Object, ParticleKeys::StartSize, Size->StartSize);
		Size->StartSizeMin = ReadVectorJSON(Object, ParticleKeys::StartSizeMin, Size->StartSize);
		Size->StartSizeMax = ReadVectorJSON(Object, ParticleKeys::StartSizeMax, Size->StartSize);
		Size->StartSizeDistribution.SetUniform(Size->StartSizeMin, Size->StartSizeMax);
		ReadVectorDistributionJSON(Object, ParticleKeys::StartSizeDistribution, Size->StartSizeDistribution);
		Module = Size;
	}
	else if (Type == "BeamSource")
	{
		UParticleModuleBeamSource* Source = GUObjectArray.CreateObject<UParticleModuleBeamSource>(Outer);
		const bool bHasSourceMethod = Object.hasKey(ParticleKeys::SourceMethod);
		if (bHasSourceMethod)
		{
			const int32 Value = static_cast<int32>(Object[ParticleKeys::SourceMethod].ToInt());
			Source->SourceMethod = static_cast<EBeam2SourceTargetMethod>(std::clamp(Value, 0, static_cast<int32>(PEB2STM_Actor)));
		}
		if (Object.hasKey(ParticleKeys::SourceName)) Source->SourceName = FName(Object[ParticleKeys::SourceName].ToString());
		if (Object.hasKey(ParticleKeys::bSourceAbsolute)) Source->bSourceAbsolute = Object[ParticleKeys::bSourceAbsolute].ToBool();
		if (Object.hasKey(ParticleKeys::bLockSource)) Source->bLockSource = Object[ParticleKeys::bLockSource].ToBool();
		if (Object.hasKey(ParticleKeys::Source))
		{
			Source->Source = ReadVectorJSON(Object, ParticleKeys::Source, Source->Source);
		}
		else if (Object.hasKey(ParticleKeys::SourcePoint))
		{
			Source->Source = ReadVectorJSON(Object, ParticleKeys::SourcePoint, Source->Source);
			if (!bHasSourceMethod)
			{
				Source->SourceMethod = PEB2STM_UserSet;
			}
		}
		if (Object.hasKey(ParticleKeys::bLockSourceTangent)) Source->bLockSourceTangent = Object[ParticleKeys::bLockSourceTangent].ToBool();
		Source->SourceTangent = ReadVectorJSON(Object, ParticleKeys::SourceTangent, Source->SourceTangent);
		if (Object.hasKey(ParticleKeys::SourceStrength)) Source->SourceStrength = std::max(0.0f, static_cast<float>(Object[ParticleKeys::SourceStrength].ToFloat()));
		Module = Source;
	}
	else if (Type == "BeamTarget")
	{
		UParticleModuleBeamTarget* Target = GUObjectArray.CreateObject<UParticleModuleBeamTarget>(Outer);
		const bool bHasTargetMethod = Object.hasKey(ParticleKeys::TargetMethod);
		if (bHasTargetMethod)
		{
			const int32 Value = static_cast<int32>(Object[ParticleKeys::TargetMethod].ToInt());
			Target->TargetMethod = static_cast<EBeam2SourceTargetMethod>(std::clamp(Value, 0, static_cast<int32>(PEB2STM_Actor)));
		}
		if (Object.hasKey(ParticleKeys::TargetName)) Target->TargetName = FName(Object[ParticleKeys::TargetName].ToString());
		if (Object.hasKey(ParticleKeys::bTargetAbsolute)) Target->bTargetAbsolute = Object[ParticleKeys::bTargetAbsolute].ToBool();
		if (Object.hasKey(ParticleKeys::bLockTarget)) Target->bLockTarget = Object[ParticleKeys::bLockTarget].ToBool();
		if (Object.hasKey(ParticleKeys::Target))
		{
			Target->Target = ReadVectorJSON(Object, ParticleKeys::Target, Target->Target);
		}
		else if (Object.hasKey(ParticleKeys::TargetPoint))
		{
			Target->Target = ReadVectorJSON(Object, ParticleKeys::TargetPoint, Target->Target);
			if (!bHasTargetMethod)
			{
				Target->TargetMethod = PEB2STM_UserSet;
			}
		}
		if (Object.hasKey(ParticleKeys::bLockTargetTangent)) Target->bLockTargetTangent = Object[ParticleKeys::bLockTargetTangent].ToBool();
		Target->TargetTangent = ReadVectorJSON(Object, ParticleKeys::TargetTangent, Target->TargetTangent);
		if (Object.hasKey(ParticleKeys::TargetStrength)) Target->TargetStrength = std::max(0.0f, static_cast<float>(Object[ParticleKeys::TargetStrength].ToFloat()));
		Module = Target;
	}
	else if (Type == "BeamNoise")
	{
		UParticleModuleBeamNoise* Noise = GUObjectArray.CreateObject<UParticleModuleBeamNoise>(Outer);
		// Legacy bLowFreq_Enabled is intentionally ignored; the module-level
		// bEnabled flag now controls whether noise is applied.
		if (Object.hasKey(ParticleKeys::Frequency)) Noise->Frequency = std::clamp(static_cast<int32>(Object[ParticleKeys::Frequency].ToInt()), 0, 64);
		else if (Object.hasKey(ParticleKeys::NoiseFrequency)) Noise->Frequency = std::clamp(static_cast<int32>(Object[ParticleKeys::NoiseFrequency].ToInt()), 0, 64);
		if (Object.hasKey(ParticleKeys::FrequencyDistance)) Noise->FrequencyDistance = std::max(0.0f, static_cast<float>(Object[ParticleKeys::FrequencyDistance].ToFloat()));
		if (Object.hasKey(ParticleKeys::NoiseRange))
		{
			Noise->NoiseRange = ReadVectorJSON(Object, ParticleKeys::NoiseRange, Noise->NoiseRange);
		}
		else if (Object.hasKey(ParticleKeys::NoiseRangeMin) || Object.hasKey(ParticleKeys::NoiseRangeMax))
		{
			const FVector RangeMin = ReadVectorJSON(Object, ParticleKeys::NoiseRangeMin, FVector::ZeroVector);
			const FVector RangeMax = ReadVectorJSON(Object, ParticleKeys::NoiseRangeMax, FVector::ZeroVector);
			Noise->NoiseRange = FVector(
				std::max(std::abs(RangeMin.X), std::abs(RangeMax.X)),
				std::max(std::abs(RangeMin.Y), std::abs(RangeMax.Y)),
				std::max(std::abs(RangeMin.Z), std::abs(RangeMax.Z)));
		}
		else if (Object.hasKey(ParticleKeys::NoiseAmplitude))
		{
			const float Amplitude = std::max(0.0f, static_cast<float>(Object[ParticleKeys::NoiseAmplitude].ToFloat()));
			Noise->NoiseRange = FVector(Amplitude, Amplitude, Amplitude);
		}
		if (Object.hasKey(ParticleKeys::NoiseSpeed)) Noise->NoiseSpeed = std::max(0.0f, static_cast<float>(Object[ParticleKeys::NoiseSpeed].ToFloat()));
		if (Object.hasKey(ParticleKeys::NoiseLockTime)) Noise->NoiseLockTime = std::max(0.0f, static_cast<float>(Object[ParticleKeys::NoiseLockTime].ToFloat()));
		if (Object.hasKey(ParticleKeys::bTargetNoise)) Noise->bTargetNoise = Object[ParticleKeys::bTargetNoise].ToBool();
		Module = Noise;
	}
	else if (Type == ParticleKeys::Collision)
	{
		UParticleModuleCollision* Collision = GUObjectArray.CreateObject<UParticleModuleCollision>(Outer);
		if (Object.hasKey(ParticleKeys::TraceChannel))
		{
			const int32 Value = static_cast<int32>(Object[ParticleKeys::TraceChannel].ToInt());
			Collision->TraceChannel = static_cast<ECollisionChannel>(std::clamp(Value, 0, NumActiveCollisionChannels - 1));
		}
		if (Object.hasKey(ParticleKeys::ResponseMode))
		{
			const int32 Value = static_cast<int32>(Object[ParticleKeys::ResponseMode].ToInt());
			Collision->ResponseMode = static_cast<EParticleCollisionResponseMode>(std::clamp(Value, 0, 2));
		}
		if (Object.hasKey(ParticleKeys::DampingFactor))
		{
			Collision->DampingFactor = std::clamp(static_cast<float>(Object[ParticleKeys::DampingFactor].ToFloat()), 0.0f, 1.0f);
		}
		if (Object.hasKey(ParticleKeys::CollisionOffset))
		{
			Collision->CollisionOffset = std::max(0.0f, static_cast<float>(Object[ParticleKeys::CollisionOffset].ToFloat()));
		}
		if (Object.hasKey(ParticleKeys::CollisionRadiusScale))
		{
			Collision->CollisionRadiusScale = std::max(0.0f, static_cast<float>(Object[ParticleKeys::CollisionRadiusScale].ToFloat()));
		}
		if (Object.hasKey(ParticleKeys::MaxCollisions))
		{
			Collision->MaxCollisions = std::max(0, static_cast<int32>(Object[ParticleKeys::MaxCollisions].ToInt()));
		}
		Module = Collision;
	}
	else if (Type == ParticleKeys::EventGenerator)
	{
		UParticleModuleEventGenerator* EventGenerator = GUObjectArray.CreateObject<UParticleModuleEventGenerator>(Outer);
		if (Object.hasKey(ParticleKeys::Events) && Object[ParticleKeys::Events].JSONType() == json::JSON::Class::Array)
		{
			for (auto& EventObject : Object[ParticleKeys::Events].ArrayRange())
			{
				FParticleEvent_GenerateInfo EventInfo;
				if (EventObject.hasKey(ParticleKeys::EventType))
				{
					const int32 Value = static_cast<int32>(EventObject[ParticleKeys::EventType].ToInt());
					EventInfo.Type = static_cast<EParticleEventType>(std::clamp(Value, 0, static_cast<int32>(EPET_MAX) - 1));
				}
				if (EventObject.hasKey(ParticleKeys::CustomName)) EventInfo.CustomName = FName(EventObject[ParticleKeys::CustomName].ToString());
				if (EventObject.hasKey(ParticleKeys::Frequency)) EventInfo.Frequency = std::max(0, static_cast<int32>(EventObject[ParticleKeys::Frequency].ToInt()));
				if (EventObject.hasKey(ParticleKeys::ParticleFrequency)) EventInfo.ParticleFrequency = std::max(0, static_cast<int32>(EventObject[ParticleKeys::ParticleFrequency].ToInt()));
				if (EventObject.hasKey(ParticleKeys::FirstTimeOnly)) EventInfo.FirstTimeOnly = EventObject[ParticleKeys::FirstTimeOnly].ToBool();
				if (EventObject.hasKey(ParticleKeys::LastTimeOnly)) EventInfo.LastTimeOnly = EventObject[ParticleKeys::LastTimeOnly].ToBool();
				if (EventObject.hasKey(ParticleKeys::UseReflectedImpactVector)) EventInfo.UseReflectedImpactVector = EventObject[ParticleKeys::UseReflectedImpactVector].ToBool();
				if (EventObject.hasKey(ParticleKeys::bUseOrbitOffset)) EventInfo.bUseOrbitOffset = EventObject[ParticleKeys::bUseOrbitOffset].ToBool();
				EventGenerator->Events.push_back(EventInfo);
			}
		}
		Module = EventGenerator;
	}
	else if (Type == ParticleKeys::EventReceiverSpawn)
	{
		UParticleModuleEventReceiverSpawn* EventReceiver = GUObjectArray.CreateObject<UParticleModuleEventReceiverSpawn>(Outer);
		if (Object.hasKey(ParticleKeys::EventGeneratorType))
		{
			const int32 Value = static_cast<int32>(Object[ParticleKeys::EventGeneratorType].ToInt());
			EventReceiver->EventGeneratorType = static_cast<EParticleEventType>(std::clamp(Value, 0, static_cast<int32>(EPET_MAX) - 1));
		}
		if (Object.hasKey(ParticleKeys::EventName)) EventReceiver->EventName = FName(Object[ParticleKeys::EventName].ToString());
		if (Object.hasKey(ParticleKeys::SpawnCount)) EventReceiver->SpawnCount = std::clamp(static_cast<int32>(Object[ParticleKeys::SpawnCount].ToInt()), 0, 1024);
		if (Object.hasKey(ParticleKeys::bSpawnOnlyOnEvent)) EventReceiver->bSpawnOnlyOnEvent = Object[ParticleKeys::bSpawnOnlyOnEvent].ToBool();
		EventReceiver->SpawnLocationOffset = ReadVectorJSON(Object, ParticleKeys::SpawnLocationOffset, EventReceiver->SpawnLocationOffset);
		if (Object.hasKey(ParticleKeys::bInheritEventVelocity)) EventReceiver->bInheritEventVelocity = Object[ParticleKeys::bInheritEventVelocity].ToBool();
		if (Object.hasKey(ParticleKeys::EventVelocityScale)) EventReceiver->EventVelocityScale = std::max(0.0f, static_cast<float>(Object[ParticleKeys::EventVelocityScale].ToFloat()));
		Module = EventReceiver;
	}

	if (Module && Object.hasKey(ParticleKeys::bEnabled))
	{
		Module->bEnabled = Object[ParticleKeys::bEnabled].ToBool();
	}
	return Module;
}

void RestoreLegacyDisabledModules(UParticleLODLevel* LOD, bool bAllowLegacyRestore)
{
	if (!bAllowLegacyRestore || !LOD || LOD->Modules.empty())
	{
		return;
	}

	bool bHasEnabledModule = false;
	for (UParticleModule* Module : LOD->Modules)
	{
		if (Module && Module->bEnabled)
		{
			bHasEnabledModule = true;
			break;
		}
	}

	if (bHasEnabledModule)
	{
		return;
	}

	// Early particle assets were saved after module support was added, but before
	// newly created/deserialized modules defaulted to enabled. Treat the "all off"
	// state as legacy data so the particle remains visible when opened.
	for (UParticleModule* Module : LOD->Modules)
	{
		if (Module)
		{
			Module->bEnabled = true;
		}
	}
}

UParticleLODLevel* DeserializeLODLevel(json::JSON& Object, UParticleEmitter* Outer, bool bAllowLegacyRestore)
{
	UParticleLODLevel* LOD = GUObjectArray.CreateObject<UParticleLODLevel>(Outer);
	LOD->SetLevelIndex(Object.hasKey(ParticleKeys::Level) ? static_cast<int32>(Object[ParticleKeys::Level].ToInt()) : 0);
	LOD->bEnabled = Object.hasKey(ParticleKeys::bEnabled) ? Object[ParticleKeys::bEnabled].ToBool() : true;

	if (Object.hasKey(ParticleKeys::Required))
	{
		LOD->RequiredModule = DeserializeRequiredModule(Object[ParticleKeys::Required], LOD);
	}
	else
	{
		LOD->RequiredModule = GUObjectArray.CreateObject<UParticleModuleRequired>(LOD);
	}

	if (Object.hasKey(ParticleKeys::Spawn))
	{
		LOD->SpawnModule = DeserializeSpawnModule(Object[ParticleKeys::Spawn], LOD);
	}
	else
	{
		LOD->SpawnModule = GUObjectArray.CreateObject<UParticleModuleSpawn>(LOD);
	}

	if (Object.hasKey(ParticleKeys::TypeData))
	{
		if (UParticleModuleTypeDataBase* TypeData = DeserializeTypeDataModule(Object[ParticleKeys::TypeData], LOD))
		{
			LOD->TypeDataModule = TypeData;
			LOD->Modules.push_back(TypeData);
		}
	}

	if (Object.hasKey(ParticleKeys::Modules))
	{
		for (auto& ModuleObject : Object[ParticleKeys::Modules].ArrayRange())
		{
			if (UParticleModule* Module = DeserializeModule(ModuleObject, LOD))
			{
				LOD->Modules.push_back(Module);
			}
		}
	}

	RestoreLegacyDisabledModules(LOD, bAllowLegacyRestore);
	RestoreMeshEmitterSizeDefaults(LOD);
	LOD->ClassifyModulesByRole();
	return LOD;
}

UParticleEmitter* DeserializeEmitter(json::JSON& Object, UParticleSystem* Outer, bool bAllowLegacyRestore)
{
	UParticleSpriteEmitter* Emitter = GUObjectArray.CreateObject<UParticleSpriteEmitter>(Outer);
	Emitter->SetEmitterName(FName(Object.hasKey(ParticleKeys::Name) ? Object[ParticleKeys::Name].ToString() : FString("Particle Emitter")));
	if (Object.hasKey(ParticleKeys::InitialAllocationCount))
	{
		Emitter->InitialAllocationCount = std::max(0, static_cast<int32>(Object[ParticleKeys::InitialAllocationCount].ToInt()));
	}
	if (Object.hasKey(ParticleKeys::PeakActiveParticles))
	{
		Emitter->PeakActiveParticles = std::max(0, static_cast<int32>(Object[ParticleKeys::PeakActiveParticles].ToInt()));
	}

	if (Object.hasKey(ParticleKeys::LODLevels))
	{
		for (auto& LODObject : Object[ParticleKeys::LODLevels].ArrayRange())
		{
			Emitter->LODLevels.push_back(DeserializeLODLevel(LODObject, Emitter, bAllowLegacyRestore));
		}
	}

	if (Emitter->LODLevels.empty())
	{
		json::JSON DefaultLOD = json::Object();
		Emitter->LODLevels.push_back(DeserializeLODLevel(DefaultLOD, Emitter, bAllowLegacyRestore));
	}

	Emitter->ClassifyModulesByRole();
	return Emitter;
}

void DeserializeParticleSystem(UParticleSystem* ParticleSystem, const FString& Payload)
{
	if (!ParticleSystem || Payload.empty())
	{
		return;
	}

	json::JSON Root = json::JSON::Load(Payload);
	if (Root.JSONType() != json::JSON::Class::Object || !Root.hasKey(ParticleKeys::Emitters))
	{
		return;
	}

	const int32 Version = Root.hasKey(ParticleKeys::Version) ? static_cast<int32>(Root[ParticleKeys::Version].ToInt()) : 1;
	const bool bAllowLegacyRestore = Version < ParticleSystemVersion;
	if (Root.hasKey(ParticleKeys::Name))
	{
		const FString AssetName = Root[ParticleKeys::Name].ToString();
		if (!AssetName.empty())
		{
			ParticleSystem->SetFName(FName(AssetName));
		}
	}

	if (Root.hasKey(ParticleKeys::LODDistances))
	{
		json::JSON& LODDistances = Root[ParticleKeys::LODDistances];
		if (LODDistances.JSONType() == json::JSON::Class::Array)
		{
			for (int32 Index = 0; Index < static_cast<int32>(LODDistances.length()); ++Index)
			{
				const float Distance = std::max(0.0f, static_cast<float>(LODDistances[Index].ToFloat()));
				if (Index == 0)
				{
					ParticleSystem->SetLODDistance(0, Distance);
				}
				else
				{
					ParticleSystem->CreateLOD(Distance);
				}
			}
		}
	}

	for (auto& EmitterObject : Root[ParticleKeys::Emitters].ArrayRange())
	{
		if (UParticleEmitter* Emitter = DeserializeEmitter(EmitterObject, ParticleSystem, bAllowLegacyRestore))
		{
			ParticleSystem->Emitters.push_back(Emitter);
		}
	}

	if (!Root.hasKey(ParticleKeys::LODDistances))
	{
		int32 MaxLODCount = ParticleSystem->GetLODCount();
		for (UParticleEmitter* Emitter : ParticleSystem->Emitters)
		{
			if (Emitter)
			{
				MaxLODCount = std::max(MaxLODCount, static_cast<int32>(Emitter->LODLevels.size()));
			}
		}
		while (ParticleSystem->GetLODCount() < MaxLODCount)
		{
			ParticleSystem->CreateLOD();
		}
	}

	ParticleSystem->NormalizeLODData();
}
}

UParticleSystem* FParticleSystemManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedParticleSystems.find(NormalizedPath);
	if (It != LoadedParticleSystems.end())
	{
		return It->second;
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath))
	{
		return nullptr;
	}

	FAssetImportMetadata Metadata;
	FString Payload;
	if (!FAssetPackage::LoadStringPayload(NormalizedPath, EAssetPackageType::ParticleSystem, Metadata, Payload))
	{
		return nullptr;
	}

	UParticleSystem* ParticleSystem = GUObjectArray.CreateObject<UParticleSystem>();
	ParticleSystem->SetAssetPathFileName(NormalizedPath);
	ParticleSystem->SetFName(FName(FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(NormalizedPath)).stem().wstring())));
	DeserializeParticleSystem(ParticleSystem, Payload);

	LoadedParticleSystems.emplace(NormalizedPath, ParticleSystem);
	return ParticleSystem;
}

UParticleSystem* FParticleSystemManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	auto It = LoadedParticleSystems.find(NormalizedPath);
	return It != LoadedParticleSystems.end() ? It->second : nullptr;
}

bool FParticleSystemManager::Save(UParticleSystem* ParticleSystem)
{
	if (!ParticleSystem)
	{
		return false;
	}

	const FString Path = FPaths::MakeProjectRelative(ParticleSystem->GetAssetPathFileName());
	if (Path.empty())
	{
		return false;
	}

	FAssetImportMetadata Metadata;
	json::JSON Root = SerializeParticleSystem(ParticleSystem);
	return FAssetPackage::SaveStringPayload(Path, EAssetPackageType::ParticleSystem, Metadata, Root.dump());
}

bool FParticleSystemManager::Rename(UParticleSystem* ParticleSystem, const FString& NewName)
{
	if (!ParticleSystem || NewName.empty())
	{
		return false;
	}

	const FString OldPathString = FPaths::MakeProjectRelative(ParticleSystem->GetAssetPathFileName());
	if (OldPathString.empty())
	{
		return false;
	}

	std::filesystem::path OldPath(FPaths::ToWide(OldPathString));
	if (!OldPath.is_absolute())
	{
		OldPath = std::filesystem::path(FPaths::RootDir()) / OldPath;
	}
	OldPath = OldPath.lexically_normal();

	std::filesystem::path NewPath = OldPath.parent_path() / (FPaths::ToWide(NewName) + L".uasset");
	NewPath = NewPath.lexically_normal();

	if (OldPath == NewPath)
	{
		ParticleSystem->SetFName(FName(NewName));
		return Save(ParticleSystem);
	}

	if (std::filesystem::exists(NewPath))
	{
		return false;
	}

	std::error_code Error;
	if (std::filesystem::exists(OldPath))
	{
		std::filesystem::rename(OldPath, NewPath, Error);
		if (Error)
		{
			return false;
		}
	}

	const FString NewPathString = FPaths::MakeProjectRelative(FPaths::ToUtf8(NewPath.generic_wstring()));
	LoadedParticleSystems.erase(OldPathString);
	ParticleSystem->SetAssetPathFileName(NewPathString);
	ParticleSystem->SetFName(FName(NewName));
	LoadedParticleSystems[NewPathString] = ParticleSystem;

	return Save(ParticleSystem);
}
