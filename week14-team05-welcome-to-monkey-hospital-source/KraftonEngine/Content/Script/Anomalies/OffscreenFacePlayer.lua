local OffscreenFacePlayer = {}

OffscreenFacePlayer.Name = "OffscreenFacePlayer"

local RAD_TO_DEG = 57.29577951308232

local function is_valid_actor(actor)
    if actor == nil then
        return false
    end
    if actor.IsValid == nil then
        return true
    end
    return actor:IsValid()
end

local function get_actor_location(actor)
    if actor == nil then
        return nil
    end
    if actor.GetLocation ~= nil then
        return actor:GetLocation()
    end
    return actor.Location
end

local function get_actor_rotation(actor)
    if actor == nil then
        return nil
    end
    return actor.Rotation
end

local function set_actor_rotation(actor, rotation)
    if actor == nil or rotation == nil then
        return false
    end
    if actor.SetRotation ~= nil then
        actor:SetRotation(rotation)
    else
        actor.Rotation = rotation
    end
    return true
end

local function get_player_pawn()
    if World == nil or World.GetFirstPlayerController == nil then
        return nil
    end

    local controller = World.GetFirstPlayerController()
    if controller == nil or controller.GetPossessedPawn == nil then
        return nil
    end
    return controller:GetPossessedPawn()
end

local function get_player_camera()
    if CameraManager ~= nil and CameraManager.GetActiveCamera ~= nil then
        local activeCamera = CameraManager.GetActiveCamera()
        if activeCamera ~= nil then
            return activeCamera
        end
    end

    local player = get_player_pawn()
    if player ~= nil and player.GetCamera ~= nil then
        return player:GetCamera()
    end
    return nil
end

local function get_camera_height_target_location(actor, cameraLocation)
    local targetLocation = get_actor_location(actor)
    if targetLocation == nil or cameraLocation == nil then
        return nil
    end
    return Vec3(targetLocation.X, targetLocation.Y, cameraLocation.Z)
end

local function atan2(y, x)
    if math.atan2 ~= nil then
        return math.atan2(y, x)
    end
    return math.atan(y, x)
end

local function make_yaw_rotation_to_player(location)
    local playerLocation = get_actor_location(get_player_pawn())
    if location == nil or playerLocation == nil then
        return nil
    end

    local delta = playerLocation - location
    if math.abs(delta.X) < 0.0001 and math.abs(delta.Y) < 0.0001 then
        return nil
    end

    return Vec3(0.0, 0.0, atan2(delta.Y, delta.X) * RAD_TO_DEG)
end

local function get_skeletal_mesh(actor)
    if actor == nil or actor.GetSkeletalMeshComponent == nil then
        return nil
    end
    return actor:GetSkeletalMeshComponent()
end

local function is_target_in_view(context)
    if World == nil then
        return true
    end

    local mesh = context.State.Mesh
    if mesh ~= nil and World.IsComponentInViewFrustum ~= nil then
        return World.IsComponentInViewFrustum(mesh)
    end

    if World.IsActorInViewFrustum ~= nil then
        return World.IsActorInViewFrustum(context.Target)
    end

    return true
end

local function is_trace_clear_to_target(startLocation, targetActor, ignoreActor)
    if World == nil or World.LineTraceObjects == nil then
        return false
    end
    if startLocation == nil or not is_valid_actor(targetActor) then
        return false
    end

    local targetLocation = get_camera_height_target_location(targetActor, startLocation)
    if targetLocation == nil then
        return false
    end

    local hit = World.LineTraceObjects(startLocation, targetLocation, ignoreActor)
    if hit == nil or not hit.Hit then
        return true
    end
    return hit.Actor == targetActor
end

local function is_target_observed_by_player(context)
    if not is_target_in_view(context) then
        return false
    end

    local camera = get_player_camera()
    if camera == nil or camera.GetLocation == nil then
        return false
    end

    return is_trace_clear_to_target(camera:GetLocation(), context.Target, get_player_pawn())
end

function OffscreenFacePlayer:Spawn(context)
    local target = context.Target
    if not is_valid_actor(target) then
        return false, "target is invalid"
    end

    context.State.OriginalRotation = get_actor_rotation(target)
    context.State.Mesh = get_skeletal_mesh(target)
    context.State.bObservedByPlayer = false
    return true
end

function OffscreenFacePlayer:Tick(context)
    if not context.State.bObservedByPlayer then
        if is_target_observed_by_player(context) then
            context.State.bObservedByPlayer = true
        end
        return
    end

    if is_target_in_view(context) then
        return
    end

    local target = context.Target
    return self:FaceTargetToPlayer(context, target)
end

function OffscreenFacePlayer:FaceTargetToPlayer(context, target)
    target = target or context.Target
    local targetLocation = get_actor_location(target)
    local rotation = make_yaw_rotation_to_player(targetLocation)
    if rotation == nil then
        return false
    end

    return set_actor_rotation(target, rotation)
end

function OffscreenFacePlayer:Despawn(context)
    if context.State.OriginalRotation ~= nil then
        set_actor_rotation(context.Target, context.State.OriginalRotation)
    end
end

function OffscreenFacePlayer:IsCleared(context)
    return context.State.bCleared == true
end

return OffscreenFacePlayer
