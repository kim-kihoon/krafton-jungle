#include "AReflectionTestActor.h"

void AReflectionTestActor::InitDefaultComponents()
{
    auto root = AddComponent<USceneComponent>();
    SetRootComponent(root);
}
