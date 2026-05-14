#include "PostProcessRenderPass.h"
#include "Core/ResourceManager.h"
#include "Render/Scene/RenderBus.h"

bool FPostProcessRenderPass::Initialize()
{
	return true;
}

bool FPostProcessRenderPass::Release()
{
	ShaderBinding.reset();
	return true;
}

bool FPostProcessRenderPass::Begin(const FRenderPassContext* Context)
{
	OutSRV = Context->RenderTargets->ScenePostProcessSRV;
	OutRTV = Context->RenderTargets->ScenePostProcessRTV;
	
	const FRenderTargetSet* RenderTargets = Context->RenderTargets;
	ID3D11RenderTargetView* RTVs[1] = { RenderTargets->ScenePostProcessRTV };
	Context->DeviceContext->OMSetRenderTargets(ARRAYSIZE(RTVs), RTVs, nullptr);

	UShader* PostProcessShader = FResourceManager::Get().GetShader("Shaders/Multipass/PostProcessPass.hlsl");
	if (!PostProcessShader)
	{
		return false;
	}

	if (!ShaderBinding || ShaderBinding->GetShader() != PostProcessShader)
	{
		ShaderBinding = PostProcessShader->CreateBindingInstance(Context->Device);
	}

	if (!ShaderBinding)
	{
		return false;
	}

	ShaderBinding->ApplyFrameParameters(*Context->RenderBus);
	ShaderBinding->SetSRV("FinalSceneColor", PrevPassSRV);

	float Width = Context->RenderTargets->Width;
	float Height = Context->RenderTargets->Height;
	ShaderBinding->SetVector2("InvResolution", FVector2((Width > 0.0f) ? (1.0f / Width) : 0.0f, (Height > 0.0f) ? (1.0f / Height) : 0.0f));
	const FPostProcessSettings& PostProcess = Context->RenderBus->GetPostProcessSettings();
	ShaderBinding->SetFloat("Gamma", PostProcess.Gamma);
	ShaderBinding->SetFloat("VignetteIntensity", PostProcess.VignetteIntensity);
	ShaderBinding->SetFloat("VignetteRadius", PostProcess.VignetteRadius);
	ShaderBinding->SetFloat("VignetteSoftness", PostProcess.VignetteSoftness);
	ShaderBinding->SetAllSamplers(FResourceManager::Get().GetOrCreateSamplerState(ESamplerType::EST_Linear));

	Context->DeviceContext->IASetInputLayout(nullptr);
	Context->DeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	Context->DeviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	return true;
}

bool FPostProcessRenderPass::DrawCommand(const FRenderPassContext* Context)
{
	if (!ShaderBinding)
	{
		return false;
	}

	/*  이 패스는 visible pass들이 끝난 뒤 실행되는 범용 scene post process 슬롯입니다.
		Gamma/Vignette 같은 카메라 색 보정은 Grid/SubUV/Billboard까지 포함한 최종 scene color에 적용하고,
		fade/letterbox 같은 최종 화면 overlay는 FPostProcessOutlineRenderPass에서 처리합니다.
		이후 bloom/tonemap/color grading도 이 위치에 추가할 수 있습니다. */

	ShaderBinding->ApplyFrameParameters(*Context->RenderBus);
	ShaderBinding->Bind(Context->DeviceContext);
	Context->DeviceContext->Draw(3, 0);
	return true;
}

bool FPostProcessRenderPass::End(const FRenderPassContext* Context)
{
	ID3D11ShaderResourceView* NullSRVs[] = { nullptr };
	Context->DeviceContext->PSSetShaderResources(0, 1, NullSRVs);
	return true;
}
