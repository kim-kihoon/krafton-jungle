local OffscreenAnimation = {}

OffscreenAnimation.Name = "OffscreenAnimation"

local function get_skeletal_mesh(actor)
    if actor == nil or actor.GetSkeletalMeshComponent == nil then
        return nil
    end
    return actor:GetSkeletalMeshComponent()
end

local function is_target_in_view(actor)
    if World == nil or World.IsActorInViewFrustum == nil then
        return true
    end
    return World.IsActorInViewFrustum(actor)
end

local function is_empty_animation_path(path)
    return path == nil or path == "" or path == "None"
end

local function collect_candidate_animation_paths(mesh, currentPath)
    if mesh == nil or mesh.GetCompatibleAnimationPaths == nil then
        return nil
    end

    local compatiblePaths = mesh:GetCompatibleAnimationPaths()
    if compatiblePaths == nil then
        return nil
    end

    local candidates = {}
    for _, path in ipairs(compatiblePaths) do
        if not is_empty_animation_path(path) and path ~= currentPath then
            candidates[#candidates + 1] = path
        end
    end
    return candidates
end

function OffscreenAnimation:Spawn(context)
    local mesh = get_skeletal_mesh(context.Target)
    if mesh == nil or mesh.PlayAnimationByPath == nil then
        return false, "skeletal mesh animation binding unavailable"
    end

    context.State.Mesh = mesh
    local currentPath = nil
    if mesh.GetAnimationPath ~= nil then
        currentPath = mesh:GetAnimationPath()
        context.State.OriginalAnimationPath = currentPath
    end
    if mesh.GetPlayRate ~= nil then
        context.State.OriginalPlayRate = mesh:GetPlayRate()
    end
    if mesh.GetLooping ~= nil then
        context.State.OriginalLooping = mesh:GetLooping()
    end
    if mesh.IsPlaying ~= nil then
        context.State.OriginalPlaying = mesh:IsPlaying()
    end

    local candidates = collect_candidate_animation_paths(mesh, currentPath)
    if candidates == nil then
        return false, "compatible animation list binding unavailable"
    end
    if #candidates == 0 then
        return false, "compatible non-current animation not found"
    end

    local selectedIndex = 1
    if context.RandomIndex ~= nil then
        selectedIndex = context.RandomIndex(#candidates) or 1
    end

    context.State.OffscreenAnimationPath = candidates[selectedIndex]
    context.State.bPlayingOffscreen = false
    return true
end

function OffscreenAnimation:Tick(context)
    local mesh = context.State.Mesh
    local animationPath = context.State.OffscreenAnimationPath
    if mesh == nil or is_empty_animation_path(animationPath) then
        return
    end

    local shouldPlay = not is_target_in_view(context.Target)
    if shouldPlay == context.State.bPlayingOffscreen then
        return
    end

    context.State.bPlayingOffscreen = shouldPlay
    if shouldPlay then
        mesh:PlayAnimationByPath(animationPath, true)
    else
        mesh:SetPlaying(false)
    end
end

function OffscreenAnimation:Despawn(context)
    local mesh = context.State.Mesh
    if mesh == nil then
        return
    end

    local originalPath = context.State.OriginalAnimationPath
    local bHasOriginalAnimation = originalPath ~= nil and originalPath ~= "" and originalPath ~= "None"
    local bRestoredOriginalAnimation = false
    if bHasOriginalAnimation and mesh.SetAnimationByPath ~= nil then
        bRestoredOriginalAnimation = mesh:SetAnimationByPath(originalPath) == true
    end
    if not bRestoredOriginalAnimation and mesh.StopAnimation ~= nil then
        mesh:StopAnimation()
    end

    if context.State.OriginalPlayRate ~= nil and mesh.SetPlayRate ~= nil then
        mesh:SetPlayRate(context.State.OriginalPlayRate)
    end
    if context.State.OriginalLooping ~= nil and mesh.SetLooping ~= nil then
        mesh:SetLooping(context.State.OriginalLooping)
    end
    if bRestoredOriginalAnimation and context.State.OriginalPlaying ~= nil and mesh.SetPlaying ~= nil then
        mesh:SetPlaying(context.State.OriginalPlaying)
    end
end

function OffscreenAnimation:IsCleared(context)
    return context.State.bCleared == true
end

return OffscreenAnimation
