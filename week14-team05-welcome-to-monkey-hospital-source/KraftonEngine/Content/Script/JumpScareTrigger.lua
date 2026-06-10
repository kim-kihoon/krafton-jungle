local TAG_ACTIVE = "JumpScareActive"
local TAG_TRIGGERED = "JumpScareTriggered"

-- 에디터 프로퍼티가 비어 있을 때만 사용하는 예비 애니메이션 경로다.
local ANIMATION_PATH = ""
local bShowMeshOnTrigger = true
local bPlayAnimationOnTrigger = true
local bMoveMeshOnTrigger = true
local bOneShot = true

local MOVE_OFFSET = Vec3(0.0, 0.0, 0.0)
local MOVE_DURATION = 0.5

local OriginalMeshLocation = nil
local TriggerBoxComponent = nil
local MoveState = nil
local bPollingOverlapTriggered = false
local DebugOnce = {}
local LoadedJumpScareSoundPath = nil
local LoadedJumpScareSoundKey = nil

local function debug_log(message)
    print("[JumpScareTrigger] " .. tostring(message))
end

local function debug_log_once(key, message)
    if DebugOnce[key] == true then
        return
    end

    DebugOnce[key] = true
    debug_log(message)
end

local function format_vec3(value)
    if value == nil then
        return "nil"
    end

    return "(" .. tostring(value.X) .. ", " .. tostring(value.Y) .. ", " .. tostring(value.Z) .. ")"
end

local function copy_vec3(value)
    if value == nil then
        return nil
    end

    return Vec3(value.X or 0.0, value.Y or 0.0, value.Z or 0.0)
end

local function lerp_vec3(startLocation, targetLocation, alpha)
    return Vec3(
        startLocation.X + (targetLocation.X - startLocation.X) * alpha,
        startLocation.Y + (targetLocation.Y - startLocation.Y) * alpha,
        startLocation.Z + (targetLocation.Z - startLocation.Z) * alpha
    )
end

local function is_valid_actor(actor)
    if actor == nil then
        return false
    end
    if actor.IsValid == nil then
        return true
    end

    local ok, valid = pcall(function()
        return actor:IsValid()
    end)
    return ok and valid == true
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

local function is_player_actor(actor)
    local player = get_player_pawn()
    if player ~= nil and actor == player then
        return true
    end

    if actor ~= nil and actor.HasTag ~= nil then
        local ok, hasPlayerTag = pcall(function()
            return actor:HasTag("Player")
        end)
        return ok and hasPlayerTag == true
    end

    return false
end

local function get_target_actor()
    -- 기본은 이 스크립트가 붙은 액터 자신이다.
    return obj
end

local function get_target_mesh()
    local target = get_target_actor()
    if not is_valid_actor(target) or target.GetSkeletalMeshComponent == nil then
        return nil
    end

    local ok, mesh = pcall(function()
        return target:GetSkeletalMeshComponent()
    end)
    if not ok then
        return nil
    end
    return mesh
end

local function cast_object(object, className)
    if object == nil or Reflection == nil or Reflection.Cast == nil then
        return nil
    end

    local ok, casted = pcall(function()
        return Reflection.Cast(object, className)
    end)
    if not ok then
        return nil
    end
    return casted
end

local function find_trigger_box_component()
    if TriggerBoxComponent ~= nil then
        return TriggerBoxComponent
    end
    if obj == nil then
        return nil
    end

    if obj.GetBoxComponent ~= nil then
        local ok, box = pcall(function()
            return obj:GetBoxComponent()
        end)
        if ok and box ~= nil then
            TriggerBoxComponent = box
            debug_log_once("box_cached_actor_api", "trigger box cached from actor api")
            return TriggerBoxComponent
        end
    end

    if obj.GetRootComponent ~= nil then
        local root = obj:GetRootComponent()
        local rootBox = cast_object(root, "UBoxComponent")
        if rootBox ~= nil and rootBox.GetScaledBoxExtent ~= nil then
            TriggerBoxComponent = rootBox
            debug_log_once("box_cached_root", "trigger box cached from root")
            return TriggerBoxComponent
        end
    end

    if obj.GetComponents == nil then
        return nil
    end

    local ok, components = pcall(function()
        return obj:GetComponents()
    end)
    if not ok or components == nil then
        return nil
    end

    for _, component in pairs(components) do
        local box = cast_object(component, "UBoxComponent")
        if box ~= nil and box.GetScaledBoxExtent ~= nil then
            TriggerBoxComponent = box
            debug_log_once("box_cached_component", "trigger box cached from components")
            return TriggerBoxComponent
        end
    end

    return nil
