#include "SkyRenderPass.h"

#include "Core/ResourceManager.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Scene/RenderCommand.h"

bool FSkyRenderPass::Initialize()
{
	return true;
}

bool FSkyRenderPass::Release()
{
	ShaderBinding.reset();
	return true;
}

bool FSkyRenderPass::Begin(const FRenderPassContext* Context)
{
	bSkipSkyDraw = false;

	const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::Sky);
	OutSRV = Context->RenderTargets->SceneColorSRV;
	OutRTV = Context->RenderTargets->SceneColorRTV;

	if (Commands.empty())
	{
		bSkipSkyDraw = true;
		return true;
	}

	UShader* SkyShader = FResourceManager::Get().GetShader("Shaders/Multipass/SkyPass.hlsl");
	if (SkyShader == nullptr)
	{
		bSkipSkyDraw = true;
		return true;
	}

	if (!ShaderBinding || ShaderBinding->GetShader() != SkyShader)
	{
		ShaderBinding = SkyShader->CreateBindingInstance(Context->Device);
	}

	if (!ShaderBinding)
	{
		bSkipSkyDraw = true;
		return true;
	}

	ID3D11BlendState* BlendState = FResourceManager::Get().GetOrCreateBlendState(EBlendType::Opaque);
	Context->DeviceContext->OMSetBlendState(BlendState, nullptr, 0xFFFFFFFF);

	ID3D11RenderTargetView* RTV = Context->RenderTargets->SceneColorRTV;
	Context->DeviceContext->OMSetRenderTargets(1, &RTV, nullptr);
	Context->DeviceContext->IASetInputLayout(nullptr);
	Context->DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	Context->DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	return true;
}

bool FSkyRenderPass::DrawCommand(const FRenderPassContext* Context)
{
	if (bSkipSkyDraw || !ShaderBinding)
	{
		return true;
	}

	const TArray<FRenderCommand>& Commands = Context->RenderBus->GetCommands(ERenderPass::Sky);
	if (Commands.empty())
	{
		return true;
	}

	const FSkyConstants& SkyConstants = Commands.front().Constants.Sky;
	ShaderBinding->ApplyFrameParameters(*Context->RenderBus);
	ShaderBinding->SetMatrix4("InvView", SkyConstants.InvView);
	ShaderBinding->SetMatrix4("InvProjection", SkyConstants.InvProjection);
	ShaderBinding->SetVector4("SkyZenithColor", SkyConstants.SkyZenithColor);
	ShaderBinding->SetVector4("SkyHorizonColor", SkyConstants.SkyHorizonColor);
	ShaderBinding->SetVector4("SunColor", SkyConstants.SunColor);
	ShaderBinding->SetVector4("SunDirectionAndDiskSize", SkyConstants.SunDirectionAndDiskSize);
	ShaderBinding->SetVector4("SkyParams0", SkyConstants.SkyParams0);
	ShaderBinding->SetVector4("SkyParams1", SkyConstants.SkyParams1);
	ShaderBinding->SetVector4("CameraForward", SkyConstants.CameraForward);
	ShaderBinding->SetVector4("CameraRight", SkyConstants.CameraRight);
	ShaderBinding->SetVector4("CameraUp", SkyConstants.CameraUp);
	ShaderBinding->Bind(Context->DeviceContext);

	Context->DeviceContext->Draw(3, 0);
	return true;
}

bool FSkyRenderPass::End(const FRenderPassContext* Context)
{
	(void)Context;
	return true;
}
