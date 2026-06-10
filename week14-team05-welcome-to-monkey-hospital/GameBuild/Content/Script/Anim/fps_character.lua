local PISTOL_IDLE_PATH = "Content/Data/human/source/Armpist_Armature_FPS_Pistol_Idle.uasset"
local PISTOL_WALK_PATH = "Content/Data/human/source/Armpist_Armature_FPS_Pistol_Walk.uasset"
local PISTOL_FIRE_PATH = "Content/Data/human/source/Armpist_Armature_FPS_Pistol_Fire.uasset"
local CAMERA_MESH_PATH = "Content/Data/camera/camera_StaticMesh.uasset"
local GameManager = require("GameManager")
local EndingManager = require("EndingManager")
local SoundManager = require("SoundManager")
local ToolManager = require("ToolManager")
local SettingManager = require("SettingManager")

local FPS_SPEED_THRESHOLD = 0.5
local FPS_IDLE_TO_WALK_BLEND = 0.15
local FPS_WALK_TO_IDLE_BLEND = 0.15
local FPS_CAMERA_TO_PISTOL_BLEND = 0.15
local FPS_FIRE_ENTER_BLEND = 0.05
local FPS_FIRE_EXIT_BLEND = 0.1

local KEY_SPACE = 0x20
local KEY_RBUTTON = 0x02
local PISTOL_SOCKET = "PistolSocket"
local MUZZLE_SOCKET = "Muzzle"
local PROJECTILE_TEMPLATE_PATH = "Content/Blueprint/AStaticMeshActor_8.ActorTemplate"
local PROJECTILE_SPAWN_OFFSET = 0.0
local CAMERA_TRACE_DISTANCE = 1000.0
local PHOTO_BLACKOUT_TARGET_TAG = "PhotoBlackoutTarget"
local TOY_PROJECTILE_TAG = "ToyProjectile"

local TOOL_PISTOL = ToolManager.Tool.Pistol
local TOOL_CAMERA = ToolManager.Tool.Camera

local function sync_tool_state(tool)
    ToolManager:SetCurrentTool(tool)
end

local SWITCH_NONE = 0
local SWITCH_TO_CAMERA = 1
local SWITCH_TO_PISTOL = 2

local ACTION_NONE = 0
local ACTION_PISTOL_FIRE = 1

local TOOL_SWITCH_DURATION = 0.35
local ARMS_READY_PITCH = 0.0
local ARMS_DOWN_PITCH = 65.0

-- Camera mesh is expected to be a child of CameraComponent.
-- X is forward from the camera, Z moves it between lower-screen hidden and raised positions.
local CAMERA_READY_X = 0.2
local CAMERA_READY_Y = 0.0
local CAMERA_READY_Z = -0.2
local CAMERA_DOWN_X = 0.5
local CAMERA_DOWN_Y = 0.0
local CAMERA_DOWN_Z = -1.0
local CAMERA_BOB_RATE = 9.0
local CAMERA_BOB_SMOOTH = 8.0
local CAMERA_BOB_FORWARD_AMOUNT = 0.017
local CAMERA_BOB_SIDE_AMOUNT = 0.022
local CAMERA_BOB_UP_AMOUNT = 0.026
local HEAD_BOB_ROLL_DEGREES = 2.75
local HEAD_BOB_PITCH_DEGREES = 1.35
local HEAD_BOB_OFFSET_Z = 0.014
local HEAD_BOB_ROLL_PHASE_OFFSET = 0.42
local HEAD_BOB_SMOOTH = 17.0
local HEAD_BOB_AIM_SCALE = 0.15
local WALK_BOB_WEIGHT_SPEED = 3.2
local FOOTSTEP_PHASE_HALF = math.pi
local TWO_PI = math.pi * 2.0

local function clamp01(value)
    if value < 0.0 then
        return 0.0
    elseif value > 1.0 then
        return 1.0
    end
    return value
end

local function smooth_step(value)
    value = clamp01(value)
    return value * value * (3.0 - 2.0 * value)
end

local function lerp(a, b, alpha)
    return a + (b - a) * alpha
end

local function is_pistol(self)
    return self.CurrentTool == TOOL_PISTOL
end

local function is_idle(self)
    return self.Speed <= self.SpeedThreshold
end

local function is_walk(self)
    return self.Speed > self.SpeedThreshold
