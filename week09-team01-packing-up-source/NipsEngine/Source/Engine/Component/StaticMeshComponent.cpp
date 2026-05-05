#include "StaticMeshComponent.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include "Core/ResourceManager.h"

DEFINE_CLASS(UStaticMeshComponent, UMeshComponent)
REGISTER_FACTORY(UStaticMeshComponent)

UStaticMeshComponent::UStaticMeshComponent()
{
    //	기본 도형은 Cube로 설정
    SetStaticMesh(FResourceManager::Get().LoadStaticMesh("Asset\\Mesh\\Dice\\Dice.obj"));
}

// 프로퍼티 시스템에 노출되지 않은 필드를 직접 복사합니다.
// StaticMeshAsset·OverrideMaterial 은 얕은 복사로 동일한 원본 리소스를 참조하게 합니다.
void UStaticMeshComponent::PostDuplicate(UObject* Original)
{
    UMeshComponent::PostDuplicate(Original);

    const UStaticMeshComponent* Orig = Cast<UStaticMeshComponent>(Original);
    StaticMeshAsset = Orig->StaticMeshAsset;
    bNormalizeOnImport = Orig->bNormalizeOnImport;
    bBoundsDirty = true;
    bRenderStateDirty = true;

	Materials = TArray<UMaterialInterface*>(Orig->Materials.size());
	for (int32 i = 0; i < static_cast<int32>(Orig->Materials.size()); ++i)
	{
		if (UMaterialInstance* OrigMatInst = Cast<UMaterialInstance>(Orig->Materials[i]))
		{
			UMaterialInstance* MatInst = UMaterialInstance::CreateTransient(OrigMatInst->Parent);
			MatInst->OverridedParams = OrigMatInst->OverridedParams;
			if (OrigMatInst->HasLightingModelOverride())
			{
				MatInst->SetLightingModelOverride(OrigMatInst->GetLightingModelOverride());
			}
			else
			{
				MatInst->ClearLightingModelOverride();
			}
			Materials[i] = MatInst;
		}
		else
		{
			Materials[i] = Orig->Materials[i]; // 얕은 복사 — ResourceManager 가 소유
		}
	}
}

void UStaticMeshComponent::Serialize(FArchive& Ar)
{
	UMeshComponent::Serialize(Ar);
	Ar << "ObjStaticMeshAsset" << StaticMeshAssetPath;
	Ar << "NormalizeOnImport" << bNormalizeOnImport;

	if (Ar.IsLoading())
	{
		TArray<UMaterialInterface*> SavedMaterials = Materials;

		if (!StaticMeshAssetPath.empty())
		{
			SetStaticMesh(FResourceManager::Get().LoadStaticMesh(StaticMeshAssetPath, bNormalizeOnImport));
		}
		else
		{
			SetStaticMesh(nullptr);
		}

		// StaticMesh 로드 시 기본 슬롯 material 이 다시 채워지므로
		// scene 에 저장된 override 를 마지막에 복원합니다.
		const int32 RestoreCount = static_cast<int32>(std::min(SavedMaterials.size(), Materials.size()));
		for (int32 i = 0; i < RestoreCount; ++i)
		{
			// 구형 scene 에서 빈 문자열은 "override 없음" 의미로 저장되므로
			// null 슬롯은 mesh 기본 material 을 유지합니다.
			if (SavedMaterials[i] != nullptr)
			{
				SetMaterial(i, SavedMaterials[i]);
			}
		}
	}
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InStaticMesh)
{
    if (StaticMeshAsset == InStaticMesh)
    {
        return;
    }

    StaticMeshAsset = InStaticMesh;
	ReleaseOwnedMaterialInstances();
    Materials.clear();

    if (StaticMeshAsset != nullptr)
    {
        StaticMeshAssetPath = StaticMeshAsset->GetAssetPathFileName();

		const auto& Slots = StaticMeshAsset->GetMaterialSlots();
        const auto& Sections = StaticMeshAsset->GetSections();
        Materials.reserve(Sections.size());

        for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
        {
            Materials.push_back(Slots[Sections[i].MaterialSlotIndex].Material);
        }
    }
    else
    {
        StaticMeshAssetPath.clear();
    }

    MarkBoundsDirty();
    MarkRenderStateDirty();
}

UStaticMesh* UStaticMeshComponent::GetStaticMesh() const
{
    return StaticMeshAsset;
}

bool UStaticMeshComponent::HasValidMesh() const
{
    return StaticMeshAsset != nullptr && StaticMeshAsset->HasValidMeshData();
}


void UStaticMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UMeshComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "StaticMesh", EPropertyType::String, &StaticMeshAssetPath });
    OutProps.push_back({ "Normalize On Import", EPropertyType::Bool, &bNormalizeOnImport });
}

void UStaticMeshComponent::PostEditProperty(const char* PropertyName)
{
    UMeshComponent::PostEditProperty(PropertyName);

    //	추후에 FName으로 바꿔도 될 듯 싶긴한데 보류
    if (std::strcmp(PropertyName, "StaticMesh") == 0)
    {
		if (StaticMeshAssetPath.empty())
		{
			SetStaticMesh(nullptr);
			return;
		}

		UStaticMesh* Mesh = FResourceManager::Get().LoadStaticMesh(StaticMeshAssetPath, bNormalizeOnImport);
		SetStaticMesh(Mesh);
    }
	else if (std::strcmp(PropertyName, "Normalize On Import") == 0)
	{
		if (!StaticMeshAssetPath.empty())
		{
			UStaticMesh* Mesh = FResourceManager::Get().LoadStaticMesh(StaticMeshAssetPath, bNormalizeOnImport);
			SetStaticMesh(Mesh);
		}
	}
	else if (std::strcmp(PropertyName, "Materials") == 0)
	{
		for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
		{
			SetMaterial(i, Materials[i]);
		}
	}
}

