#pragma once

#include "Core/CoreTypes.h"
#include "Render/Types/FogParams.h"
#include "Render/Types/GlobalLightParams.h"

class UHeightFogComponent;
class UAmbientLightComponent;
class UDirectionalLightComponent;
class UPointLightComponent;
class USpotLightComponent;

// ============================================================
// FSceneEnvironment — Fog / Light 환경 데이터 관리
// ============================================================
// FScene에서 분리된 환경 렌더 데이터 컨테이너.
// Component의 Add/Remove 생명주기에 연동되며,
// Owner 포인터는 역참조 없이 lookup key로만 사용한다.
class FSceneEnvironment
{
public:
	// --- Height Fog ---
	void AddFog(const UHeightFogComponent* Owner, const FFogParams& Params);
	void RemoveFog(const UHeightFogComponent* Owner);
	bool HasFog() const { return !Fogs.empty(); }
	const FFogParams& GetFogParams() const { return Fogs[0].Params; }

	// --- Global Ambient Light (배열 관리, 렌더링은 [0]만 사용) ---
	void AddGlobalAmbientLight(const UAmbientLightComponent* Owner, const FGlobalAmbientLightParams& Params);
	void RemoveGlobalAmbientLight(const UAmbientLightComponent* Owner);
	bool HasGlobalAmbientLight() const { return !AmbientLights.empty(); }
	uint32 GetNumAmbientLights() const { return static_cast<uint32>(AmbientLights.size()); }
	const FGlobalAmbientLightParams& GetGlobalAmbientLightParams() const { return AmbientLights[0].Params; }

	// --- Global Directional Light (배열 관리, 렌더링은 [0]만 사용) ---
	void AddGlobalDirectionalLight(const UDirectionalLightComponent* Owner, const FGlobalDirectionalLightParams& Params);
	void RemoveGlobalDirectionalLight(const UDirectionalLightComponent* Owner);
	bool HasGlobalDirectionalLight() const { return !DirectionalLights.empty(); }
	uint32 GetNumDirectionalLights() const { return static_cast<uint32>(DirectionalLights.size()); }
	const FGlobalDirectionalLightParams& GetGlobalDirectionalLightParams() const { return DirectionalLights[0].Params; }
	const UDirectionalLightComponent* GetGlobalDirectionalLightOwner() const { return DirectionalLights.empty() ? nullptr : DirectionalLights[0].Owner; }

	// --- Point Lights ---
	void AddPointLight(const UPointLightComponent* Owner, const FPointLightParams& Params);
	void RemovePointLight(const UPointLightComponent* Owner);
	uint32 GetNumPointLights() const { return static_cast<uint32>(PointLights.size()); }
	const FPointLightParams& GetPointLight(uint32 Index) const { return PointLights[Index].Params; }
	const UPointLightComponent* GetPointLightOwner(uint32 Index) const { return PointLights[Index].Owner; }

	// --- Spot Lights ---
	void AddSpotLight(const USpotLightComponent* Owner, const FSpotLightParams& Params);
	void RemoveSpotLight(const USpotLightComponent* Owner);
	uint32 GetNumSpotLights() const { return static_cast<uint32>(SpotLights.size()); }
	const FSpotLightParams& GetSpotLight(uint32 Index) const { return SpotLights[Index].Params; }
	const USpotLightComponent* GetSpotLightOwner(uint32 Index) const { return SpotLights[Index].Owner; }

private:
	// --- Entry 구조체 (Owner는 lookup key 전용, 역참조 없음) ---
	struct FFogEntry
	{
		const UHeightFogComponent* Owner = nullptr;
		FFogParams Params;
	};

	struct FAmbientLightEntry
	{
		const UAmbientLightComponent* Owner = nullptr;
		FGlobalAmbientLightParams Params;
	};

	struct FDirectionalLightEntry
	{
		const UDirectionalLightComponent* Owner = nullptr;
		FGlobalDirectionalLightParams Params;
	};

	struct FPointLightEntry
	{
		const UPointLightComponent* Owner = nullptr;
		FPointLightParams Params;
	};

	struct FSpotLightEntry
	{
		const USpotLightComponent* Owner = nullptr;
		FSpotLightParams Params;
	};

	// --- 데이터 ---
	TArray<FFogEntry>              Fogs;
	TArray<FAmbientLightEntry>     AmbientLights;
	TArray<FDirectionalLightEntry> DirectionalLights;
	TArray<FPointLightEntry>       PointLights;
	TArray<FSpotLightEntry>  SpotLights;
};
