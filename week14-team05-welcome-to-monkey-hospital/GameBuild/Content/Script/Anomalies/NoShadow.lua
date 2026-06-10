local NoShadow = {}

NoShadow.Name = "NoShadow"

local function get_target_primitive(actor)
    if actor == nil then
        return nil
    end

    if actor.GetRootPrimitiveComponent ~= nil then
        local rootPrimitive = actor:GetRootPrimitiveComponent()
        if rootPrimitive ~= nil then
            return rootPrimitive
        end
    end

    if actor.GetPrimitiveComponent ~= nil then
        return actor:GetPrimitiveComponent()
    end

    return nil
end

function NoShadow:Spawn(context)
    local primitive = get_target_primitive(context.Target)
    if primitive == nil or primitive.SetCastShadow == nil or primitive.GetCastShadow == nil then
        return false, "primitive shadow binding unavailable"
    end

    context.State.Primitive = primitive
    context.State.OriginalCastShadow = primitive:GetCastShadow()
    primitive:SetCastShadow(false)
    return true
end

function NoShadow:Despawn(context)
    local primitive = context.State.Primitive
    if primitive == nil or primitive.SetCastShadow == nil then
        return
    end

    primitive:SetCastShadow(context.State.OriginalCastShadow == true)
end

function NoShadow:IsCleared(context)
    return context.State.bCleared == true
end

return NoShadow
