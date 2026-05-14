#pragma once
#include "SkinnedMeshComponent.h"

/* ―――― USkeletalMeshComponent ―――― */
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

    USkeletalMeshComponent() = default;
    ~USkeletalMeshComponent() override = default;

    void RefreshBoneTransforms() override;
    void TickComponent(float DeltaTime) override;
    void Serialize(FArchive& Ar) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

private:
};
