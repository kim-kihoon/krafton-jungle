#include "SkeletalMesh.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Asset/AssetPackage.h"
#include "Serialization/Archive.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Animation/Skeleton/SkeletonManager.h"
#include "Core/Logging/Log.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"
#include "Engine/Profiling/Stats/MemoryStats.h"

#include <utility>

namespace
{
    void SetClothError(FString* OutError, const FString& Message)
    {
        if (OutError)
        {
            *OutError = Message;
        }
    }

    bool IsTriangleDegenerate(uint32 A, uint32 B, uint32 C)
    {
        return A == B || B == C || A == C;
    }
}

FSkeletalClothLODData* FSkeletalMesh::FindClothLOD(uint32 LODIndex)
{
    for (FSkeletalClothLODData& LODData : ClothPayload.LODs)
    {
        if (LODData.LODIndex == LODIndex)
        {
            return &LODData;
        }
    }
    return nullptr;
}

const FSkeletalClothLODData* FSkeletalMesh::FindClothLOD(uint32 LODIndex) const
{
    for (const FSkeletalClothLODData& LODData : ClothPayload.LODs)
    {
        if (LODData.LODIndex == LODIndex)
        {
            return &LODData;
        }
    }
    return nullptr;
}

FSkeletalClothLODData& FSkeletalMesh::FindOrAddClothLOD(uint32 LODIndex)
{
    if (FSkeletalClothLODData* Existing = FindClothLOD(LODIndex))
    {
        return *Existing;
    }

    FSkeletalClothLODData NewLOD;
    NewLOD.LODIndex = LODIndex;
    ClothPayload.LODs.push_back(std::move(NewLOD));
    return ClothPayload.LODs.back();
}

FSkeletalClothData* FSkeletalMesh::FindClothData(uint32 LODIndex, const FString& Name)
{
    FSkeletalClothLODData* LODData = FindClothLOD(LODIndex);
    if (!LODData)
    {
        return nullptr;
    }

    for (FSkeletalClothData& Cloth : LODData->Cloths)
    {
        if (Cloth.Name == Name)
        {
            return &Cloth;
        }
    }
    return nullptr;
}

const FSkeletalClothData* FSkeletalMesh::FindClothData(uint32 LODIndex, const FString& Name) const
{
    const FSkeletalClothLODData* LODData = FindClothLOD(LODIndex);
    if (!LODData)
    {
        return nullptr;
    }

    for (const FSkeletalClothData& Cloth : LODData->Cloths)
    {
        if (Cloth.Name == Name)
        {
            return &Cloth;
        }
    }
    return nullptr;
}

FSkeletalClothData* FSkeletalMesh::AddOrReplaceClothData(FSkeletalClothData&& ClothData)
{
    FSkeletalClothLODData& LODData = FindOrAddClothLOD(ClothData.Binding.LODIndex);
    for (FSkeletalClothData& Existing : LODData.Cloths)
    {
        if (Existing.Name == ClothData.Name)
        {
            Existing = std::move(ClothData);
            return &Existing;
        }
    }

    LODData.Cloths.push_back(std::move(ClothData));
    return &LODData.Cloths.back();
}

bool FSkeletalMesh::RemoveClothDataForSection(uint32 LODIndex, int32 SectionIndex)
{
    FSkeletalClothLODData* LODData = FindClothLOD(LODIndex);
    if (!LODData)
    {
        return false;
    }

    bool bRemoved = false;
    for (auto It = LODData->Cloths.begin(); It != LODData->Cloths.end();)
    {
        if (It->Binding.LODIndex == LODIndex && It->Binding.SectionIndex == SectionIndex)
        {
            It = LODData->Cloths.erase(It);
            bRemoved = true;
        }
        else
        {
            ++It;
        }
    }

    if (bRemoved && LODData->Cloths.empty())
    {
        for (auto It = ClothPayload.LODs.begin(); It != ClothPayload.LODs.end(); ++It)
        {
            if (It->LODIndex == LODIndex)
            {
                ClothPayload.LODs.erase(It);
                break;
            }
        }
    }

    return bRemoved;
}

