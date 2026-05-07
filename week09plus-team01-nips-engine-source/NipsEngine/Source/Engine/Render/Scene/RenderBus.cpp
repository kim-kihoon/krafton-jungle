#include "RenderBus.h"
#include "Core/Logger.h"

void FRenderBus::Clear()
{
	for (uint32 i = 0; i < static_cast<uint32>(ERenderPass::MAX); ++i)
	{
		PassQueues[i].clear();
	}

	Lights.clear();
	DirectionalShadow.reset();
	CastShadowSpotLights.clear();
	CastShadowPointLights.clear();
}

void FRenderBus::AddCommand(ERenderPass Pass, const FRenderCommand& InCommand)
{
	PassQueues[static_cast<uint32>(Pass)].push_back(InCommand);
}

void FRenderBus::AddCommand(ERenderPass Pass, FRenderCommand&& InCommand)
{
	PassQueues[static_cast<uint32>(Pass)].push_back(std::move(InCommand));
}

const TArray<FRenderCommand>& FRenderBus::GetCommands(ERenderPass Pass) const
{
	return PassQueues[static_cast<uint32>(Pass)];
}

void FRenderBus::SetRenderSettings(const EViewMode NewViewMode, const FShowFlags NewShowFlags)
{
	SceneView.ViewMode = NewViewMode;
	ShowFlags = NewShowFlags;
}