void UStaticMeshComponent::UpdateWorldAABB() const
{
    WorldAABB.Reset();

    if (!HasValidMesh())
    {
        bBoundsDirty = false;
        return;
    }

    const FAABB& LocalBounds = StaticMeshAsset->GetLocalBounds();
    if (!LocalBounds.IsValid())
    {
        bBoundsDirty = false;
        return;
    }

    const FVector LocalCorners[8] = {
        FVector(LocalBounds.Min.X, LocalBounds.Min.Y, LocalBounds.Min.Z),
        FVector(LocalBounds.Max.X, LocalBounds.Min.Y, LocalBounds.Min.Z),
        FVector(LocalBounds.Min.X, LocalBounds.Max.Y, LocalBounds.Min.Z),
        FVector(LocalBounds.Max.X, LocalBounds.Max.Y, LocalBounds.Min.Z),
        FVector(LocalBounds.Min.X, LocalBounds.Min.Y, LocalBounds.Max.Z),
        FVector(LocalBounds.Max.X, LocalBounds.Min.Y, LocalBounds.Max.Z),
        FVector(LocalBounds.Min.X, LocalBounds.Max.Y, LocalBounds.Max.Z),
        FVector(LocalBounds.Max.X, LocalBounds.Max.Y, LocalBounds.Max.Z)
    };

    const FMatrix& WorldMatrix = GetWorldMatrix();

    for (const FVector& Corner : LocalCorners)
    {
        const FVector WorldPos = WorldMatrix.TransformPosition(Corner);
        WorldAABB.Expand(WorldPos);
    }

    bBoundsDirty = false;
}

//	Ray를 Local로 바꿔서 확인
//	모든 Mesh를 World로 바꾸는 것보다 훨씬 빠름
bool UStaticMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
    if (!HasValidMesh())
    {
        return false;
    }

    EnsureBoundsUpdated();

    float BoxT = 0.0f;
    if (!WorldAABB.IntersectRay(Ray, BoxT))
    {
        return false;
    }

    const TArray<FNormalVertex>& Vertices = StaticMeshAsset->GetVertices();
    const TArray<uint32>& Indices = StaticMeshAsset->GetIndices();

    if (Vertices.empty() || Indices.empty())
    {
        return false;
    }

    const FMatrix InvWorld = GetWorldMatrix().GetInverse();

    FRay LocalRay = Ray;
    LocalRay.Origin = InvWorld.TransformPosition(LocalRay.Origin);
    LocalRay.Direction = InvWorld.TransformVector(LocalRay.Direction);
    LocalRay.Direction.NormalizeSafe();

    bool bHit = false;
    float ClosestT = FLT_MAX;
    int32 BestFaceIndex = -1;
    FVector BestLocalNormal = FVector::ZeroVector;

    for (uint32 i = 0; i + 2 < static_cast<uint32>(Indices.size()); i += 3)
    {
        const uint32 I0 = Indices[i];
        const uint32 I1 = Indices[i + 1];
        const uint32 I2 = Indices[i + 2];

        if (I0 >= Vertices.size() || I1 >= Vertices.size() || I2 >= Vertices.size())
        {
            continue;
        }

        const FVector& V0 = Vertices[I0].Position;
        const FVector& V1 = Vertices[I1].Position;
        const FVector& V2 = Vertices[I2].Position;

        float HitT = 0.0f;
        if (IntersectTriangle(LocalRay.Origin, LocalRay.Direction, V0, V1, V2, HitT))
        {
            if (HitT < ClosestT)
            {
                ClosestT = HitT;
                bHit = true;
                BestFaceIndex = static_cast<int32>(i / 3);

                const FVector Edge1 = V1 - V0;
                const FVector Edge2 = V2 - V0;
                BestLocalNormal = FVector::CrossProduct(Edge1, Edge2).GetSafeNormal();
            }
        }
    }

    if (!bHit)
    {
        return false;
    }

    const FVector LocalHitLocation = LocalRay.Origin + LocalRay.Direction * ClosestT;
    const FVector WorldHitLocation = GetWorldMatrix().TransformPosition(LocalHitLocation);
    FVector WorldNormal = GetWorldMatrix().TransformVector(BestLocalNormal);
    WorldNormal.NormalizeSafe();

    OutHitResult.bHit = true;
    OutHitResult.HitComponent = this;
    OutHitResult.Distance = (WorldHitLocation - Ray.Origin).Size();
    OutHitResult.Location = WorldHitLocation;
    OutHitResult.Normal = WorldNormal;
    OutHitResult.FaceIndex = BestFaceIndex;

    return true;
}

const FAABB& UStaticMeshComponent::GetWorldAABB() const
{
    EnsureBoundsUpdated();
    return WorldAABB;
}

bool UStaticMeshComponent::ConsumeRenderStateDirty()
{
    const bool bWasDirty = bRenderStateDirty;
    bRenderStateDirty = false;
    return bWasDirty;
}

void UStaticMeshComponent::MarkBoundsDirty()
{
    bBoundsDirty = true;
}

void UStaticMeshComponent::MarkRenderStateDirty()
{
    bRenderStateDirty = true;
}

void UStaticMeshComponent::EnsureBoundsUpdated() const
{
    if (!bBoundsDirty && !bTransformDirty)
    {
        return;
    }

    if (bTransformDirty)
    {
        (void)GetWorldMatrix();
        return;
    }

    const_cast<UStaticMeshComponent*>(this)->UpdateWorldAABB();
}
