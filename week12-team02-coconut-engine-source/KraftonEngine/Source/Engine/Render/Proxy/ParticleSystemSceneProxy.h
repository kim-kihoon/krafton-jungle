#pragma once
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"
#include "Render/Particle/ParticleDynamicData.h"

class UParticleSystemComponent;

class FParticleSystemSceneProxy : public FPrimitiveSceneProxy
{
public:
	FParticleSystemSceneProxy(UParticleSystemComponent* InComponent);
	~FParticleSystemSceneProxy() override;

	void SetVisibility(bool Visibility) { bIsOwnerVisible = Visibility; }

	// Called by Component each frame after CPU sim. Proxy takes ownership.
	void UpdateDynamicData(TArray<FDynamicEmitterDataBase*>&& NewData);

	// Required overrides:
	void UpdateTransform()   override;  // Local-space proxy: just CachedWorldPos + identity model
	void UpdateMaterial()    override;  // Pull material from each emitter Source
	void UpdateVisibility()  override;
	void UpdateMesh()        override;  // Reset SectionDraws; sized by emitter count
	void UpdatePerViewport(const FFrameContext& Frame) override;

	bool PrepareDrawBuffer(ID3D11Device*, ID3D11DeviceContext*, FDrawCommandBuffer&) const override;
	bool PrepareDrawCommandBindings(ID3D11Device*, ID3D11DeviceContext*,
		const FPrimitiveDrawOptions&, FDrawCommand&, int32 SectionIndex) const override;

	void SetEmitterSortingPriority(uint16 EmitterDrawIndex, uint16 InPriority);

	//const char* GetVertexShaderEntryName() const override { return "VS_ParticleSprite"; }

private:
	// Per-emitter draw range inside the shared dynamic VB/IB.
	// EmitterDraws[i] is stable storage; SectionToEmitterDrawIndex maps section order back to it.
	struct FEmitterDraw
	{
		int32  EmitterIndex                 = -1;
		EDynamicEmitterType Type           = DET_Unknown;
		UMaterial* Material                = nullptr;
		uint32 FirstIndex                  = 0;
		uint32 IndexCount                  = 0;

		// mesh path
		mutable FDynamicVertexBuffer InstanceVB;
		FMeshBuffer*		 MeshGeom		= nullptr;   // borrowed from UStaticMesh
		uint32               InstanceCount  = 0;
		TArray<FMeshParticleInstanceVertex> PackedInstances;
		mutable bool bInstanceVBDirty = true;

		struct FParticleBlendState
		{
			ERenderPass RenderPass = ERenderPass::AlphaBlend;
			EBlendState BlendState = EBlendState::AlphaBlend;
		};
		FParticleBlendState ParticleBlendState;

		// CBuffer, owned by the emitter
		mutable FConstantBuffer ParticleParamCB;
		mutable FConstantBuffer ParticleMaterialCB;
		mutable bool bParticleParamCBDirty = true;
		mutable bool bParticleMaterialCBDirty = true;
		FParticleParamConstants ParticleParams;
		mutable FParticleMaterialConstants ParticleMaterialParams;

	public:
		uint16 GetSortingPriority() const { return SortingPriority; }
		void SetParticleBlendRoute(EBlendState Mode);

	private:
		friend class FParticleSystemSceneProxy;
		uint16 SortingPriority = 0;
	};

	struct FSpriteParticlePacker
	{
		void ResetFrame() { PackedVertices.clear(); }
		void PackEmitter(const FFrameContext& Frame, FDynamicSpriteEmitterData& Emitter, const FEmitterDraw& Draw, uint32& IndexCursor);
		bool HasPackedSprites() const { return !PackedVertices.empty() && !IndexPattern.empty(); }
		void MarkGpuBuffersDirty() const { bGpuBuffersDirty = true; }
		bool PrepareDrawBuffer(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, FDrawCommandBuffer& Out) const;

	private:
		void EnsureIndexPattern(uint32 RequiredParticleCount);