bool FSkeletalMesh::BuildClothDataFromSection(
    uint32 LODIndex,
    int32 SectionIndex,
    const FString& Name,
    FSkeletalClothData& OutCloth,
    FString* OutError
) const
{
    if (SectionIndex < 0 || SectionIndex >= static_cast<int32>(Sections.size()))
    {
        SetClothError(OutError, "invalid cloth section index");
        return false;
    }

    const FSkeletalMeshSection& Section = Sections[SectionIndex];
    if (Section.IndexCount == 0 || Section.IndexCount % 3 != 0)
    {
        SetClothError(OutError, "cloth section index count must be a non-zero triangle list");
        return false;
    }
    if (Section.FirstIndex > Indices.size() || Section.IndexCount > Indices.size() - Section.FirstIndex)
    {
        SetClothError(OutError, "cloth section index range is outside the skeletal mesh index buffer");
        return false;
    }

    FSkeletalClothData NewCloth;
    NewCloth.Name = Name.empty() ? Section.MaterialSlotName : Name;
    NewCloth.Binding.LODIndex = LODIndex;
    NewCloth.Binding.SectionIndex = SectionIndex;
    NewCloth.Binding.MaterialSlotName = Section.MaterialSlotName;
    NewCloth.Binding.FirstIndex = Section.FirstIndex;
    NewCloth.Binding.IndexCount = Section.IndexCount;
    NewCloth.Binding.SourceVertexCount = static_cast<uint32>(Vertices.size());
    NewCloth.Binding.SourceIndexCount = static_cast<uint32>(Indices.size());

    TMap<uint32, uint32> RenderToParticle;
    NewCloth.ClothLocalIndices.reserve(Section.IndexCount);

    const uint32 LastIndex = Section.FirstIndex + Section.IndexCount;
    for (uint32 IndexOffset = Section.FirstIndex; IndexOffset < LastIndex; IndexOffset += 3)
    {
        const uint32 Triangle[3] =
        {
            Indices[IndexOffset + 0],
            Indices[IndexOffset + 1],
            Indices[IndexOffset + 2],
        };

        if (IsTriangleDegenerate(Triangle[0], Triangle[1], Triangle[2]))
        {
            SetClothError(OutError, "cloth section contains a degenerate triangle");
            return false;
        }

        for (uint32 Corner = 0; Corner < 3; ++Corner)
        {
            const uint32 RenderVertexIndex = Triangle[Corner];
            if (RenderVertexIndex >= Vertices.size())
            {
                SetClothError(OutError, "cloth section references a vertex outside the skeletal mesh vertex buffer");
                return false;
            }

            auto It = RenderToParticle.find(RenderVertexIndex);
            if (It == RenderToParticle.end())
            {
                const uint32 ParticleIndex = static_cast<uint32>(NewCloth.RenderVertexIndices.size());
                RenderToParticle[RenderVertexIndex] = ParticleIndex;
                NewCloth.RenderVertexIndices.push_back(RenderVertexIndex);
                NewCloth.ParticleToRenderVertex.push_back(RenderVertexIndex);
                NewCloth.ClothLocalIndices.push_back(ParticleIndex);
            }
            else
            {
                NewCloth.ClothLocalIndices.push_back(It->second);
            }
        }
    }

    NewCloth.Paint.MaxDistanceValues.assign(NewCloth.ParticleToRenderVertex.size(), 0.0f);
    OutCloth = std::move(NewCloth);
    return true;
}