end

local function is_switching(self)
    return self.SwitchPhase ~= SWITCH_NONE
end

local function set_camera_mesh_position(alpha, bob_x, bob_y, bob_z)
    bob_x = bob_x or 0.0
    bob_y = bob_y or 0.0
    bob_z = bob_z or 0.0

    Anim.set_static_mesh_relative_location_by_path(
        CAMERA_MESH_PATH,
        lerp(CAMERA_DOWN_X, CAMERA_READY_X, alpha) + bob_x,
        lerp(CAMERA_DOWN_Y, CAMERA_READY_Y, alpha) + bob_y,
        lerp(CAMERA_DOWN_Z, CAMERA_READY_Z, alpha) + bob_z)
end

local function get_walk_bob_speed_factor(weight)
    return lerp(0.55, 1.0, weight)
end

local function update_walk_bob(self, dt)
    local targetWeight = 0.0
    if self.Speed > self.SpeedThreshold then
        targetWeight = clamp01((self.Speed - self.SpeedThreshold) / WALK_BOB_WEIGHT_SPEED)
    end

    self.WalkBobWeight = lerp(self.WalkBobWeight, targetWeight, clamp01(dt * CAMERA_BOB_SMOOTH))
    local speedFactor = get_walk_bob_speed_factor(self.WalkBobWeight)
    self.WalkBobTime = self.WalkBobTime + dt * CAMERA_BOB_RATE * speedFactor
end

local function update_camera_hold_motion(self)
    local phase = self.WalkBobTime
    local weight = self.WalkBobWeight
    local bobX = math.sin(phase * 2.0) * CAMERA_BOB_FORWARD_AMOUNT * weight
    local bobY = math.sin(phase) * CAMERA_BOB_SIDE_AMOUNT * weight
    local bobZ = math.abs(math.sin(phase)) * CAMERA_BOB_UP_AMOUNT * weight

    set_camera_mesh_position(1.0, bobX, bobY, bobZ)
end

local function sharpen_step_wave(value)
    value = clamp01(math.abs(value))
    return value * value
end

local function is_aiming(self)
    if self.CurrentTool ~= TOOL_PISTOL then
        return false
    end

    if Input ~= nil and Input.GetAction ~= nil then
        local ok, down = pcall(function()
            return Input.GetAction("Aim")
        end)
        if ok then
            return down == true
        end
    end

    if Input ~= nil and Input.GetKey ~= nil then
        local ok, down = pcall(function()
            return Input.GetKey(KEY_RBUTTON)
        end)
        return ok and down == true
    end

    return false
end

local function is_action_pressed(action_name)
    if Input ~= nil and Input.GetActionDown ~= nil then
        local ok, pressed = pcall(function()
            return Input.GetActionDown(action_name)
        end)
        if ok then
            return pressed == true
        end
    end
    return false
end

local function get_head_bob_amplitude_scale(self)
    local scale = self.WalkBobWeight
    if scale <= 0.0 then
        return 0.0
    end

    if is_aiming(self) then
        scale = scale * HEAD_BOB_AIM_SCALE
    end

    return scale
end

local function get_head_bob_targets(self)
    local phase = self.WalkBobTime
    local scale = get_head_bob_amplitude_scale(self)
    local stepWave = sharpen_step_wave(math.sin(phase))
    local roll = math.sin(phase + HEAD_BOB_ROLL_PHASE_OFFSET) * HEAD_BOB_ROLL_DEGREES * scale
    local pitch = -stepWave * HEAD_BOB_PITCH_DEGREES * scale
    local offsetZ = -stepWave * HEAD_BOB_OFFSET_Z * scale
    return roll, pitch, offsetZ
end

local function apply_head_bob_to_camera(self)
    if Anim.apply_head_bob == nil then
        return
    end

    Anim.apply_head_bob(self.HeadBobRoll, self.HeadBobPitch, self.HeadBobOffsetZ)
end

local function reset_head_bob(self)
    self.HeadBobRoll = 0.0
    self.HeadBobPitch = 0.0
    self.HeadBobOffsetZ = 0.0
    apply_head_bob_to_camera(self)
end

local function clear_ending_stagger_visuals(self)
    if Anim.clear_owner_mesh_shake_offset ~= nil then
        Anim.clear_owner_mesh_shake_offset()
    end
    if self ~= nil then
        reset_head_bob(self)
    end
