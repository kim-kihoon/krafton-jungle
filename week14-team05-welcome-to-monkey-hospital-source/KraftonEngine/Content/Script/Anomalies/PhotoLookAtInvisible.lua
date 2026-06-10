local OffscreenFacePlayer = require("Anomalies/OffscreenFacePlayer")

local PhotoLookAtInvisible = {}

PhotoLookAtInvisible.Name = "PhotoLookAtInvisible"

function PhotoLookAtInvisible:Spawn(context)
    local ok, message = OffscreenFacePlayer.Spawn(OffscreenFacePlayer, context)
    if not ok then
        return false, message
    end

    local target = context.Target
    if target == nil then
        OffscreenFacePlayer.Despawn(OffscreenFacePlayer, context)
        return false, "target is nil"
    end

    context.State.HadPhotoInvisibleTag = target:HasTag(context.Tags.PhotoInvisible)
    if not context.State.HadPhotoInvisibleTag then
        target:AddTag(context.Tags.PhotoInvisible)
    end

    return true
end

function PhotoLookAtInvisible:OnPhotoCapture(context)
    return OffscreenFacePlayer.FaceTargetToPlayer(OffscreenFacePlayer, context)
end

function PhotoLookAtInvisible:Despawn(context)
    local target = context.Target
    if target ~= nil and not context.State.HadPhotoInvisibleTag then
        target:RemoveTag(context.Tags.PhotoInvisible)
    end

    OffscreenFacePlayer.Despawn(OffscreenFacePlayer, context)
end

function PhotoLookAtInvisible:IsCleared(context)
    return context.State.bCleared == true
end

return PhotoLookAtInvisible
