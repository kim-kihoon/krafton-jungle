#pragma once
#include "Render/Pipeline/ForwardLightData.h"
struct LightBaseParams
{
	float Intensity = 0.0f; //4
	FVector4 LightColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f); //16 
	bool bVisible = true; // 4
};
struct FGlobalAmbientLightParams : public LightBaseParams
{

};

struct FGlobalDirectionalLightParams : public LightBaseParams
{
	FVector Direction = FVector(0.0f, 0.0f, 0.0f);
};

struct FPointLightParams : public LightBaseParams
{
	FVector Position = FVector(0.0f, 0.0f, 0.0f);
	float AttenuationRadius = 0.0f;
	float LightFalloffExponent = 0.0f;
	uint32 LightType = 0;

	virtual FLightInfo ToLightInfo() const
	{
		FLightInfo Info = {};
		Info.Position = Position;
		Info.AttenuationRadius = AttenuationRadius;

		Info.Color = LightColor;
		Info.Intensity = Intensity;

		Info.Direction = FVector(0.f, 0.f, 0.f);
		Info.FalloffExponent = LightFalloffExponent;

		Info.InnerConeCos = 0.f;
		Info.OuterConeCos = 0.f;
		Info.LightType = LightType;
		Info.ShadowIndex = -1;
		return Info;
	}
};

struct FSpotLightParams : public FPointLightParams
{
	FVector Direction = FVector(0.0f, 0.0f, 0.0f);
	float InnerConeCos = 0.0f;
	float OuterConeCos = 0.0f;

	virtual FLightInfo ToLightInfo() const override
	{
		FLightInfo Info = FPointLightParams::ToLightInfo();
		Info.Direction = Direction;
		Info.InnerConeCos = InnerConeCos;
		Info.OuterConeCos = OuterConeCos;
		return Info;
	}

};