end

local function apply_ending_stagger_neutral(self)
    if Anim.apply_head_bob ~= nil then
        Anim.apply_head_bob(0.0, 0.0, 0.0)
    end
    if Anim.apply_owner_mesh_shake_offset ~= nil then
        Anim.apply_owner_mesh_shake_offset(0.0, 0.0, 0.0, 0.0, 0.0)
    end
    if self ~= nil then
        self.HeadBobRoll = 0.0
        self.HeadBobPitch = 0.0
        self.HeadBobOffsetZ = 0.0
    end
end

local function update_ending_stagger_visuals(self, dt)
    if EndingManager == nil
        or EndingManager.IsStaggerShakeActive == nil
        or not EndingManager:IsStaggerShakeActive() then
        apply_ending_stagger_neutral(self)
        return
    end

    if EndingManager.UpdateStaggerShake ~= nil then
        EndingManager:UpdateStaggerShake(dt)
    end

    local offset = nil
    if EndingManager.GetStaggerShakeOffset ~= nil then
        offset = EndingManager:GetStaggerShakeOffset()
    end
    if offset == nil then
        return
    end

    local roll = offset.Roll or 0.0
    local pitch = offset.Pitch or 0.0
    local locX = offset.LocX or 0.0
    local locY = offset.LocY or 0.0
    local locZ = offset.LocZ or 0.0

    if Anim.apply_head_bob ~= nil then
        Anim.apply_head_bob(roll, pitch, locZ)
    end

    if Anim.apply_owner_mesh_shake_offset ~= nil then
        Anim.apply_owner_mesh_shake_offset(roll, pitch, locX, locY, locZ)
    end
end

local function update_head_bob(self, dt)
    if Anim.apply_head_bob == nil then
        return
    end

    if not SettingManager:IsHeadBobEnabled() then
        if self.HeadBobRoll ~= 0.0 or self.HeadBobPitch ~= 0.0 or self.HeadBobOffsetZ ~= 0.0 then
            reset_head_bob(self)
        end
        return
    end

    local smoothAlpha = clamp01((tonumber(dt) or 0.0) * HEAD_BOB_SMOOTH)
    local targetRoll = 0.0
    local targetPitch = 0.0
    local targetOffsetZ = 0.0

    if self.Speed > self.SpeedThreshold and self.WalkBobWeight > 0.01 then
        targetRoll, targetPitch, targetOffsetZ = get_head_bob_targets(self)
    end

    self.HeadBobRoll = lerp(self.HeadBobRoll, targetRoll, smoothAlpha)
    self.HeadBobPitch = lerp(self.HeadBobPitch, targetPitch, smoothAlpha)
    self.HeadBobOffsetZ = lerp(self.HeadBobOffsetZ, targetOffsetZ, smoothAlpha)
    apply_head_bob_to_camera(self)
end

local function get_pistol_walk_play_rate(self)
    if self.PistolWalkLength == nil or self.PistolWalkLength <= 0.0 then
        return 1.0
    end

    local speedFactor = get_walk_bob_speed_factor(self.WalkBobWeight)
    return self.PistolWalkLength * CAMERA_BOB_RATE * speedFactor / TWO_PI
end

local function update_pistol_walk_play_rate(self)
    if self.PistolWalkPlayer == nil or Anim.set_sequence_play_rate == nil then
        return
    end

    if self.CurrentTool ~= TOOL_PISTOL or self.Speed <= self.SpeedThreshold then
        Anim.set_sequence_play_rate(self.PistolWalkPlayer, 1.0)
        return
    end

    Anim.set_sequence_play_rate(self.PistolWalkPlayer, get_pistol_walk_play_rate(self))
end

local function get_crosshair_aim_target(owner)
    if World == nil or World.LineTraceObjects == nil or owner == nil then
        return nil
    end

    local camera = owner:GetCamera()
    if camera == nil then
        return nil
    end

    local start = camera:GetLocation()
    local direction = camera.Forward
    local fallbackTarget = start + direction * CAMERA_TRACE_DISTANCE
    local hit = World.LineTraceObjects(start, fallbackTarget, owner)
    if hit ~= nil and hit.Hit and hit.Location ~= nil then
        return hit.Location
    end

    return fallbackTarget
end

local function play_party_blower_audio()
    SoundManager:PlayPartyBlower()
