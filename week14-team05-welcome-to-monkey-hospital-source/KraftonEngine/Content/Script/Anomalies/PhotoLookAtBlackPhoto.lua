local OffscreenFacePlayer = require("Anomalies/OffscreenFacePlayer")

local PhotoLookAtBlackPhoto = {}

PhotoLookAtBlackPhoto.Name = "PhotoLookAtBlackPhoto"

function PhotoLookAtBlackPhoto:Spawn(context)
    local ok, message = OffscreenFacePlayer.Spawn(OffscreenFacePlayer, context)
    if not ok then
        return false, message
    end

    local target = context.Target
    if target == nil then
        OffscreenFacePlayer.Despawn(OffscreenFacePlayer, context)
        return false, "target is nil"
    end

    context.State.HadPhotoBlackoutTargetTag = target:HasTag(context.Tags.PhotoBlackoutTarget)
    if not context.State.HadPhotoBlackoutTargetTag then
        target:AddTag(context.Tags.PhotoBlackoutTarget)
    end

    return true
end

function PhotoLookAtBlackPhoto:OnPhotoCapture(context)
    return OffscreenFacePlayer.FaceTargetToPlayer(OffscreenFacePlayer, context)
end

function PhotoLookAtBlackPhoto:Despawn(context)
    local target = context.Target
    if target ~= nil and not context.State.HadPhotoBlackoutTargetTag then
        target:RemoveTag(context.Tags.PhotoBlackoutTarget)
    end

    OffscreenFacePlayer.Despawn(OffscreenFacePlayer, context)
end

function PhotoLookAtBlackPhoto:IsCleared(context)
    return context.State.bCleared == true
end

return PhotoLookAtBlackPhoto
