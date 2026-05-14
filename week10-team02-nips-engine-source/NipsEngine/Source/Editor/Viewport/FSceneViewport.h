#pragma once
#include "Viewport/Viewport.h"
#include "Viewport/ViewportClient.h"
#include "Slate/ISlateViewport.h"
#include "Editor/Utility/EditorUIUtils.h"
#include "Render/Device/D3DDevice.h"  // FRenderTargetSet 때문에 포함했는데 따로 분리 필요할듯

class FViewportClient;

/*
* 실제 viewport 입력/출력 창구
* viewport local rect 를 알고 있음
* ViewportClient <- > Viewport 상호 참조 가능(소유권은 상위 관리자가 보유)
*/

class FSceneViewport : public FViewport, public ISlateViewport
{
public:
	void SetClient(FViewportClient* InClient) { Client = InClient; }
	FViewportClient* GetClient() { return Client; }
	const FViewportClient* GetClient() const { return Client; }

	/*
	* ISlateViewport Interface
	*/
	void Draw() override;

	bool ContainsPoint(int32 X, int32 Y) const override;
	void WindowToLocal(int32 X, int32 Y, int32& OutX, int32& OutY) const override;

	void SetRect(const FViewportRect& InRect) override
	{
		Rect = InRect;
	}
	const FViewportRect& GetRect() const override
	{
		return Rect;
	}

	FEditorViewportState& GetState() { return State; }
	const FEditorViewportState& GetState() const { return State; }
	void SetState(const FEditorViewportState& InState) { State = InState; }

	FRenderTargetSet* GetViewportRenderTargets() const;

	// 최종 출력 (임시용)
	ID3D11ShaderResourceView* GetOutSRV() const 
	{ 
		if (!RenderTargetSet)
			return nullptr;

		return RenderTargetSet->FinalSRV;
	}

	void SetRenderTargetSet(FRenderTargetSet* InRenderTargetSet) { RenderTargetSet = InRenderTargetSet; }
	FRenderTargetSet* GetRenderTargetSet() const { return RenderTargetSet; }

private:
	FViewportClient* Client = nullptr;
	FEditorViewportState State;

	// Renderer 의 자원을 참조
	FRenderTargetSet* RenderTargetSet = nullptr;

	uint32 ViewportRenderTargetWidth = 0;
	uint32 ViewportRenderTargetHeight = 0;
};