end

local function play_empty_gun_shot_audio()
    SoundManager:PlayEmptyGunShot()
end

local function spawn_projectile_from_muzzle()
    if World == nil or World.SpawnActorTemplate == nil then
        return nil
    end

    if not Anim.has_socket(MUZZLE_SOCKET) then
        return nil
    end

    local location = Anim.get_socket_location(MUZZLE_SOCKET)
    local rotation = Anim.get_socket_rotation(MUZZLE_SOCKET)
    local forward = Anim.get_socket_forward(MUZZLE_SOCKET)
    local spawnLocation = location + forward * PROJECTILE_SPAWN_OFFSET
    local owner = Anim.get_owner_actor()

    local projectile = World.SpawnActorTemplate(
        PROJECTILE_TEMPLATE_PATH,
        spawnLocation,
        rotation)

    if projectile ~= nil then
        if projectile.AddTag ~= nil then
            projectile:AddTag(TOY_PROJECTILE_TAG)
        end

        local movement = projectile:GetProjectileMovementComponent()
        if movement ~= nil then
            movement:SetIgnoredActor(owner)

            local aimTarget = get_crosshair_aim_target(owner)
            if aimTarget ~= nil then
                local aimDirection = Math.Normalize(aimTarget - spawnLocation)
                if aimDirection:Length() > 0.0001 then
                    movement:SetVelocity(aimDirection)
                end
            end
        end
    end

    return projectile
end

local function play_pistol_fire_effect(owner)
    if HospitalPlayer ~= nil and HospitalPlayer.play_pistol_fire_effect ~= nil then
        HospitalPlayer.play_pistol_fire_effect(owner)
    end
end

local function start_pistol_fire_action(self, bPlayGunAudio)
    self.ActionTime = 0.0
    self.ActionPhase = ACTION_PISTOL_FIRE

    if bPlayGunAudio ~= true then
        return
    end

    local owner = Anim.get_owner_actor()
    play_pistol_fire_effect(owner)

    if Anim.play_pistol_fire_audio ~= nil then
        Anim.play_pistol_fire_audio()
    end
end

local function get_camera_trace_hit()
    if World == nil or World.LineTraceObjects == nil then
        return nil
    end

    local owner = Anim.get_owner_actor()
    if owner == nil then
        return nil
    end

    local camera = owner:GetCamera()
    if camera == nil then
        return nil
    end

    local start = camera:GetLocation()
    local direction = camera.Forward
    local hit = World.LineTraceObjects(start, start + direction * CAMERA_TRACE_DISTANCE, owner)
    if hit == nil or not hit.Hit or hit.Actor == nil then
        return nil
    end

    hit.ShotDirection = direction
    return hit
end

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

local function get_skeletal_mesh(actor)
    if actor == nil or actor.GetSkeletalMeshComponent == nil then
        return nil
    end
    return actor:GetSkeletalMeshComponent()
end

local function is_actor_in_camera_frustum(actor)
    if World == nil or not is_valid_actor(actor) then
        return false
    end

    local mesh = get_skeletal_mesh(actor)
    if mesh ~= nil and World.IsComponentInViewFrustum ~= nil then
        return World.IsComponentInViewFrustum(mesh)
    end

    if World.IsActorInViewFrustum ~= nil then
        return World.IsActorInViewFrustum(actor)
    end

    return false
end

local function get_camera_height_target_location(actor, cameraLocation)
    local targetLocation = get_actor_location(actor)
    if targetLocation == nil or cameraLocation == nil then
        return nil
    end
    return Vec3(targetLocation.X, targetLocation.Y, cameraLocation.Z)
end

local function is_trace_clear_to_actor_from_camera(camera, targetActor, ignoreActor)
    if World == nil or World.LineTraceObjects == nil then
        return false
    end
    if camera == nil or camera.GetLocation == nil or not is_valid_actor(targetActor) then
        return false
    end

    local start = camera:GetLocation()
    local endLocation = get_camera_height_target_location(targetActor, start)
    if endLocation == nil then
        return false
    end

    local hit = World.LineTraceObjects(start, endLocation, ignoreActor)
    if hit == nil or not hit.Hit then
        return true
    end
    return hit.Actor == targetActor
end

