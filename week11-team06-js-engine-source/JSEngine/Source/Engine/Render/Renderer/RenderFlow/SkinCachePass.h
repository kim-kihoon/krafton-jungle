#pragma once

#include "Core/Logging/Stats.h"
#include "Core/Logging/GPUProfiler.h"
#include "Core/ResourceManager.h"
#include "Component/SkeletalMeshComponent.h"
#include "Render/Common/ShaderBindingSlots.h"
#include "Render/Renderer/RenderFlow/RenderPass.h"
#include "Render/Resource/ComputeShader.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Skinning/SkinCacheManager.h"

class FSkinCachePass : public FBaseRenderPass
{
public:
	bool Initialize() override { return true; }
	bool Release() override { return true; }

protected:
	bool Begin(const FRenderPassContext* Context) override
	{
		OutSRV = PrevPassSRV;
		OutRTV = PrevPassRTV;
		return Context != nullptr;
	}

	bool DrawCommand(const FRenderPassContext* Context) override
	{
		SCOPE_STAT("CPU.SkinCache.DispatchSetup");
		if (!Context || !Context->SkinCacheManager || Context->SkinCacheManager->GetEntryCount() == 0)
		{
			return true;
		}

		FComputeShader* SkinCacheCS = FResourceManager::Get().GetComputeShader("SkinCacheCS");
		if (!SkinCacheCS || !Context->DeviceContext || !Context->RenderBus)
		{
			return false;
		}

		const TArray<FRenderCommand>& OpaqueCommands = Context->RenderBus->GetCommands(ERenderPass::Opaque);
		if (OpaqueCommands.empty())
		{
			return true;
		}

		TArray<const FRenderCommand*> DispatchCommands;
		DispatchCommands.reserve(OpaqueCommands.size());
		for (const FRenderCommand& Cmd : OpaqueCommands)
		{
			if (Cmd.VertexFactoryType != EVertexFactoryType::SkinCacheSkeletalMesh)
			{
				continue;
			}

			USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Cmd.SourcePrimitive);
			if (!SkeletalMeshComp)
			{
				continue;
			}

			FSkinCacheEntry* Entry = Context->SkinCacheManager->FindEntry(SkeletalMeshComp->GetUUID());
			if (!Entry || !Entry->OutputBuffer.IsValid() || !Entry->SourceVertexSRV || !Cmd.SkeletalGpuSkinning.BoneMatrixSRV)
			{
				continue;
			}

			const uint64 Revision = SkeletalMeshComp->GetDeformationRevision();
			if (Entry->CachedRevision == Revision)
			{
				continue;
			}

			DispatchCommands.push_back(&Cmd);
		}

		if (DispatchCommands.empty())
		{
			return true;
		}

		GPU_SCOPE_STAT("GPU.SkinCache.Compute");
		SkinCacheCS->Bind(Context->DeviceContext);
		for (const FRenderCommand* DispatchCommand : DispatchCommands)
		{
			const FRenderCommand& Cmd = *DispatchCommand;
			USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Cmd.SourcePrimitive);
			FSkinCacheEntry* Entry = Context->SkinCacheManager->FindEntry(SkeletalMeshComp->GetUUID());
			const uint64 Revision = SkeletalMeshComp->GetDeformationRevision();

			ID3D11ShaderResourceView* SourceSRV = Entry->SourceVertexSRV;
			ID3D11ShaderResourceView* BoneSRV = Cmd.SkeletalGpuSkinning.BoneMatrixSRV;
			ID3D11UnorderedAccessView* OutputUAV = Entry->OutputBuffer.GetUAV();
			Context->DeviceContext->CSSetShaderResources(0, 1, &SourceSRV);
			Context->DeviceContext->CSSetShaderResources(ShaderBindingSlots::BoneMatricesSRV, 1, &BoneSRV);
			Context->DeviceContext->CSSetUnorderedAccessViews(0, 1, &OutputUAV, nullptr);

			const uint32 GroupCountX = (Entry->VertexCount + 63u) / 64u;
			SkinCacheCS->Dispatch(Context->DeviceContext, GroupCountX, 1, 1);
			Context->SkinCacheManager->MarkEntryDispatched(SkeletalMeshComp->GetUUID(), Revision);
		}
		SkinCacheCS->Unbind(Context->DeviceContext);

		return true;
	}

	bool End(const FRenderPassContext* Context) override
	{
		if (!Context || !Context->DeviceContext)
		{
			return true;
		}

		ID3D11UnorderedAccessView* NullUAV = nullptr;
		Context->DeviceContext->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
		ID3D11ShaderResourceView* NullSourceSRV = nullptr;
		ID3D11ShaderResourceView* NullBoneSRV = nullptr;
		Context->DeviceContext->CSSetShaderResources(0, 1, &NullSourceSRV);
		Context->DeviceContext->CSSetShaderResources(ShaderBindingSlots::BoneMatricesSRV, 1, &NullBoneSRV);
		return true;
	}
};
