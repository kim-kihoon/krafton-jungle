local PhotoBoneTwist = {}

PhotoBoneTwist.Name = "PhotoBoneTwist"

function PhotoBoneTwist:Spawn(context)
    local target = context.Target
    if target == nil then
        return false, "target is nil"
    end
    if target.GetSkeletalMeshComponent == nil then
        return false, "Actor:GetSkeletalMeshComponent unavailable"
    end

    local mesh = target:GetSkeletalMeshComponent()
    if mesh == nil then
        return false, "target skeletal mesh component not found"
    end

    context.State.HadPhotoBoneTwistTargetTag = target:HasTag(context.Tags.PhotoBoneTwistTarget)
    if not context.State.HadPhotoBoneTwistTargetTag then
        target:AddTag(context.Tags.PhotoBoneTwistTarget)
    end

    return true
end

function PhotoBoneTwist:Despawn(context)
    local target = context.Target
    if target ~= nil and not context.State.HadPhotoBoneTwistTargetTag then
        target:RemoveTag(context.Tags.PhotoBoneTwistTarget)
    end
end

function PhotoBoneTwist:IsCleared(context)
    return context.State.bCleared == true
end

return PhotoBoneTwist