local function should_blackout_photo_capture()
    if GameManager == nil or
        GameManager.GetActiveAnomalyTarget == nil then
        return false
    end

    local target = GameManager:GetActiveAnomalyTarget()
    if target == nil or target.HasTag == nil or not target:HasTag(PHOTO_BLACKOUT_TARGET_TAG) then
        return false
    end

    if not is_actor_in_camera_frustum(target) then
        return false
    end

    local owner = Anim.get_owner_actor()
    if owner == nil or owner.GetCamera == nil then
        return false
    end

    return is_trace_clear_to_actor_from_camera(owner:GetCamera(), target, owner)
end

local function resolve_pistol_shot()
    local hit = get_camera_trace_hit()
    if hit == nil or hit.Actor == nil then
        return "miss", hit
    end

    local hitActor = hit.Actor
    if GameManager ~= nil
        and GameManager.ReportAnomalyShot ~= nil
        and GameManager:ReportAnomalyShot(hitActor, hit) then
        return "anomaly", hit
    end

    return "miss", hit
end

local function consume_pistol_bullet()
    if GameManager == nil or GameManager.ConsumePlayerBullet == nil then
        return true
    end
    return GameManager:ConsumePlayerBullet()
end

local function report_pistol_shot_failure()
    if GameManager == nil or GameManager.ReportPlayerShotFailure == nil then
        return false
    end
    return GameManager:ReportPlayerShotFailure("PistolShotMiss")
end

local function is_ending_cutscene()
    return GameManager ~= nil
        and GameManager.IsEnding ~= nil
        and GameManager:IsEnding()
end

local function can_fire_pistol()
    if is_ending_cutscene() then
        return false
    end

    if GameManager ~= nil then
        if GameManager.IsLoopStopped ~= nil and GameManager:IsLoopStopped() then
            return false
        end
    end

    return true
end

local function try_consume_pistol_bullet_for_fire()
    if not can_fire_pistol() then
        play_empty_gun_shot_audio()
        return false
    end

    if not consume_pistol_bullet() then
        play_empty_gun_shot_audio()
        return false
    end

    return true
end

local function can_request_photo_capture()
    if Anim == nil or Anim.is_photo_capture_available == nil then
        return true
    end
    return Anim.is_photo_capture_available()
end

local function notify_photo_capture_requested()
    if GameManager == nil or GameManager.NotifyPhotoCaptureRequested == nil then
        return false
    end
    return GameManager:NotifyPhotoCaptureRequested()
end

local function show_pistol(self)
    sync_tool_state(TOOL_PISTOL)
    Anim.set_owner_mesh_pitch(ARMS_READY_PITCH)
    Anim.set_owner_mesh_visibility(true)
    Anim.set_socket_child_visibility(PISTOL_SOCKET, true)
    Anim.set_static_mesh_visibility_by_path(CAMERA_MESH_PATH, false)
    set_camera_mesh_position(0.0)
end

local function show_camera(self)
    sync_tool_state(TOOL_CAMERA)
    Anim.set_owner_mesh_pitch(ARMS_DOWN_PITCH)
    Anim.set_owner_mesh_visibility(false)
    Anim.set_socket_child_visibility(PISTOL_SOCKET, false)
    Anim.set_static_mesh_visibility_by_path(CAMERA_MESH_PATH, true)
    set_camera_mesh_position(1.0)
end

local function update_switch_to_camera(self, alpha)
    alpha = smooth_step(alpha)
    Anim.set_owner_mesh_visibility(true)
    Anim.set_socket_child_visibility(PISTOL_SOCKET, true)
    Anim.set_static_mesh_visibility_by_path(CAMERA_MESH_PATH, true)
    Anim.set_owner_mesh_pitch(lerp(ARMS_READY_PITCH, ARMS_DOWN_PITCH, alpha))
    set_camera_mesh_position(alpha)
end

local function reset_footstep_tracking(self)
    self.WalkFootstepPhaseIndex = nil
end

local function play_footstep()
    if Anim.play_footstep_audio ~= nil then
        Anim.play_footstep_audio()
    end
end