end

local function set_mesh_visible(mesh, visible)
    if mesh ~= nil and mesh.SetVisibility ~= nil then
        pcall(function()
            mesh:SetVisibility(visible)
        end)
    end
end

local function get_component_location(component)
    if component == nil or component.GetLocation == nil then
        return nil
    end

    local ok, location = pcall(function()
        return component:GetLocation()
    end)
    if not ok then
        return nil
    end
    return location
end

local function get_component_relative_location(component)
    if component == nil then
        return nil
    end

    local ok, location = pcall(function()
        return component.RelativeLocation
    end)
    if not ok then
        return nil
    end
    return copy_vec3(location)
end

local function set_component_location(component, location)
    if component == nil or location == nil or component.SetLocation == nil then
        return false
    end

    local ok = pcall(function()
        component:SetLocation(location)
    end)
    return ok == true
end

local function set_component_relative_location(component, location)
    if component == nil or location == nil then
        return false
    end

    local ok = pcall(function()
        component.RelativeLocation = location
    end)
    return ok == true
end

local function get_actor_location(actor)
    if actor == nil or actor.GetLocation == nil then
        return nil
    end

    local ok, location = pcall(function()
        return actor:GetLocation()
    end)
    if not ok then
        return nil
    end
    return copy_vec3(location)
end

local function read_script_property(name)
    if this == nil or this.GetProperty == nil then
        return nil
    end

    local ok, value = pcall(function()
        return this:GetProperty(name)
    end)
    if not ok then
        return nil
    end
    return value
end

local function normalize_string(value)
    if type(value) ~= "string" then
        return nil
    end

    local normalized = value:match("^%s*(.-)%s*$")
    if normalized == nil or normalized == "" or normalized == "None" then
        return nil
    end
    return normalized
end

local function read_number_property(name, defaultValue)
    local value = tonumber(read_script_property(name))
    if value == nil then
        return defaultValue
    end
    return value
end

local function get_jump_scare_sound_key(soundPath)
    return "JumpScare:" .. soundPath .. ":3D"
end

local function ensure_jump_scare_sound_loaded(soundPath)
    if LoadedJumpScareSoundPath == soundPath and LoadedJumpScareSoundKey ~= nil then
        return LoadedJumpScareSoundKey
    end
    if Audio == nil or Audio.Load == nil then
        return nil
    end

    local soundKey = get_jump_scare_sound_key(soundPath)
    local ok, result = pcall(function()
        return Audio.Load(soundKey, soundPath, false, true)
    end)
    if not ok or result == false then
        return nil
    end

    LoadedJumpScareSoundPath = soundPath
    LoadedJumpScareSoundKey = soundKey
    return LoadedJumpScareSoundKey
end

local function play_jump_scare_sound(mesh)
    local soundPath = normalize_string(read_script_property("JumpScareSoundPath"))
    if soundPath == nil then
        return false
    end

    local soundKey = ensure_jump_scare_sound_loaded(soundPath)
    if soundKey == nil or Audio == nil then
        return false
    end

    local volume = read_number_property("JumpScareSoundVolume", 1.0)
    local pitch = read_number_property("JumpScareSoundPitch", 1.0)
    local minDistance = read_number_property("JumpScareSoundMinDistance", 1.0)
    local maxDistance = read_number_property("JumpScareSoundMaxDistance", 12.0)
    local soundLocation = get_component_location(mesh) or get_actor_location(get_target_actor())

    if soundLocation ~= nil and Audio.PlayAt ~= nil then
        local ok = pcall(function()
            Audio.PlayAt(soundKey, volume, soundLocation, minDistance, maxDistance, pitch)
        end)
        return ok == true
    end

    if Audio.Play ~= nil then
        local ok = pcall(function()
            Audio.Play(soundKey, volume)
        end)
        return ok == true
    end
    return false
end