		TArray<FParticleSpriteVertex> PackedVertices;
		TArray<uint32>                IndexPattern;
		uint32                        IndexPatternParticleCapacity = 0;

		mutable FDynamicVertexBuffer VertexBuffer;
		mutable FDynamicIndexBuffer  IndexBuffer;
		mutable bool bGpuBuffersDirty = true;
		mutable bool bIndexBufferDirty = true;
	};

	struct FMeshParticlePacker
	{
		void ResetFrame(TArray<FEmitterDraw>& EmitterDraws);
		void PackEmitter(const FFrameContext& Frame, FDynamicMeshEmitterData& Emitter, FEmitterDraw& Draw);
		bool HasPackedInstances(const TArray<FEmitterDraw>& EmitterDraws) const;
	};

	// Beam: CPU expands the camera-facing quad strip into dynamic geometry.
	struct FBeamParticlePacker
	{
		static constexpr uint32 MaxSegmentsPerBeam = 256;
		static constexpr uint32 MaxSheetsPerBeam   = 16;

		void ResetFrame();
		void PackEmitter(const FFrameContext& Frame, FDynamicBeamEmitterData& Emitter, FEmitterDraw& Draw);
		bool HasPackedBeams() const { return !PackedVertices.empty() && !PackedIndices.empty(); }
		bool PrepareDrawBuffer(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, FDrawCommandBuffer& Out) const;
		ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer.GetBuffer(); }
		ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer.GetBuffer(); }

	private:
		TArray<FBeamParticleInstanceVertex> PackedVertices;
		TArray<uint32>                      PackedIndices;
		mutable FDynamicVertexBuffer        VertexBuffer;
		mutable FDynamicIndexBuffer         IndexBuffer;
		mutable bool                        bGpuBuffersDirty = true;
	};

	// Ribbon: active particles are grouped into trail strips and expanded into dynamic geometry.
	struct FRibbonParticlePacker
	{
		static constexpr uint32 MaxSheetsPerTrail = 16;

		void ResetFrame();
		void PackEmitter(const FFrameContext& Frame, FDynamicRibbonEmitterData& Emitter, FEmitterDraw& Draw);
		bool HasPackedRibbons() const { return !PackedVertices.empty() && !PackedIndices.empty(); }
		bool PrepareDrawBuffer(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, FDrawCommandBuffer& Out) const;
		ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer.GetBuffer(); }
		ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer.GetBuffer(); }

	private:
		TArray<FRibbonParticleInstanceVertex> PackedVertices;
		TArray<uint32>                        PackedIndices;
		mutable FDynamicVertexBuffer          VertexBuffer;
		mutable FDynamicIndexBuffer           IndexBuffer;
		mutable bool                          bGpuBuffersDirty = true;
	};

	// Sorts EmitterData according to its Sorting Priority, which should be a user-defined numeric value
	// Does NOT reorder the physical array of EmitterDraws. Fills SectionToEmitterDrawIndex instead.
	void SortEmitters();
	void RebuildSectionDraws();

	// Delegates type-specific CPU packing and refreshes per-emitter
	// (FirstIndex, IndexCount) for DrawCommandBuilder.
	void PackParticles(const FFrameContext& Frame);

	void UpdateCB(FEmitterDraw& EmitterDraw, const FDynamicEmitterReplayDataBase& Source);

private:
	TArray<FDynamicEmitterDataBase*> DynamicData;   // owned, freed on next UpdateDynamicData
	TArray<FEmitterDraw>             EmitterDraws;
	TArray<uint16>					 SectionToEmitterDrawIndex;

	FMatrix ComponentToWorld = FMatrix::Identity;
	FSpriteParticlePacker SpritePacker;
	FMeshParticlePacker MeshPacker;
	FBeamParticlePacker BeamPacker;
	FRibbonParticlePacker RibbonPacker;

	bool bIsOwnerVisible	  = true;
	bool bInstancePacked	  = false;

	// Set to true when EmitterDraws.size() changes, or is signaled that one of its elements changed its sort priority.
	// True by default as to ensure Emitter sorting upon initialization
	bool bIsEmitterOrderDirty = true;

};