local function update_walk_footsteps(self)
    if self.WalkBobWeight <= 0.01 then
        self.WalkFootstepPhaseIndex = nil
        return
    end

    local phaseIndex = math.floor(self.WalkBobTime / FOOTSTEP_PHASE_HALF)
    if self.WalkFootstepPhaseIndex ~= nil and phaseIndex ~= self.WalkFootstepPhaseIndex then
        local delta = phaseIndex - self.WalkFootstepPhaseIndex
        if delta < 0 then
            delta = 1
        end
        for _ = 1, delta do
            play_footstep()
        end
    end
    self.WalkFootstepPhaseIndex = phaseIndex
end

local function update_footsteps(self)
    if self.Speed <= self.SpeedThreshold then
        reset_footstep_tracking(self)
        return
    end

    update_walk_footsteps(self)
end

local function update_switch_to_pistol(self, alpha)
    alpha = smooth_step(alpha)
    Anim.set_owner_mesh_visibility(true)
    Anim.set_socket_child_visibility(PISTOL_SOCKET, true)
    Anim.set_static_mesh_visibility_by_path(CAMERA_MESH_PATH, true)
    Anim.set_owner_mesh_pitch(lerp(ARMS_DOWN_PITCH, ARMS_READY_PITCH, alpha))
    set_camera_mesh_position(1.0 - alpha)
end

function init(self)
    self.Speed = 0.0
    self.SpeedThreshold = FPS_SPEED_THRESHOLD
    self.CurrentTool = TOOL_PISTOL
    self.SwitchPhase = SWITCH_NONE
    self.SwitchTime = 0.0
    self.ActionPhase = ACTION_NONE
    self.ActionTime = 0.0
    self.WalkBobTime = 0.0
    self.WalkBobWeight = 0.0
    self.HeadBobRoll = 0.0
    self.HeadBobPitch = 0.0
    self.HeadBobOffsetZ = 0.0
    self.PistolFireDuration = Anim.get_sequence_length(PISTOL_FIRE_PATH)
    self.PistolWalkLength = Anim.get_sequence_length(PISTOL_WALK_PATH)
    reset_footstep_tracking(self)

    local fps = Anim.create_state_machine("FPS")
    local pistolWalkPlayer = Anim.create_sequence_player(PISTOL_WALK_PATH, 1.0, true)
    self.PistolWalkPlayer = pistolWalkPlayer

    Anim.sm_add_state(fps, "PistolIdle", Anim.create_sequence_player(PISTOL_IDLE_PATH, 1.0, true))
    Anim.sm_add_state(fps, "PistolWalk", pistolWalkPlayer)
    Anim.sm_add_state(fps, "PistolFire", Anim.create_sequence_player(PISTOL_FIRE_PATH, 1.0, false))
    Anim.sm_add_state(fps, "CameraHold", Anim.create_ref_pose())

    Anim.sm_add_transition(fps, "PistolIdle", "PistolWalk",
        function()
            return not is_switching(self) and self.ActionPhase == ACTION_NONE and is_pistol(self) and is_walk(self)
        end,
        FPS_IDLE_TO_WALK_BLEND)

    Anim.sm_add_transition(fps, "PistolWalk", "PistolIdle",
        function()
            return not is_switching(self) and self.ActionPhase == ACTION_NONE and is_pistol(self) and is_idle(self)
        end,
        FPS_WALK_TO_IDLE_BLEND)

    Anim.sm_add_transition(fps, "PistolIdle", "PistolFire",
        function()
            return self.ActionPhase == ACTION_PISTOL_FIRE
        end,
        FPS_FIRE_ENTER_BLEND)

    Anim.sm_add_transition(fps, "PistolWalk", "PistolFire",
        function()
            return self.ActionPhase == ACTION_PISTOL_FIRE
        end,
        FPS_FIRE_ENTER_BLEND)

    Anim.sm_add_transition(fps, "PistolFire", "PistolIdle",
        function()
            return self.ActionPhase == ACTION_NONE and is_pistol(self) and is_idle(self)
        end,
        FPS_FIRE_EXIT_BLEND)

    Anim.sm_add_transition(fps, "PistolFire", "PistolWalk",
        function()
            return self.ActionPhase == ACTION_NONE and is_pistol(self) and is_walk(self)
        end,
        FPS_FIRE_EXIT_BLEND)

    Anim.sm_add_transition(fps, "PistolIdle", "CameraHold",
        function()
            return self.SwitchPhase == SWITCH_NONE and self.CurrentTool == TOOL_CAMERA
        end,
        0.0)

    Anim.sm_add_transition(fps, "PistolWalk", "CameraHold",
        function()
            return self.SwitchPhase == SWITCH_NONE and self.CurrentTool == TOOL_CAMERA
        end,
        0.0)

    Anim.sm_add_transition(fps, "CameraHold", "PistolIdle",
        function()
            return self.SwitchPhase == SWITCH_NONE and is_pistol(self) and is_idle(self)
        end,
        FPS_CAMERA_TO_PISTOL_BLEND)

    Anim.sm_add_transition(fps, "CameraHold", "PistolWalk",
        function()
            return self.SwitchPhase == SWITCH_NONE and is_pistol(self) and is_walk(self)
        end,
        FPS_CAMERA_TO_PISTOL_BLEND)

    Anim.sm_set_initial_state(fps, "PistolIdle")
    Anim.set_root_node(fps)
    self.FpsStateMachine = fps
    show_pistol(self)
    sync_tool_state(TOOL_PISTOL)
