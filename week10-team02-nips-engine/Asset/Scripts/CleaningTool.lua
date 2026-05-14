ToolId = ToolId or "mop"

function BeginPlay(owner)
    if owner == nil then
        return
    end

    RegisterCleaningToolActor(owner, ToolId)
end
