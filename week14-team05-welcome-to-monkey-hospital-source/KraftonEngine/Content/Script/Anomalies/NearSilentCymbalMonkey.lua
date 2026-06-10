local NearSilentCymbalMonkey = {}

NearSilentCymbalMonkey.Name = "NearSilentCymbalMonkey"

local MONKEY_TAG = "CymbalsMonkey"
local TARGET_TAG = "NearSilentCymbalMonkeyTarget"
local MUTE_RADIUS = 2.5
local MUTE_VOLUME = 0.0

local function is_valid_object(object)
    if object == nil then
        return false
    end
    if object.IsValid == nil then
        return true
    end
    return object:IsValid()
end

local function get_first_actor_by_tag(tag)
    if World == nil or World.FindActorsByTag == nil then
        return nil
    end

    local actors = World.FindActorsByTag(tag)
    if actors == nil then
        return nil
    end

    for _, actor in pairs(actors) do
        if is_valid_object(actor) then
            return actor
        end
    end

    return nil
end

local function get_camera_location()
    if World == nil or World.GetFirstPlayerController == nil then
        return nil
    end

    local controller = World.GetFirstPlayerController()
    if controller == nil then
        return nil
    end

    if controller.GetPlayerCameraManager ~= nil then
        local cameraManager = controller:GetPlayerCameraManager()
        if cameraManager ~= nil and cameraManager.GetActiveCamera ~= nil then
            local camera = cameraManager:GetActiveCamera()
            if camera ~= nil and camera.GetLocation ~= nil then
                return camera:GetLocation()
            end
        end
    end

    if controller.GetPossessedPawn ~= nil then
        local pawn = controller:GetPossessedPawn()
        if pawn ~= nil and pawn.GetLocation ~= nil then
            return pawn:GetLocation()
        end
    end

    return nil
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

local function distance_between(a, b)
    if a == nil or b == nil then
        return nil
    end

    local dx = (a.X or 0) - (b.X or 0)
    local dy = (a.Y or 0) - (b.Y or 0)
    local dz = (a.Z or 0) - (b.Z or 0)
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function set_muted(context, bMuted)
    if context.State.bMuted == bMuted then
        return
    end

    local audio = context.State.AudioComponent
    if not is_valid_object(audio) or audio.SetVolume == nil then
        return
    end

    if bMuted then
        audio:SetVolume(MUTE_VOLUME)
    else
        audio:SetVolume(context.State.OriginalVolume or 1.0)
    end

    context.State.bMuted = bMuted
end

function NearSilentCymbalMonkey:Spawn(context)
    local target = context.Target
    if not is_valid_object(target) then
        return false, "target is nil"
    end

    local monkey = get_first_actor_by_tag(MONKEY_TAG)
    if monkey == nil or monkey.GetAudioComponent == nil then
        return false, "CymbalsMonkey audio actor not found"
    end

    local audio = monkey:GetAudioComponent()
    if not is_valid_object(audio) or audio.SetVolume == nil or audio.GetVolume == nil then
        return false, "CymbalsMonkey AudioComponent binding unavailable"
    end

    context.State.MonkeyActor = monkey
    context.State.AudioComponent = audio
    context.State.OriginalVolume = audio:GetVolume()
    context.State.bMuted = false
    context.State.HadTargetTag = target:HasTag(TARGET_TAG)

    if not context.State.HadTargetTag then
        target:AddTag(TARGET_TAG)
    end

    return true
end

function NearSilentCymbalMonkey:Tick(context)
    local cameraLocation = get_camera_location()
    local targetLocation = get_actor_location(context.Target)
    local distance = distance_between(cameraLocation, targetLocation)
    if distance == nil then
        set_muted(context, false)
        return
    end

    set_muted(context, distance <= MUTE_RADIUS)
end

function NearSilentCymbalMonkey:Despawn(context)
    set_muted(context, false)

    local target = context.Target
    if target ~= nil and target.RemoveTag ~= nil and not context.State.HadTargetTag then
        target:RemoveTag(TARGET_TAG)
    end
end

function NearSilentCymbalMonkey:IsCleared(context)
    return context.State.bCleared == true
end

return NearSilentCymbalMonkey