bool FSkeletalMesh::ValidateClothData(const FSkeletalClothData& ClothData, FString* OutError) const
{
    const int32 SectionIndex = FindSectionForClothBinding(ClothData.Binding);
    if (SectionIndex < 0)
    {
        SetClothError(OutError, "cloth binding no longer matches a skeletal mesh section");
        return false;
    }

    if (ClothData.Binding.SourceVertexCount != static_cast<uint32>(Vertices.size()) ||
        ClothData.Binding.SourceIndexCount != static_cast<uint32>(Indices.size()))
    {
        SetClothError(OutError, "cloth binding source vertex/index signature is stale");
        return false;
    }

    if (ClothData.RenderVertexIndices.size() != ClothData.ParticleToRenderVertex.size())
    {
        SetClothError(OutError, "cloth render vertex and particle mapping sizes differ");
        return false;
    }
    if (ClothData.Paint.MaxDistanceValues.size() != ClothData.ParticleToRenderVertex.size())
    {
        SetClothError(OutError, "cloth paint value count does not match particle count");
        return false;
    }
    if (ClothData.ClothLocalIndices.empty() || ClothData.ClothLocalIndices.size() % 3 != 0)
    {
        SetClothError(OutError, "cloth local index buffer is not a triangle list");
        return false;
    }

    for (uint32 RenderVertexIndex : ClothData.ParticleToRenderVertex)
    {
        if (RenderVertexIndex >= Vertices.size())
        {
            SetClothError(OutError, "cloth particle maps to a vertex outside the skeletal mesh vertex buffer");
            return false;
        }
    }

    for (uint32 LocalIndex : ClothData.ClothLocalIndices)
    {
        if (LocalIndex >= ClothData.ParticleToRenderVertex.size())
        {
            SetClothError(OutError, "cloth local triangle index is outside the particle range");
            return false;
        }
    }

    return true;
}

int32 FSkeletalMesh::FindSectionForClothBinding(const FSkeletalClothSectionBinding& Binding) const
{
    if (Binding.SectionIndex >= 0 && Binding.SectionIndex < static_cast<int32>(Sections.size()))
    {
        const FSkeletalMeshSection& Section = Sections[Binding.SectionIndex];
        if (Section.FirstIndex == Binding.FirstIndex &&
            Section.IndexCount == Binding.IndexCount &&
            (Binding.MaterialSlotName.empty() || Section.MaterialSlotName == Binding.MaterialSlotName))
        {
            return Binding.SectionIndex;
        }
    }

    for (int32 SectionIndex = 0; SectionIndex < static_cast<int32>(Sections.size()); ++SectionIndex)
    {
        const FSkeletalMeshSection& Section = Sections[SectionIndex];
        if (Section.FirstIndex == Binding.FirstIndex &&
            Section.IndexCount == Binding.IndexCount &&
            (Binding.MaterialSlotName.empty() || Section.MaterialSlotName == Binding.MaterialSlotName))
        {
            return SectionIndex;
        }
    }

    return -1;
}

USkeletalMesh::~USkeletalMesh()
{
    ReleaseTrackedMemory();
}

void USkeletalMesh::Serialize(FArchive& Ar)
{
    SerializeCurrentPayload(Ar, FAssetPackageHeader::CurrentVersion);
}

void USkeletalMesh::SerializeLegacyPayload(FArchive& Ar)
{
    if (Ar.IsLoading())
    {
        ReleaseTrackedMemory();
        if (!SkeletalMeshAsset)
        {
            SkeletalMeshAsset = std::make_unique<FSkeletalMesh>();
        }
    }
    else if (!SkeletalMeshAsset)
    {
        SkeletalMeshAsset = std::make_unique<FSkeletalMesh>();
    }

    if (Ar.IsSaving())
    {
        SyncSkeletonBindingToAsset();
    }

    Ar << SkeletalMeshAsset->PathFileName;
    Ar << SkeletalMeshAsset->SkeletonPath;
    Ar << SkeletalMeshAsset->SkeletonAssetGuid;
    Ar << SkeletalMeshAsset->SkeletonCompatibilitySignature;
    Ar << SkeletalMeshAsset->Vertices;
    Ar << SkeletalMeshAsset->Indices;
    Ar << SkeletalMeshAsset->Sections;
    Ar << SkeletalMeshAsset->MeshRanges;
    Ar << SkeletalMeshAsset->Bones;
    Ar << SkeletalMaterials;
    Ar << SkeletalMeshAsset->MorphTargets;

    if (Ar.IsLoading())
    {
        SkeletalMeshAsset->ClothPayload = FSkeletalMeshClothPayload();
        SkeletalMeshAsset->NormalizeBonePoseData();
        SyncSkeletonBindingFromAsset();
        PhysicsAssetPath = "None";
        PhysicsAsset.Reset();
        CacheSectionMaterialIndices();
        SkeletalMeshAsset->bBoundsValid = false;
    }
}

