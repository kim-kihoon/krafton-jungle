local PhotoGhostReplacement = {}

PhotoGhostReplacement.Name = "PhotoGhostReplacement"

local GHOST_MESH_PATH = "Content/Data/Ghost/Ghost.uasset"

local function get_actor_location(actor)
    if actor == nil then
        return Vec3(0, 0, 0)
    end
    if actor.GetLocation ~= nil then
        return actor:GetLocation()
    end
    return actor.Location
end

local function get_actor_rotation(actor)
    if actor == nil then
        return Vec3(0, 0, 0)
    end
    if actor.GetRotation ~= nil then
        return actor:GetRotation()
    end
    return actor.Rotation
end

local function get_actor_scale(actor)
    if actor == nil or actor.Scale == nil then
        return Vec3(1, 1, 1)
    end
    return actor.Scale
end

local function destroy_actor(actor)
    if actor == nil then
        return
    end
    if actor.Destroy ~= nil then
        pcall(function()
            actor:Destroy()
        end)
    end
end

function PhotoGhostReplacement:Spawn(context)
    local target = context.Target
    if target == nil then
        return false, "target is nil"
    end
    if World == nil or World.SpawnStaticMeshActor == nil then
        return false, "World.SpawnStaticMeshActor unavailable"
    end

    local ghostActor = World.SpawnStaticMeshActor(
        GHOST_MESH_PATH,
        get_actor_location(target),
        get_actor_rotation(target),
        get_actor_scale(target)
    )
    if ghostActor == nil then
        return false, "Ghost static mesh actor spawn failed: " .. GHOST_MESH_PATH
    end

    if ghostActor.AddTag == nil or ghostActor.SetVisible == nil or ghostActor.Destroy == nil then
        destroy_actor(ghostActor)
        return false, "spawned Ghost actor does not expose Actor bindings"
    end

    context.State.GhostActor = ghostActor
    context.State.HadPhotoGhostReplacementTargetTag = target:HasTag(context.Tags.PhotoGhostReplacementTarget)

    if not context.State.HadPhotoGhostReplacementTargetTag then
        target:AddTag(context.Tags.PhotoGhostReplacementTarget)
    end

    ghostActor:AddTag(context.Tags.PhotoGhostReplacementActor)
    ghostActor:SetVisible(false)
    return true
end

function PhotoGhostReplacement:Despawn(context)
    local target = context.Target
    if target ~= nil and not context.State.HadPhotoGhostReplacementTargetTag then
        target:RemoveTag(context.Tags.PhotoGhostReplacementTarget)
    end

    destroy_actor(context.State.GhostActor)
    context.State.GhostActor = nil
end

function PhotoGhostReplacement:IsCleared(context)
    return context.State.bCleared == true
end

return PhotoGhostReplacement
