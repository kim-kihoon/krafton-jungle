local BlackPhoto = {}

BlackPhoto.Name = "BlackPhoto"

function BlackPhoto:Spawn(context)
    local target = context.Target
    if target == nil then
        return false, "target is nil"
    end

    context.State.HadPhotoBlackoutTargetTag = target:HasTag(context.Tags.PhotoBlackoutTarget)
    if not context.State.HadPhotoBlackoutTargetTag then
        target:AddTag(context.Tags.PhotoBlackoutTarget)
    end

    return true
end

function BlackPhoto:Despawn(context)
    local target = context.Target
    if target == nil then
        return
    end

    if not context.State.HadPhotoBlackoutTargetTag then
        target:RemoveTag(context.Tags.PhotoBlackoutTarget)
    end
end

function BlackPhoto:IsCleared(context)
    return context.State.bCleared == true
end

return BlackPhoto
