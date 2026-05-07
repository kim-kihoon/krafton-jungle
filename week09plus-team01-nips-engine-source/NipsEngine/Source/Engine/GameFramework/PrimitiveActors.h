#pragma once

#include "AActor.h"

class UTextRenderComponent;
class UDecalComponent;

class APawnActor : public AActor
{
public:
	DECLARE_CLASS(APawnActor, AActor)
	APawnActor() = default;

	void InitDefaultComponents();
	void EnsureDefaultComponents();
};

class ASceneActor : public AActor
{
public:
	DECLARE_CLASS(ASceneActor, AActor)
	ASceneActor() = default;

	void InitDefaultComponents();
};

class AStaticMeshActor : public AActor
{
public:
	DECLARE_CLASS(AStaticMeshActor, AActor)
	AStaticMeshActor() = default;

	void InitDefaultComponents();
};

class ASubUVActor : public AActor
{
public:
    DECLARE_CLASS(ASubUVActor, AActor)
    ASubUVActor() = default;

    void InitDefaultComponents();
};

class ATextRenderActor : public AActor
{
public:
    DECLARE_CLASS(ATextRenderActor, AActor)
    ATextRenderActor() = default;

    void InitDefaultComponents();
};

class ABillboardActor : public AActor
{
public:
    DECLARE_CLASS(ABillboardActor, AActor)
	ABillboardActor() = default;

    void InitDefaultComponents();
};

class ADecalActor : public AActor
{
public:
	DECLARE_CLASS(ADecalActor, AActor)
	ADecalActor() = default;

	void InitDefaultComponents();
};

class ALightActor : public AActor {
public:
    DECLARE_CLASS(ALightActor, AActor)
    ALightActor() = default;

    void PostDuplicate(UObject* Original) override;

protected:
    void SetupBillboard(class USceneComponent* Root);
};

class ADirectionalLightActor : public ALightActor {
public:
	DECLARE_CLASS(ADirectionalLightActor, ALightActor)
	ADirectionalLightActor() = default;

	void InitDefaultComponents();
};

class AAmbientLightActor : public ALightActor {
public:
	DECLARE_CLASS(AAmbientLightActor, ALightActor)
	AAmbientLightActor() = default;

	void InitDefaultComponents();
};

class APointLightActor : public ALightActor {
public:
	DECLARE_CLASS(APointLightActor, ALightActor)
	APointLightActor() = default;

	void InitDefaultComponents();
};

class ASpotLightActor : public ALightActor {
public:
	DECLARE_CLASS(ASpotLightActor, ALightActor)
	ASpotLightActor() = default;

	void InitDefaultComponents();
};

class ASkyAtmosphereActor : public AActor {
public:
	DECLARE_CLASS(ASkyAtmosphereActor, AActor)
	ASkyAtmosphereActor() = default;

	void InitDefaultComponents();
};

class AHeightFogActor : public AActor {
public:
	DECLARE_CLASS(AHeightFogActor, AActor)
	AHeightFogActor() = default;

	void InitDefaultComponents();
};

class AAudioZoneActor : public AActor {
public:
	DECLARE_CLASS(AAudioZoneActor, AActor)
	AAudioZoneActor() = default;

	void InitDefaultComponents();
};

class APlayerStartActor : public AActor {
public:
	DECLARE_CLASS(APlayerStartActor, AActor)
	APlayerStartActor() = default;

	void InitDefaultComponents();
};