void USkeletalMesh::SerializeCurrentPayload(FArchive& Ar, uint32 PackageVersion)
{
    SerializeLegacyPayload(Ar);
    Ar << PhysicsAssetPath;

    if (PackageVersion >= static_cast<uint32>(EAssetPackageSerializationVersion::SkeletalMeshClothPayload))
    {
        SerializeClothPayload(Ar);
    }
    else if (Ar.IsLoading() && SkeletalMeshAsset)
    {
        SkeletalMeshAsset->ClothPayload = FSkeletalMeshClothPayload();
    }

    if (Ar.IsLoading())
    {
        if (PhysicsAssetPath.empty())
        {
            PhysicsAssetPath = "None";
        }
        PhysicsAsset.Reset();
    }
}

void USkeletalMesh::SerializeClothPayload(FArchive& Ar)
{
    if (!SkeletalMeshAsset)
    {
        if (Ar.IsLoading())
        {
            SkeletalMeshAsset = std::make_unique<FSkeletalMesh>();
        }
        else
        {
            return;
        }
    }

    Ar << SkeletalMeshAsset->ClothPayload;
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InMesh)
{
    if (SkeletalMeshAsset.get() == InMesh)
    {
        CacheSectionMaterialIndices();
        return;
    }

    SetSkeletalMeshAsset(std::unique_ptr<FSkeletalMesh>(InMesh));
}

void USkeletalMesh::SetSkeletalMeshAsset(std::unique_ptr<FSkeletalMesh> InMesh)
{
    if (SkeletalMeshAsset.get() == InMesh.get())
    {
        InMesh.release();
        CacheSectionMaterialIndices();
        return;
    }

    ReleaseTrackedMemory();
    SkeletalMeshAsset = std::move(InMesh);

    if (SkeletalMeshAsset)
    {
        SkeletalMeshAsset->NormalizeBonePoseData();
        SkeletalMeshAsset->bBoundsValid = false;
    }
    SyncSkeletonBindingFromAsset();
    if (PhysicsAsset.Get())
    {
        FSkeletonCompatibilityReport Report;
        if (!CanUsePhysicsAsset(PhysicsAsset.Get(), &Report))
        {
            UE_LOG("SkeletalMesh default PhysicsAsset cleared after mesh asset change. Mesh=%s PhysicsAsset=%s Reason=%s",
                GetName().c_str(),
                PhysicsAsset->GetName().c_str(),
                Report.Reason.c_str());
            ClearPhysicsAsset();
        }
    }
    CacheSectionMaterialIndices();
}

FSkeletalMesh* USkeletalMesh::GetSkeletalMeshAsset() const
{
    return SkeletalMeshAsset.get();
}

void USkeletalMesh::SetSkeletalMaterials(TArray<FSkeletalMaterial>&& InMaterials)
{
    SkeletalMaterials = std::move(InMaterials);
    CacheSectionMaterialIndices();
}

const TArray<FSkeletalMaterial>& USkeletalMesh::GetSkeletalMaterials() const
{
    return SkeletalMaterials;
}

void USkeletalMesh::InitResources(ID3D11Device* InDevice)
{
    if (!InDevice || !SkeletalMeshAsset) return;

    if (!bMemoryTracked)
    {
        TrackedMemorySize = CalculateTrackedMemorySize();
        MemoryStats::AddSkeletalMeshCPUMemory(TrackedMemorySize);
        bMemoryTracked = true;
    }

    TMeshData<FVertexPNCTBW> RenderMeshData;
    RenderMeshData.Vertices.reserve(SkeletalMeshAsset->Vertices.size());

    for (const FVertexPNCTBW& RawVert : SkeletalMeshAsset->Vertices)
    {
        FVertexPNCTBW RenderVert;
        RenderVert.Position = RawVert.Position;
        RenderVert.Normal   = RawVert.Normal;
        RenderVert.Color    = RawVert.Color;
        RenderVert.UV       = RawVert.UV;
        RenderVert.Tangent  = RawVert.Tangent;
        std::copy(std::begin(RawVert.BoneIndices), std::end(RawVert.BoneIndices), std::begin(RenderVert.BoneIndices));
        std::copy(std::begin(RawVert.BoneWeights), std::end(RawVert.BoneWeights), std::begin(RenderVert.BoneWeights));
        RenderMeshData.Vertices.push_back(RenderVert);
    }
    RenderMeshData.Indices = SkeletalMeshAsset->Indices;

    SkeletalMeshAsset->RenderBuffer = std::make_unique<FMeshBuffer>();
    SkeletalMeshAsset->RenderBuffer->Create(InDevice, RenderMeshData);
}