local function play_mesh_animation(mesh)
    if not bPlayAnimationOnTrigger then
        debug_log("animation skipped: disabled")
        return false
    end
    if mesh == nil or mesh.PlayAnimationByPath == nil then
        debug_log("animation skipped: invalid mesh or PlayAnimationByPath unavailable")
        return false
    end

    local animationPath = ANIMATION_PATH
    local propertyPath = read_script_property("LoopAnimationPath")
    if type(propertyPath) == "string" and propertyPath ~= "" and propertyPath ~= "None" then
        animationPath = propertyPath
    end

    if type(animationPath) ~= "string" or animationPath == "" or animationPath == "None" then
        debug_log("animation skipped: empty animation path")
        return false
    end

    local ok, result = pcall(function()
        return mesh:PlayAnimationByPath(animationPath, true)
    end)
    debug_log("animation play path=" .. animationPath .. " ok=" .. tostring(ok) .. " result=" .. tostring(result))
    return ok and result ~= false
end

local function get_move_duration()
    local propertyDuration = tonumber(read_script_property("JumpScareMoveDuration"))
    if propertyDuration ~= nil and propertyDuration > 0 then
        return propertyDuration
    end
    return tonumber(MOVE_DURATION) or 0
end

local function get_target_location(startLocation)
    local bUseArrivalLocation = read_script_property("bUseJumpScareArrivalLocation") == true
    if bUseArrivalLocation then
        local arrivalLocation = read_script_property("JumpScareArrivalLocation")
        if arrivalLocation ~= nil then
            return arrivalLocation
        end
    end

    if startLocation == nil or MOVE_OFFSET == nil then
        return nil
    end
    return startLocation + MOVE_OFFSET
end

local function start_mesh_movement(mesh)
    if not bMoveMeshOnTrigger then
        MoveState = nil
        debug_log("movement skipped: disabled")
        return false
    end

    local duration = get_move_duration()
    if duration <= 0 then
        MoveState = nil
        debug_log("movement skipped: invalid duration=" .. tostring(duration))
        return false
    end

    local startLocation = get_component_relative_location(mesh)
    local targetLocation = copy_vec3(get_target_location(startLocation))
    if startLocation == nil or targetLocation == nil then
        MoveState = nil
        debug_log("movement skipped: invalid start or target location")
        return false
    end

    MoveState = {
        Mesh = mesh,
        StartLocation = startLocation,
        TargetLocation = targetLocation,
        Elapsed = 0.0,
        Duration = duration
    }
    debug_log("movement started start=" .. format_vec3(startLocation) .. " target=" .. format_vec3(targetLocation) .. " duration=" .. tostring(duration))
    return true
end

local function run_jump_scare()
    local mesh = get_target_mesh()
    debug_log("run jump scare mesh=" .. tostring(mesh))
    if OriginalMeshLocation ~= nil then
        set_component_relative_location(mesh, OriginalMeshLocation)
    end
    if bShowMeshOnTrigger then
        set_mesh_visible(mesh, true)
    end
    play_mesh_animation(mesh)
    play_jump_scare_sound(mesh)
    start_mesh_movement(mesh)
end

local function try_run_from_actor(otherActor, source)
    if obj == nil or obj.HasTag == nil then
        debug_log(source .. " ignored: invalid obj")
        return false
    end

    if not obj:HasTag(TAG_ACTIVE) then
        debug_log(source .. " ignored: missing " .. TAG_ACTIVE)
        return false
    end

    if bOneShot and obj:HasTag(TAG_TRIGGERED) then
        debug_log(source .. " ignored: already triggered")
        return false
    end

    if not is_player_actor(otherActor) then
        debug_log(source .. " ignored: other actor is not player")
        return false
    end

    if bOneShot and obj.AddTag ~= nil then
        obj:AddTag(TAG_TRIGGERED)
        debug_log(source .. " trigger tag added")
    end

    run_jump_scare()
    return true
end