end

function update(self, dt)
    self.Speed = Anim.get_owner_speed()

    local bEndingCutscene = is_ending_cutscene()
    if self.bWasEndingCutscene == true and not bEndingCutscene then
        clear_ending_stagger_visuals(self)
    end
    self.bWasEndingCutscene = bEndingCutscene

    if bEndingCutscene then
        if self.ActionPhase == ACTION_PISTOL_FIRE then
            self.ActionTime = self.ActionTime + dt
            if self.ActionTime >= self.PistolFireDuration then
                self.ActionTime = 0.0
                self.ActionPhase = ACTION_NONE
            end
        end
        update_ending_stagger_visuals(self, dt)
        return
    end

    if self.ActionPhase == ACTION_PISTOL_FIRE then
        self.ActionTime = self.ActionTime + dt
        if self.ActionTime >= self.PistolFireDuration then
            self.ActionTime = 0.0
            self.ActionPhase = ACTION_NONE
        end
        return
    end

    if self.SwitchPhase == SWITCH_NONE then
        if is_action_pressed("Jump") or Anim.is_key_pressed(KEY_SPACE) then
            self.SwitchTime = 0.0
            if self.CurrentTool == TOOL_PISTOL then
                self.SwitchPhase = SWITCH_TO_CAMERA
                update_switch_to_camera(self, 0.0)
            else
                self.SwitchPhase = SWITCH_TO_PISTOL
                update_switch_to_pistol(self, 0.0)
            end
        elseif self.CurrentTool == TOOL_PISTOL and (is_action_pressed("Fire") or Anim.is_left_mouse_pressed()) then
            if try_consume_pistol_bullet_for_fire() then
                local shotKind = resolve_pistol_shot()
                if shotKind == "anomaly" then
                    start_pistol_fire_action(self, true)
                else
                    start_pistol_fire_action(self, false)
                    report_pistol_shot_failure()
                    play_party_blower_audio()
                    spawn_projectile_from_muzzle()
                end
            end
        elseif self.CurrentTool == TOOL_CAMERA then
            if (is_action_pressed("Fire") or Anim.is_left_mouse_pressed()) and can_request_photo_capture() then
                notify_photo_capture_requested()
                Anim.request_photo_capture(should_blackout_photo_capture())
            end
        end

        update_walk_bob(self, dt)
        update_pistol_walk_play_rate(self)
        update_head_bob(self, dt)
        if self.CurrentTool == TOOL_CAMERA then
            update_camera_hold_motion(self)
        end
        update_footsteps(self)
        return
    end

    reset_footstep_tracking(self)
    reset_head_bob(self)
    self.SwitchTime = self.SwitchTime + dt
    local alpha = clamp01(self.SwitchTime / TOOL_SWITCH_DURATION)

    if self.SwitchPhase == SWITCH_TO_CAMERA then
        update_switch_to_camera(self, alpha)
        if alpha >= 1.0 then
            self.SwitchTime = 0.0
            self.SwitchPhase = SWITCH_NONE
            self.CurrentTool = TOOL_CAMERA
            show_camera(self)
        end
    elseif self.SwitchPhase == SWITCH_TO_PISTOL then
        update_switch_to_pistol(self, alpha)
        if alpha >= 1.0 then
            self.SwitchTime = 0.0
            self.SwitchPhase = SWITCH_NONE
            self.CurrentTool = TOOL_PISTOL
            show_pistol(self)
        end
    end
end