void USkeletalMesh::SetSkeleton(USkeleton* InSkeleton)
{
    Skeleton = InSkeleton;

    USkeleton* ValidSkeleton = Skeleton.Get();
    if (ValidSkeleton)
    {
        SetSkeletonBinding(ValidSkeleton->GetSkeletonBinding());
    }
    else
    {
        FSkeletonBinding EmptyBinding;
        SetSkeletonBinding(EmptyBinding);
    }
}

USkeleton* USkeletalMesh::GetSkeleton() const
{
    return Skeleton.Get();
}

void USkeletalMesh::SetSkeletonBinding(const FSkeletonBinding& InBinding)
{
    SkeletonBinding = InBinding;
    if (SkeletonBinding.SkeletonPath.empty())
    {
        SkeletonBinding.SkeletonPath = "None";
    }
    SyncSkeletonBindingToAsset();

    if (PhysicsAsset.Get())
    {
        FSkeletonCompatibilityReport Report;
        if (!CanUsePhysicsAsset(PhysicsAsset.Get(), &Report))
        {
            UE_LOG("SkeletalMesh default PhysicsAsset cleared after skeleton binding change. Mesh=%s PhysicsAsset=%s Reason=%s",
                GetName().c_str(),
                PhysicsAsset->GetName().c_str(),
                Report.Reason.c_str());
            ClearPhysicsAsset();
        }
    }
}

