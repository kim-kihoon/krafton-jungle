local PhotoInvisible = {}

PhotoInvisible.Name = "PhotoInvisible"

function PhotoInvisible:Spawn(context)
    local target = context.Target
    if target == nil then
        return false, "target is nil"
    end

    context.State.HadPhotoInvisibleTag = target:HasTag(context.Tags.PhotoInvisible)
    if not context.State.HadPhotoInvisibleTag then
        target:AddTag(context.Tags.PhotoInvisible)
    end

    return true
end

function PhotoInvisible:Despawn(context)
    local target = context.Target
    if target == nil then
        return
    end

    if not context.State.HadPhotoInvisibleTag then
        target:RemoveTag(context.Tags.PhotoInvisible)
    end
end

function PhotoInvisible:IsCleared(context)
    return context.State.bCleared == true
end

return PhotoInvisible
