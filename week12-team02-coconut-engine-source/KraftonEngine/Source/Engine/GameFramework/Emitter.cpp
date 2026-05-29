#include "GameFramework/Emitter.h"

#include "Component/BillboardComponent.h"
#include "Component/ParticleSystemComponent.h"
#include "Materials/MaterialManager.h"

AEmitter::AEmitter()
{
	bNeedsTick = true;
	bTickInEditor = true;
}

void AEmitter::InitDefaultComponents(UParticleSystem* ParticleSystem)
{
	if (!ParticleSystemComponent)
	{
		ParticleSystemComponent = AddComponent<UParticleSystemComponent>();
		SetRootComponent(ParticleSystemComponent);
	}

	if (!SpriteComponent)
	{
		SpriteComponent = AddComponent<UBillboardComponent>();
		SpriteComponent->AttachToComponent(ParticleSystemComponent);
		SpriteComponent->SetAbsoluteScale(true);
		SpriteComponent->SetEditorOnlyComponent(true);
		SpriteComponent->SetHiddenInComponentTree(true);
		SpriteComponent->SetMaterial(FMaterialManager::Get().GetOrCreateMaterial("Asset/Materials/Editor/Emitter.mat"));
	}

	if (ParticleSystem)
	{
		ParticleSystemComponent->SetTemplate(ParticleSystem);
	}
}