void USkeletalMesh::SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset)
{
    if (!InPhysicsAsset)
    {
        ClearPhysicsAsset();
        return;
    }

    FSkeletonCompatibilityReport Report;
    if (!CanUsePhysicsAsset(InPhysicsAsset, &Report))
    {
        UE_LOG("SetPhysicsAsset rejected: skeleton mismatch. Mesh=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            InPhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        ClearPhysicsAsset();
        return;
    }

    PhysicsAsset     = InPhysicsAsset;
    PhysicsAssetPath = InPhysicsAsset->GetAssetPathFileName().empty()
            ? FString("None")
            : InPhysicsAsset->GetAssetPathFileName();
}

UPhysicsAsset* USkeletalMesh::GetPhysicsAsset() const
{
    if (PhysicsAsset.Get())
    {
        FSkeletonCompatibilityReport Report;
        if (CanUsePhysicsAsset(PhysicsAsset.Get(), &Report))
        {
            return PhysicsAsset.Get();
        }

        UE_LOG("GetPhysicsAsset cleared incompatible cached PhysicsAsset. Mesh=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            PhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        const_cast<USkeletalMesh*>(this)->ClearPhysicsAsset();
        return nullptr;
    }

    if (PhysicsAssetPath.empty() || PhysicsAssetPath == "None")
    {
        return nullptr;
    }

    USkeletalMesh* MutableThis = const_cast<USkeletalMesh*>(this);
    return MutableThis->ResolvePhysicsAsset() ? PhysicsAsset.Get() : nullptr;
}

void USkeletalMesh::SetPhysicsAssetPath(const FString& InPath)
{
    PhysicsAssetPath = InPath.empty() ? FString("None") : InPath;
    PhysicsAsset.Reset();

    if (PhysicsAssetPath == "None")
    {
        return;
    }
}

bool USkeletalMesh::ResolvePhysicsAsset()
{
    if (PhysicsAsset.Get())
    {
        FSkeletonCompatibilityReport Report;
        if (CanUsePhysicsAsset(PhysicsAsset.Get(), &Report))
        {
            return true;
        }

        UE_LOG("ResolvePhysicsAsset cleared incompatible cached PhysicsAsset. Mesh=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            PhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        ClearPhysicsAsset();
        return false;
    }

    if (PhysicsAssetPath.empty() || PhysicsAssetPath == "None")
    {
        PhysicsAsset.Reset();
        return false;
    }

    UPhysicsAsset* LoadedPhysicsAsset = FPhysicsAssetManager::Get().LoadPhysicsAsset(PhysicsAssetPath);
    if (!LoadedPhysicsAsset)
    {
        PhysicsAsset.Reset();
        return false;
    }

    FSkeletonCompatibilityReport Report;
    if (!CanUsePhysicsAsset(LoadedPhysicsAsset, &Report))
    {
        UE_LOG("ResolvePhysicsAsset rejected loaded PhysicsAsset: skeleton mismatch. Mesh=%s PhysicsAsset=%s Reason=%s",
            GetName().c_str(),
            LoadedPhysicsAsset->GetName().c_str(),
            Report.Reason.c_str());
        ClearPhysicsAsset();
        return false;
    }

    PhysicsAsset = LoadedPhysicsAsset;
    return true;
}

void USkeletalMesh::ClearPhysicsAsset()
{
    PhysicsAsset.Reset();
    PhysicsAssetPath = "None";
}

uint32 USkeletalMesh::CalculateTrackedMemorySize() const
{
    if (!SkeletalMeshAsset)
    {
        return 0;
    }

    return
            static_cast<uint32>(SkeletalMeshAsset->Vertices.size() * sizeof(FVertexPNCTBW)) +
            static_cast<uint32>(SkeletalMeshAsset->Indices.size() * sizeof(uint32));
}

void USkeletalMesh::ReleaseTrackedMemory()
{
    if (!bMemoryTracked)
    {
        return;
    }

    MemoryStats::SubSkeletalMeshCPUMemory(TrackedMemorySize);
    bMemoryTracked = false;
    TrackedMemorySize = 0;
}

void USkeletalMesh::CacheSectionMaterialIndices()
{
    if (!SkeletalMeshAsset)
    {
        return;
    }

    for (FSkeletalMeshSection& Section : SkeletalMeshAsset->Sections)
    {
        Section.MaterialIndex = -1;
        for (int32 i = 0; i < static_cast<int32>(SkeletalMaterials.size()); ++i)
        {
            if (SkeletalMaterials[i].MaterialSlotName == Section.MaterialSlotName)
            {
                Section.MaterialIndex = i;
                break;
            }
        }
    }
}

void USkeletalMesh::SyncSkeletonBindingToAsset()
{
    if (!SkeletalMeshAsset)
    {
        return;
    }

    SkeletalMeshAsset->SkeletonPath                   = SkeletonBinding.SkeletonPath.empty() ? FString("None") : SkeletonBinding.SkeletonPath;
    SkeletalMeshAsset->SkeletonAssetGuid              = SkeletonBinding.SkeletonAssetGuid;
    SkeletalMeshAsset->SkeletonCompatibilitySignature = SkeletonBinding.CompatibilitySignature;
}

void USkeletalMesh::SyncSkeletonBindingFromAsset()
{
    if (!SkeletalMeshAsset)
    {
        return;
    }

    SkeletonBinding.SkeletonPath           = SkeletalMeshAsset->SkeletonPath.empty() ? FString("None") : SkeletalMeshAsset->SkeletonPath;
    SkeletonBinding.SkeletonAssetGuid      = SkeletalMeshAsset->SkeletonAssetGuid;
    SkeletonBinding.CompatibilitySignature = SkeletalMeshAsset->SkeletonCompatibilitySignature;
}

bool USkeletalMesh::CanUsePhysicsAsset(UPhysicsAsset* InPhysicsAsset, FSkeletonCompatibilityReport* OutReport) const
{
    if (!InPhysicsAsset)
    {
        if (OutReport)
        {
            OutReport->Result = ESkeletonCompatibilityResult::Incompatible;
            OutReport->Reason = "null physics asset";
        }
        return false;
    }

    const FSkeletonCompatibilityReport Report =
            FSkeletonManager::CheckCompatibility(GetSkeletonBinding(), InPhysicsAsset->GetSkeletonBinding());

    if (OutReport)
    {
        *OutReport = Report;
    }

    return Report.Result == ESkeletonCompatibilityResult::ExactSkeleton;
}