local function is_player_inside_root_box(player)
    local box = find_trigger_box_component()
    if box == nil or box.GetLocation == nil or box.GetScaledBoxExtent == nil then
        debug_log_once("box_invalid", "poll ignored: trigger box component not found")
        return false
    end

    local playerLocation = get_actor_location(player)
    if playerLocation == nil then
        debug_log_once("box_invalid_player_location", "poll ignored: player location unavailable player=" .. tostring(player))
        return false
    end

    local boxLocation = box:GetLocation()
    local extent = box:GetScaledBoxExtent()
    if boxLocation == nil or extent == nil then
        debug_log_once("box_invalid_box_location", "poll ignored: box location or extent unavailable box=" .. tostring(box))
        return false
    end

    local inside = math.abs(playerLocation.X - boxLocation.X) <= extent.X and
        math.abs(playerLocation.Y - boxLocation.Y) <= extent.Y and
        math.abs(playerLocation.Z - boxLocation.Z) <= extent.Z
    if not inside then
        debug_log_once(
            "box_outside",
            "poll outside: player=" .. format_vec3(playerLocation) ..
                " box=" .. format_vec3(boxLocation) ..
                " extent=" .. format_vec3(extent)
        )
    end
    return inside
end

local function poll_player_overlap()
    if bPollingOverlapTriggered then
        return
    end
    if obj == nil or obj.HasTag == nil then
        debug_log_once("poll_invalid_obj", "poll ignored: invalid obj")
        return
    end
    if not obj:HasTag(TAG_ACTIVE) then
        debug_log_once("poll_missing_active", "poll ignored: missing " .. TAG_ACTIVE)
        return
    end
    if bOneShot and obj:HasTag(TAG_TRIGGERED) then
        bPollingOverlapTriggered = true
        debug_log_once("poll_already_triggered", "poll ignored: already triggered")
        return
    end

    local player = get_player_pawn()
    if player == nil then
        debug_log_once("poll_no_player", "poll ignored: possessed player pawn not found")
        return
    end

    if is_player_inside_root_box(player) then
        debug_log("poll overlap detected")
        bPollingOverlapTriggered = try_run_from_actor(player, "PollOverlap")
    end
end

function BeginPlay()
    debug_log("BeginPlay actor=" .. tostring(obj))
    local mesh = get_target_mesh()
    if mesh ~= nil then
        OriginalMeshLocation = get_component_relative_location(mesh)
        set_mesh_visible(mesh, false)
        debug_log("initial mesh cached and hidden")
    else
        debug_log("BeginPlay failed: target mesh not found")
    end
end

function ResetJumpScare()
    MoveState = nil
    bPollingOverlapTriggered = false
    TriggerBoxComponent = nil
    DebugOnce = {}

    local mesh = get_target_mesh()
    if mesh == nil then
        return false
    end

    if mesh.StopAnimation ~= nil then
        pcall(function()
            mesh:StopAnimation()
        end)
    end
    if OriginalMeshLocation ~= nil then
        set_component_relative_location(mesh, OriginalMeshLocation)
    end
    set_mesh_visible(mesh, false)
    return true
end

function OnOverlap(OtherActor, OverlappedComponent, OtherComp)
    debug_log("OnOverlap other=" .. tostring(OtherActor) .. " overlapped=" .. tostring(OverlappedComponent) .. " otherComp=" .. tostring(OtherComp))
    try_run_from_actor(OtherActor, "OnOverlap")
end

function Tick(dt)
    debug_log_once(
        "tick_enter",
        "Tick active=" .. tostring(obj ~= nil and obj.HasTag ~= nil and obj:HasTag(TAG_ACTIVE)) ..
            " triggered=" .. tostring(obj ~= nil and obj.HasTag ~= nil and obj:HasTag(TAG_TRIGGERED)) ..
            " dt=" .. tostring(dt)
    )
    poll_player_overlap()

    if MoveState == nil then
        return
    end

    local mesh = MoveState.Mesh
    if mesh == nil then
        MoveState = nil
        return
    end

    MoveState.Elapsed = MoveState.Elapsed + (tonumber(dt) or 0)
    local alpha = MoveState.Elapsed / MoveState.Duration
    if alpha >= 1.0 then
        set_component_relative_location(mesh, MoveState.TargetLocation)
        if mesh.StopAnimation ~= nil then
            pcall(function()
                mesh:StopAnimation()
            end)
        end
        set_mesh_visible(mesh, false)
        MoveState = nil
        return
    end
    if alpha < 0.0 then
        alpha = 0.0
    end

    local location = lerp_vec3(MoveState.StartLocation, MoveState.TargetLocation, alpha)
    set_component_relative_location(mesh, location)
end
