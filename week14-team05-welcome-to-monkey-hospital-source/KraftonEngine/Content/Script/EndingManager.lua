local GameManager = require("GameManager")
local LoopManager = require("LoopManager")
local StageManager = require("StageManager")
local SoundManager = require("SoundManager")
local UIManager = require("UIManager")
local LeaderboardManager = require("LeaderboardManager")

local EndingManager = {}

EndingManager.bActive = false

EndingManager.ENDING_SPAWN_TAG = "EndingSpawn"
EndingManager.ENDING_SUN_NAME = "EndingSun"
EndingManager.ENDING_SUN_TAG = "EndingLighting"
EndingManager.ENDING_MAP_NAME = "EndingHospital"

EndingManager.FALLBACK_SPAWN = Vec3(600.0, 0.0, 0.0)
EndingManager.ENDING_SPAWN_YAW = -180.0
EndingManager.ENDING_SPAWN_PITCH = 15.0
EndingManager.WAKE_UP_SHOT_TRACE_DISTANCE = 1000.0

EndingManager.VICTIM_ACTOR_TEMPLATE = "Content/Blueprint/ending/EndingVictim.ActorTemplate"
EndingManager.VICTIM_LOCATION = Vec3(597.0, 0.0, 0.0)
EndingManager.VICTIM_ROTATION = Vec3(0.0, 0.0, 0.0)
EndingManager.VICTIM_SCALE = Vec3(0.35, 0.35, 0.35)
EndingManager.VICTIM_ANIMATION_PATH =
    "Content/Data/ending-hospital-map-data/victim-with-animation_Object_4_C4D_Animation_Take.uasset"
EndingManager.VICTIM_ANIMATION_LOOPING = false
EndingManager.VICTIM_ANIMATION_PLAY_RATE = 1.0

EndingManager.MONKEY_ACTOR_TEMPLATE = "Content/Blueprint/TitleMonkey.ActorTemplate"
EndingManager.MONKEY_LOCATION = Vec3(604.0, 0.0, 0.0)
EndingManager.MONKEY_ROTATION = Vec3(0.0, 0.0, 270.0)
EndingManager.MONKEY_SCALE = Vec3(0.25, 0.25, 0.25)
EndingManager.MONKEY_ENTRY_ANIMATION_PATH =
    "Content/Data/CymbalMonkey/CymbalMonkey_Joints_ArmOnlyCymbalEntry.uasset"
EndingManager.MONKEY_STRIKE_ANIMATION_PATH =
    "Content/Data/CymbalMonkey/CymbalMonkey_Joints_ArmOnlyCymbalStrike.uasset"
EndingManager.MONKEY_ENTRY_TO_STRIKE_SECONDS = 5.0
EndingManager.MONKEY_STRIKE_PLAY_RATE = 1.0
EndingManager.MONKEY_AUDIO_VOLUME = 10.0
EndingManager.MONKEY_STRIKE_BLACKOUT_HOLD_SECONDS = 2.0
EndingManager.ENDING_CREDIT_DISPLAY_SECONDS = 4.0
EndingManager.MAX_PLAYER_NAME_LENGTH = 16
EndingManager.DEFAULT_PLAYER_NAME = "Player"

-- 엔딩 진입 후 카메라 연출 타이밍 (초)
EndingManager.STAGGER_DELAY = 2.0
EndingManager.STAGGER_SHAKE_DURATION = 10.0
EndingManager.TURN_START_TIME = EndingManager.STAGGER_DELAY + EndingManager.STAGGER_SHAKE_DURATION
EndingManager.FACING_TURN_BLEND = 2.0
EndingManager.FACING_TURN_EASE_POWER = 5.0

-- Lua 연속 휘청: 빠름 <-> 느림 교차 (멈춤 없음, Roll 위주)
EndingManager.STAGGER_SPEED_CYCLE = 5.0
EndingManager.STAGGER_FAST_MULT = 0.9
EndingManager.STAGGER_SLOW_MULT = 0.12
EndingManager.STAGGER_BASE_FREQ = 0.22
EndingManager.STAGGER_ROLL_DEGREES = 2.0
EndingManager.STAGGER_PITCH_DEGREES = 0.25
EndingManager.STAGGER_LOC_Z = 0.01
EndingManager.STAGGER_LOC_XY = 0.006
EndingManager.STAGGER_FADE_IN_SECONDS = 1.0

EndingManager.ENDING_SIREN_KEY = "DistantSiren"
EndingManager.ENDING_SIREN_VOLUME = 0.7

EndingManager.VictimActor = nil
EndingManager.MonkeyActor = nil
EndingManager.SpawnFacingYaw = EndingManager.ENDING_SPAWN_YAW
EndingManager.SequenceCoroutine = nil
EndingManager.MonkeySequenceCoroutine = nil
EndingManager.bEndingCreditsMusicActive = false
EndingManager.bPreserveCreditsMusicOnReset = false
EndingManager.bNameInputActive = false
EndingManager.PlayerNameBuffer = ""
EndingManager.PendingLeaderboardRecord = nil
EndingManager.bSubmittingToTitle = false
EndingManager.ReturnToTitleCallback = nil
EndingManager.bStaggerShakeActive = false
EndingManager.StaggerElapsed = 0.0
EndingManager.StaggerPhase = 0.0
EndingManager.StaggerCurrentOffset = {
    Roll = 0.0,
    Pitch = 0.0,
    LocX = 0.0,
    LocY = 0.0,
    LocZ = 0.0,
}

EndingManager.HORROR_LIGHT_CLASSES = {
    "AAmbientLightActor",
    "ASpotLightActor",
    "APointLightActor",
}

local function is_valid_actor(actor)
    if actor == nil then
        return false
    end
    if actor.IsValid == nil then
        return true
    end
    return actor:IsValid()
end

local function find_actor_by_name(name)
    if World == nil or World.FindActorByName == nil or name == nil then
        return nil
    end
    local ok, actor = pcall(function()
        return World.FindActorByName(name)
    end)
    if ok and is_valid_actor(actor) then
        return actor
    end
    return nil
end

local function find_first_actor_by_tag(tag)
    if World == nil or World.FindFirstActorByTag == nil or tag == nil then
        return nil
    end
    local ok, actor = pcall(function()
        return World.FindFirstActorByTag(tag)
    end)
    if ok and is_valid_actor(actor) then
        return actor
    end
    return nil
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

local function get_pawn_from_player(player)
    if player == nil then
        return get_player_pawn()
    end

    if player.AsPawn ~= nil then
        local ok, pawn = pcall(function()
            return player:AsPawn()
        end)
        if ok and pawn ~= nil then
            return pawn
        end
    end

    if player.SetControlRotation ~= nil then
        return player
    end

    return get_player_pawn()
end

local function normalize_yaw_degrees(yaw)
    while yaw > 180.0 do
        yaw = yaw - 360.0
    end
    while yaw < -180.0 do
        yaw = yaw + 360.0
    end
    return yaw
end

local function get_opposite_yaw(yaw)
    return normalize_yaw_degrees(yaw + 180.0)
end

local function set_control_yaw(player, yaw, pitch)
    local pawn = get_pawn_from_player(player)
    if pawn == nil and player == nil then
        return
    end

    local roll = 0.0
    if pawn ~= nil and pawn.GetControlRotation ~= nil then
        local ok, rotation = pcall(function()
            return pawn:GetControlRotation()
        end)
        if ok and rotation ~= nil then
            roll = rotation.X or 0.0
        end
    end

    local targetRotation = Vec3(roll, pitch or EndingManager.ENDING_SPAWN_PITCH, yaw)

    if pawn ~= nil and pawn.SetControlRotation ~= nil then
        pcall(function()
            pawn:SetControlRotation(targetRotation)
        end)
    end

    if player ~= nil and player.SetRotation ~= nil then
        pcall(function()
            player:SetRotation(targetRotation)
        end)
    elseif pawn ~= nil and pawn.SetRotation ~= nil then
        pcall(function()
            pawn:SetRotation(targetRotation)
        end)
    end
end

local function stop_camera_shakes()
    if World == nil or World.GetFirstPlayerController == nil then
        return
    end

    local controller = World.GetFirstPlayerController()
    if controller == nil or controller.GetPlayerCameraManager == nil then
        return
    end

    local ok, manager = pcall(function()
        return controller:GetPlayerCameraManager()
    end)
    if ok and manager ~= nil and manager.StopAllCameraShakes ~= nil then
        pcall(function()
            manager:StopAllCameraShakes(true)
        end)
    end
end

local function start_stagger_camera_shake()
    if CameraManager == nil then
        return
    end

    if CameraManager.StartCameraShakeAsset ~= nil then
        pcall(function()
            CameraManager.StartCameraShakeAsset(
                EndingManager.STAGGER_SHAKE_ASSET,
                EndingManager.STAGGER_SHAKE_SCALE
            )
        end)
        return
    end

    if CameraManager.StartWaveShake ~= nil then
        pcall(function()
            CameraManager.StartWaveShake(EndingManager.STAGGER_SHAKE_SCALE)
        end)
    end
end

local function stop_ending_sequence_coroutine()
    if EndingManager.SequenceCoroutine ~= nil and StopCoroutine ~= nil then
        StopCoroutine(EndingManager.SequenceCoroutine)
    end
    EndingManager.SequenceCoroutine = nil
end

local function stop_ending_monkey_sequence_coroutine()
    if EndingManager.MonkeySequenceCoroutine ~= nil and StopCoroutine ~= nil then
        StopCoroutine(EndingManager.MonkeySequenceCoroutine)
    end
    EndingManager.MonkeySequenceCoroutine = nil
end

local start_ending_monkey_cymbal_sequence

local function ease_in_turn(alpha, power)
    alpha = math.max(0.0, math.min(tonumber(alpha) or 0.0, 1.0))
    power = math.max(1.0, tonumber(power) or 1.0)
    return alpha ^ power
end

local function blend_control_yaw(player, fromYaw, toYaw, duration)
    local steps = math.max(1, math.floor((duration or 0.0) * 60.0))
    local stepDuration = (duration or 0.0) / steps
    local pitch = EndingManager.ENDING_SPAWN_PITCH
    local easePower = EndingManager.FACING_TURN_EASE_POWER or 3.0

    for step = 1, steps do
        if not EndingManager:IsActive() then
            return
        end

        local alpha = ease_in_turn(step / steps, easePower)
        local yaw = fromYaw + (toYaw - fromYaw) * alpha
        set_control_yaw(player, yaw, pitch)

        if Wait ~= nil then
            Wait(stepDuration)
        end
    end
end

local function reset_stagger_shake_state()
    EndingManager.StaggerElapsed = 0.0
    EndingManager.StaggerPhase = 0.0
    EndingManager.StaggerCurrentOffset.Roll = 0.0
    EndingManager.StaggerCurrentOffset.Pitch = 0.0
    EndingManager.StaggerCurrentOffset.LocX = 0.0
    EndingManager.StaggerCurrentOffset.LocY = 0.0
    EndingManager.StaggerCurrentOffset.LocZ = 0.0
end

local function start_ending_sequence_coroutine(player)
    stop_ending_sequence_coroutine()
    stop_camera_shakes()

    if StartCoroutine == nil or Wait == nil then
        print("[EndingManager] Ending sequence skipped: coroutine API unavailable")
        return
    end

    EndingManager.SequenceCoroutine = StartCoroutine(function()
        Wait(EndingManager.STAGGER_DELAY)
        if not EndingManager:IsActive() then
            return
        end

        print(string.format(
            "[EndingManager] Stagger shake start (delay=%.1fs duration=%.1fs turnAt=%.1fs)",
            EndingManager.STAGGER_DELAY,
            EndingManager.STAGGER_SHAKE_DURATION,
            EndingManager.TURN_START_TIME
        ))
        reset_stagger_shake_state()
        EndingManager.bStaggerShakeActive = true
        Wait(EndingManager.STAGGER_SHAKE_DURATION)
        if not EndingManager:IsActive() then
            return
        end

        EndingManager.bStaggerShakeActive = false
        reset_stagger_shake_state()
        stop_camera_shakes()

        local startYaw = EndingManager.SpawnFacingYaw or EndingManager.ENDING_SPAWN_YAW
        local targetYaw = get_opposite_yaw(startYaw)
        print(string.format(
            "[EndingManager] Turning to opposite yaw %.1f -> %.1f over %.1fs (easePower=%.1f)",
            startYaw,
            targetYaw,
            EndingManager.FACING_TURN_BLEND,
            EndingManager.FACING_TURN_EASE_POWER
        ))
        if StartCoroutine ~= nil and Wait ~= nil then
            StartCoroutine(function()
                Wait(0)
                pcall(function()
                    if start_ending_monkey_cymbal_sequence ~= nil then
                        start_ending_monkey_cymbal_sequence()
                    end
                end)
            end)
        end
        blend_control_yaw(player, startYaw, targetYaw, EndingManager.FACING_TURN_BLEND)

        stop_camera_shakes()
        set_control_yaw(player, targetYaw, EndingManager.ENDING_SPAWN_PITCH)
        print(string.format("[EndingManager] Ending sequence complete yaw=%.1f", targetYaw))
    end)
end

local function apply_ending_spawn_facing(player)
    local pawn = get_pawn_from_player(player)
    local currentRotation = Vec3(0.0, 0.0, 0.0)

    if pawn ~= nil and pawn.GetControlRotation ~= nil then
        local ok, rotation = pcall(function()
            return pawn:GetControlRotation()
        end)
        if ok and rotation ~= nil then
            currentRotation = Vec3(rotation.X or 0.0, rotation.Y or 0.0, rotation.Z or 0.0)
        end
    elseif player ~= nil then
        local ok, rotation = pcall(function()
            return player.Rotation
        end)
        if ok and rotation ~= nil then
            currentRotation = Vec3(rotation.X or 0.0, rotation.Y or 0.0, rotation.Z or 0.0)
        end
    end

    -- FVector(Roll, Pitch, Yaw): yaw=-X, pitch=아래 15도(+Pitch), roll은 유지.
    local targetRotation = Vec3(
        currentRotation.X,
        EndingManager.ENDING_SPAWN_PITCH,
        EndingManager.ENDING_SPAWN_YAW
    )

    if pawn ~= nil and pawn.SetControlRotation ~= nil then
        pcall(function()
            pawn:SetControlRotation(targetRotation)
        end)
    end

    if player ~= nil and player.SetRotation ~= nil then
        pcall(function()
            player:SetRotation(targetRotation)
        end)
    end

    EndingManager.SpawnFacingYaw = EndingManager.ENDING_SPAWN_YAW
end

local function find_actors_by_class(className)
    if World == nil or World.FindActorsByClass == nil or className == nil then
        return {}
    end
    local ok, actors = pcall(function()
        return World.FindActorsByClass(className)
    end)
    if ok and type(actors) == "table" then
        return actors
    end
    return {}
end

local function set_component_light_visible(component, bVisible)
    if component == nil then
        return
    end
    pcall(function()
        if component.SetVisible ~= nil then
            component:SetVisible(bVisible)
        end
    end)
    pcall(function()
        if component.PushToScene ~= nil then
            component:PushToScene()
        end
    end)
end

local function set_actor_lights_visible(actor, bVisible)
    if not is_valid_actor(actor) then
        return
    end

    pcall(function()
        actor:SetVisible(bVisible)
    end)

    local ok, components = pcall(function()
        return actor:GetComponents()
    end)
    if ok and type(components) == "table" then
        for _, component in ipairs(components) do
            set_component_light_visible(component, bVisible)
        end
    end
end

local function is_ending_sun_actor(actor)
    if not is_valid_actor(actor) then
        return false
    end
    if actor.GetName ~= nil then
        local ok, name = pcall(function()
            return actor:GetName()
        end)
        if ok and name == EndingManager.ENDING_SUN_NAME then
            return true
        end
    end
    if actor.HasTag ~= nil and actor:HasTag(EndingManager.ENDING_SUN_TAG) then
        return true
    end
    return false
end

function EndingManager:IsActive()
    return self.bActive == true
end

function EndingManager:IsStaggerShakeActive()
    return self.bStaggerShakeActive == true
end

function EndingManager:GetStaggerShakeOffset()
    return self.StaggerCurrentOffset
end

function EndingManager:UpdateStaggerShake(dt)
    if not self:IsStaggerShakeActive() then
        return
    end

    dt = tonumber(dt) or 0.0
    if dt <= 0.0 then
        return
    end

    self.StaggerElapsed = (self.StaggerElapsed or 0.0) + dt

    local cycle = self.STAGGER_SPEED_CYCLE or 2.2
    local wave = (math.sin(self.StaggerElapsed * math.pi / cycle) + 1.0) * 0.5
    local fastMult = self.STAGGER_FAST_MULT or 3.0
    local slowMult = self.STAGGER_SLOW_MULT or 0.4
    local speedMult = slowMult + (fastMult - slowMult) * wave

    self.StaggerPhase = (self.StaggerPhase or 0.0) + dt * (self.STAGGER_BASE_FREQ or 1.1) * speedMult

    local ampBlend = 0.55 + 0.45 * wave
    local rollAmp = (self.STAGGER_ROLL_DEGREES or 2.0) * ampBlend
    local pitchAmp = (self.STAGGER_PITCH_DEGREES or 0.25) * ampBlend
    local locAmp = (self.STAGGER_LOC_Z or 0.01) * ampBlend
    local locXYAmp = (self.STAGGER_LOC_XY or 0.006) * ampBlend

    local fadeIn = 1.0
    local fadeSeconds = tonumber(self.STAGGER_FADE_IN_SECONDS) or 0.0
    if fadeSeconds > 0.0 then
        fadeIn = math.min(1.0, (self.StaggerElapsed or 0.0) / fadeSeconds)
        fadeIn = fadeIn * fadeIn * (3.0 - 2.0 * fadeIn)
    end

    local phase = self.StaggerPhase
    local offset = self.StaggerCurrentOffset
    offset.Roll = math.sin(phase * math.pi * 2.0) * rollAmp * fadeIn
    offset.Pitch = math.sin(phase * math.pi * 2.0 * 0.85 + 0.6) * pitchAmp * fadeIn
    offset.LocX = math.sin(phase * math.pi * 2.0 * 1.15) * locXYAmp * fadeIn
    offset.LocY = math.sin(phase * math.pi * 2.0 * 0.7 + 1.1) * locXYAmp * fadeIn
    offset.LocZ = math.sin(phase * math.pi * 2.0 * 0.55 + 0.3) * locAmp * fadeIn
end

function EndingManager:SetHorrorLightingEnabled(bEnabled)
    for _, className in ipairs(self.HORROR_LIGHT_CLASSES) do
        for _, actor in ipairs(find_actors_by_class(className)) do
            set_actor_lights_visible(actor, bEnabled)
        end
    end

    for _, actor in ipairs(find_actors_by_class("ADirectionalLightActor")) do
        if not is_ending_sun_actor(actor) then
            set_actor_lights_visible(actor, bEnabled)
        end
    end
end

function EndingManager:SetEndingLightingEnabled(bEnabled)
    local endingSun = find_actor_by_name(self.ENDING_SUN_NAME)
        or find_first_actor_by_tag(self.ENDING_SUN_TAG)
    if endingSun ~= nil then
        set_actor_lights_visible(endingSun, bEnabled)
    end
end

function EndingManager:GetSpawnLocation()
    local spawnActor = find_first_actor_by_tag(self.ENDING_SPAWN_TAG)
        or find_actor_by_name("EndingSpawn")
    if spawnActor ~= nil and spawnActor.GetLocation ~= nil then
        local ok, location = pcall(function()
            return spawnActor:GetLocation()
        end)
        if ok and location ~= nil then
            return Vec3(location.X or 0.0, location.Y or 0.0, location.Z or 0.0)
        end
    end
    return self.FALLBACK_SPAWN
end

function EndingManager:Initialize()
    self.bActive = false
    self:SetEndingLightingEnabled(false)
end

local function destroy_ending_victim_actor(actor)
    if not is_valid_actor(actor) or actor.Destroy == nil then
        return
    end
    pcall(function()
        actor:Destroy()
    end)
end

local function get_skeletal_mesh_from_actor(actor)
    if not is_valid_actor(actor) or actor.GetSkeletalMeshComponent == nil then
        return nil
    end
    local ok, mesh = pcall(function()
        return actor:GetSkeletalMeshComponent()
    end)
    if ok then
        return mesh
    end
    return nil
end

function EndingManager:DespawnVictim()
    destroy_ending_victim_actor(self.VictimActor)
    self.VictimActor = nil
end

function EndingManager:DespawnMonkey()
    stop_ending_monkey_sequence_coroutine()
    destroy_ending_victim_actor(self.MonkeyActor)
    self.MonkeyActor = nil
end

local function configure_monkey_audio(actor)
    if not is_valid_actor(actor) or actor.GetAudioComponent == nil then
        return false
    end

    local ok, audio = pcall(function()
        return actor:GetAudioComponent()
    end)
    if not ok or audio == nil or audio.SetVolume == nil then
        return false
    end

    ok = pcall(function()
        audio:SetVolume(EndingManager.MONKEY_AUDIO_VOLUME)
    end)
    return ok == true
end

local function play_monkey_animation(mesh, animationPath, looping, playRate)
    if mesh == nil or mesh.PlayAnimationByPath == nil then
        return false
    end

    local playOk, playResult = pcall(function()
        return mesh:PlayAnimationByPath(animationPath, looping)
    end)
    if not playOk or playResult == false then
        return false
    end

    if mesh.SetPlayRate ~= nil then
        pcall(function()
            mesh:SetPlayRate(playRate or 1.0)
        end)
    end

    return true
end

local function set_monkey_entry_play_rate_for_duration(mesh, duration)
    if mesh == nil then
        return 0.1
    end

    local length = 0.0
    if mesh.GetCurrentAnimationLength ~= nil then
        local ok, value = pcall(function()
            return mesh:GetCurrentAnimationLength()
        end)
        if ok and value ~= nil then
            length = tonumber(value) or 0.0
        end
    end

    local safeDuration = math.max(tonumber(duration) or 0.0, 0.001)
    local playRate = 0.1
    if length > 0.0 then
        playRate = length / safeDuration
    end

    if mesh.SetPlayRate ~= nil then
        pcall(function()
            mesh:SetPlayRate(playRate)
        end)
    end

    return playRate
end

local function stop_ending_camera_fade()
    if CameraManager == nil then
        return
    end

    if CameraManager.StopCameraFade ~= nil then
        pcall(function()
            CameraManager.StopCameraFade()
        end)
        return
    end

    if CameraManager.FadeIn ~= nil then
        pcall(function()
            CameraManager.FadeIn(0.01)
        end)
    end
end

local function play_ending_credits_music()
    if SoundManager == nil or SoundManager.PlayTitleMusic == nil then
        EndingManager.bEndingCreditsMusicActive = false
        return false
    end

    local playFn = SoundManager.PlayTitleMusicIfNeeded or SoundManager.PlayTitleMusic
    local bStarted = playFn(SoundManager) == true
    EndingManager.bEndingCreditsMusicActive = bStarted
    return bStarted
end

local function stop_ending_credits_music()
    if not EndingManager.bEndingCreditsMusicActive then
        return
    end

    EndingManager.bEndingCreditsMusicActive = false
    if SoundManager == nil or SoundManager.StopTitleMusic == nil then
        return
    end

    SoundManager:StopTitleMusic()
end

local function show_ending_credits()
    if UIManager == nil or UIManager.ShowEndingCredits == nil then
        return false
    end

    local bShown = UIManager:ShowEndingCredits() == true
    if bShown then
        play_ending_credits_music()
    end
    return bShown
end

local function hide_ending_credits()
    if UIManager == nil or UIManager.HideEndingCredits == nil then
        return
    end

    UIManager:HideEndingCredits()
end

local function hide_ending_credits_and_music()
    hide_ending_credits()
    stop_ending_credits_music()
end

local function hide_ending_name_input()
    EndingManager.bNameInputActive = false
    EndingManager.PlayerNameBuffer = ""
    if UIManager ~= nil and UIManager.HideEndingNameInput ~= nil then
        UIManager:HideEndingNameInput()
    end
end

local function get_name_input_display_text(buffer)
    buffer = tostring(buffer or "")
    if buffer == "" then
        return "_"
    end
    return buffer
end

local function refresh_ending_name_input_display()
    if UIManager == nil or UIManager.SetEndingNameInputText == nil then
        return
    end
    UIManager:SetEndingNameInputText(get_name_input_display_text(EndingManager.PlayerNameBuffer))
end

local function ensure_ending_black_screen()
    if CameraManager == nil or CameraManager.FadeOut == nil then
        return
    end

    pcall(function()
        CameraManager.FadeOut(0.0)
    end)
end

local function show_ending_name_input()
    EndingManager.PlayerNameBuffer = ""
    EndingManager.bNameInputActive = true
    if UIManager == nil or UIManager.ShowEndingNameInput == nil then
        print("[EndingManager] Name input failed: UIManager.ShowEndingNameInput unavailable")
        EndingManager.bNameInputActive = false
        return false
    end

    ensure_ending_black_screen()
    local bShown = UIManager:ShowEndingNameInput(get_name_input_display_text("")) == true
    if not bShown then
        print("[EndingManager] Name input failed: widget could not be shown")
        EndingManager.bNameInputActive = false
    else
        print("[EndingManager] Name input shown")
    end
    return bShown
end

local KEY_BACKSPACE = 0x08
local KEY_RETURN = 0x0D
local KEY_SPACE = 0x20

local function normalize_player_name(rawName)
    rawName = tostring(rawName or "")
    rawName = string.gsub(rawName, "^%s+", "")
    rawName = string.gsub(rawName, "%s+$", "")
    rawName = string.gsub(rawName, "%s+", " ")
    if rawName == "" then
        return EndingManager.DEFAULT_PLAYER_NAME
    end
    if #rawName > EndingManager.MAX_PLAYER_NAME_LENGTH then
        rawName = string.sub(rawName, 1, EndingManager.MAX_PLAYER_NAME_LENGTH)
    end
    return rawName
end

local function capture_pending_leaderboard_record()
    local createdAtSeconds = 0
    if World ~= nil and World.GetRealTimeSeconds ~= nil then
        createdAtSeconds = tonumber(World.GetRealTimeSeconds()) or 0
    end

    EndingManager.PendingLeaderboardRecord = {
        TotalTimeSeconds = GameManager:GetTotalGameTime(),
        ElapsedTimeSeconds = GameManager:GetElapsedTime(),
        Score = GameManager:GetScore(),
        ClearReason = "Ending",
        CreatedAtSeconds = createdAtSeconds,
    }
end

local function submit_pending_leaderboard_record(playerName)
    local record = EndingManager.PendingLeaderboardRecord
    if record == nil then
        capture_pending_leaderboard_record()
        record = EndingManager.PendingLeaderboardRecord
    end
    if record == nil then
        return nil
    end

    return LeaderboardManager:AddClearRecord({
        TotalTimeSeconds = record.TotalTimeSeconds,
        ElapsedTimeSeconds = record.ElapsedTimeSeconds,
        Score = record.Score,
        ClearReason = record.ClearReason,
        CreatedAtSeconds = record.CreatedAtSeconds,
        PlayerName = normalize_player_name(playerName),
    })
end

function EndingManager:RegisterReturnToTitleCallback(callback)
    self.ReturnToTitleCallback = callback
end

local function return_to_title_from_ending()
    stop_ending_camera_fade()
    EndingManager.bPreserveCreditsMusicOnReset = true
    if EndingManager.bEndingCreditsMusicActive == true and SoundManager ~= nil then
        if SoundManager.SetPreserveBgmOnReset ~= nil then
            SoundManager:SetPreserveBgmOnReset(true)
        end
        if SoundManager.SetContinueEndingCreditsBgm ~= nil then
            SoundManager:SetContinueEndingCreditsBgm(true)
        end
    end

    local returnCallback = EndingManager.ReturnToTitleCallback
    if type(returnCallback) == "function" then
        local ok, err = pcall(returnCallback)
        if not ok then
            print("[EndingManager] ReturnToTitleCallback failed: " .. tostring(err))
        end
        return
    end

    print("[EndingManager] ReturnToTitleCallback is not registered; title transition skipped.")
end

local function transition_ending_credits_to_name_input()
    if not EndingManager:IsActive() then
        return false
    end

    hide_ending_credits()
    if UIManager ~= nil and UIManager.ExitCutsceneMode ~= nil then
        UIManager:ExitCutsceneMode()
    end
    ensure_ending_black_screen()
    return show_ending_name_input() == true
end

local function process_name_input_key(keyCode)
    if keyCode == KEY_BACKSPACE then
        local buffer = EndingManager.PlayerNameBuffer or ""
        if #buffer > 0 then
            EndingManager.PlayerNameBuffer = string.sub(buffer, 1, #buffer - 1)
            refresh_ending_name_input_display()
        end
        return true
    end

    if keyCode == KEY_RETURN then
        EndingManager:SubmitPlayerName()
        return true
    end

    if keyCode == KEY_SPACE then
        local buffer = EndingManager.PlayerNameBuffer or ""
        if #buffer >= EndingManager.MAX_PLAYER_NAME_LENGTH then
            return true
        end
        if buffer ~= "" and string.sub(buffer, -1) ~= " " then
            EndingManager.PlayerNameBuffer = buffer .. " "
            refresh_ending_name_input_display()
        end
        return true
    end

    if keyCode >= 0x30 and keyCode <= 0x39 then
        local buffer = EndingManager.PlayerNameBuffer or ""
        if #buffer >= EndingManager.MAX_PLAYER_NAME_LENGTH then
            return true
        end
        EndingManager.PlayerNameBuffer = buffer .. string.char(keyCode)
        refresh_ending_name_input_display()
        return true
    end

    if keyCode >= 0x41 and keyCode <= 0x5A then
        local buffer = EndingManager.PlayerNameBuffer or ""
        if #buffer >= EndingManager.MAX_PLAYER_NAME_LENGTH then
            return true
        end
        EndingManager.PlayerNameBuffer = buffer .. string.char(keyCode)
        refresh_ending_name_input_display()
        return true
    end

    if keyCode >= 0x61 and keyCode <= 0x7A then
        local buffer = EndingManager.PlayerNameBuffer or ""
        if #buffer >= EndingManager.MAX_PLAYER_NAME_LENGTH then
            return true
        end
        EndingManager.PlayerNameBuffer = buffer .. string.char(keyCode - 0x20)
        refresh_ending_name_input_display()
        return true
    end

    return false
end

local ENDING_NAME_INPUT_KEYS = {
    KEY_BACKSPACE,
    KEY_RETURN,
    KEY_SPACE,
}

for keyCode = 0x30, 0x39 do
    ENDING_NAME_INPUT_KEYS[#ENDING_NAME_INPUT_KEYS + 1] = keyCode
end
for keyCode = 0x41, 0x5A do
    ENDING_NAME_INPUT_KEYS[#ENDING_NAME_INPUT_KEYS + 1] = keyCode
end
for keyCode = 0x61, 0x7A do
    ENDING_NAME_INPUT_KEYS[#ENDING_NAME_INPUT_KEYS + 1] = keyCode
end

local function is_ending_name_input_key_down(keyCode)
    if Input == nil then
        return false
    end

    if Input.GetRawKeyDown ~= nil then
        local ok, pressed = pcall(function()
            return Input.GetRawKeyDown(keyCode)
        end)
        return ok and pressed == true
    end

    if Input.GetKeyDown ~= nil then
        local ok, pressed = pcall(function()
            return Input.GetKeyDown(keyCode)
        end)
        return ok and pressed == true
    end

    return false
end

local function play_ending_strike_blackout()
    ensure_ending_black_screen()

    if Wait ~= nil then
        Wait(EndingManager.MONKEY_STRIKE_BLACKOUT_HOLD_SECONDS)
    end
    if not EndingManager:IsActive() then
        return
    end

    if show_ending_credits() ~= true then
        print("[EndingManager] Ending credits could not be shown")
        return
    end

    print(string.format(
        "[EndingManager] Ending credits shown for %.1fs",
        EndingManager.ENDING_CREDIT_DISPLAY_SECONDS
    ))

    if Wait ~= nil then
        Wait(EndingManager.ENDING_CREDIT_DISPLAY_SECONDS)
    end

    if not transition_ending_credits_to_name_input() then
        print("[EndingManager] Ending credits to name input transition failed")
    end
end

start_ending_monkey_cymbal_sequence = function()
    stop_ending_monkey_sequence_coroutine()
    configure_monkey_audio(EndingManager.MonkeyActor)

    if StartCoroutine == nil or Wait == nil then
        print("[EndingManager] Ending monkey sequence skipped: coroutine API unavailable")
        return
    end

    EndingManager.MonkeySequenceCoroutine = StartCoroutine(function()
        local mesh = get_skeletal_mesh_from_actor(EndingManager.MonkeyActor)
        if mesh == nil then
            print("[EndingManager] Ending monkey sequence skipped: skeletal mesh unavailable")
            return
        end

        if not play_monkey_animation(
            mesh,
            EndingManager.MONKEY_ENTRY_ANIMATION_PATH,
            false,
            1.0
        ) then
            print("[EndingManager] Ending monkey entry animation failed")
            return
        end

        local entryPlayRate = set_monkey_entry_play_rate_for_duration(
            mesh,
            EndingManager.MONKEY_ENTRY_TO_STRIKE_SECONDS
        )
        print(string.format(
            "[EndingManager] Ending monkey entry started (duration=%.1fs playRate=%.3f)",
            EndingManager.MONKEY_ENTRY_TO_STRIKE_SECONDS,
            entryPlayRate
        ))

        Wait(EndingManager.MONKEY_ENTRY_TO_STRIKE_SECONDS)
        if not EndingManager:IsActive() then
            return
        end

        if not play_monkey_animation(
            mesh,
            EndingManager.MONKEY_STRIKE_ANIMATION_PATH,
            false,
            EndingManager.MONKEY_STRIKE_PLAY_RATE
        ) then
            print("[EndingManager] Ending monkey strike animation failed")
            return
        end

        print(string.format(
            "[EndingManager] Ending monkey strike started (playRate=%.2f)",
            EndingManager.MONKEY_STRIKE_PLAY_RATE
        ))

        if EndingManager:IsActive() then
            play_ending_strike_blackout()
        end
    end)
end

function EndingManager:SpawnMonkey()
    self:DespawnMonkey()

    if World == nil or World.SpawnActorTemplate == nil then
        print("[EndingManager] SpawnMonkey failed: World.SpawnActorTemplate unavailable")
        return false
    end

    local ok, actor = pcall(function()
        return World.SpawnActorTemplate(
            self.MONKEY_ACTOR_TEMPLATE,
            self.MONKEY_LOCATION,
            self.MONKEY_ROTATION,
            self.MONKEY_SCALE
        )
    end)
    if not ok or not is_valid_actor(actor) then
        print("[EndingManager] SpawnMonkey failed: actor template spawn error")
        return false
    end

    self.MonkeyActor = actor
    configure_monkey_audio(actor)

    print(string.format(
        "[EndingManager] Monkey spawned at (%.2f, %.2f, %.2f) yaw=%.1f volume=%.1f",
        self.MONKEY_LOCATION.X,
        self.MONKEY_LOCATION.Y,
        self.MONKEY_LOCATION.Z,
        self.MONKEY_ROTATION.Z or 0.0,
        self.MONKEY_AUDIO_VOLUME
    ))
    return true
end

function EndingManager:SpawnVictim()
    self:DespawnVictim()

    if World == nil or World.SpawnActorTemplate == nil then
        print("[EndingManager] SpawnVictim failed: World.SpawnActorTemplate unavailable")
        return false
    end

    local ok, actor = pcall(function()
        return World.SpawnActorTemplate(
            self.VICTIM_ACTOR_TEMPLATE,
            self.VICTIM_LOCATION,
            self.VICTIM_ROTATION,
            self.VICTIM_SCALE
        )
    end)
    if not ok or not is_valid_actor(actor) then
        print("[EndingManager] SpawnVictim failed: actor template spawn error")
        return false
    end

    self.VictimActor = actor

    local mesh = get_skeletal_mesh_from_actor(actor)
    if mesh == nil or mesh.PlayAnimationByPath == nil then
        print("[EndingManager] SpawnVictim failed: skeletal mesh component unavailable")
        return false
    end

    local playOk, playResult = pcall(function()
        return mesh:PlayAnimationByPath(self.VICTIM_ANIMATION_PATH, self.VICTIM_ANIMATION_LOOPING)
    end)
    if not playOk or playResult == false then
        print("[EndingManager] SpawnVictim failed: PlayAnimationByPath returned false")
        return false
    end

    if mesh.SetPlayRate ~= nil then
        pcall(function()
            mesh:SetPlayRate(self.VICTIM_ANIMATION_PLAY_RATE)
        end)
    end

    print(string.format(
        "[EndingManager] Victim spawned at (%.2f, %.2f, %.2f) anim=%s",
        self.VICTIM_LOCATION.X,
        self.VICTIM_LOCATION.Y,
        self.VICTIM_LOCATION.Z,
        self.VICTIM_ANIMATION_PATH
    ))
    return true
end

function EndingManager:IsNameInputActive()
    return self.bNameInputActive == true
end

function EndingManager:ShouldProcessEndingTick()
    return self:IsActive() or self:IsNameInputActive()
end

function EndingManager:Tick(dt)
    if not self:IsNameInputActive() then
        return
    end

    for _, keyCode in ipairs(ENDING_NAME_INPUT_KEYS) do
        if is_ending_name_input_key_down(keyCode) and process_name_input_key(keyCode) then
            break
        end
    end
end

function EndingManager:SubmitPlayerName(playerName)
    if self.bSubmittingToTitle == true then
        return false
    end
    if not self.bActive and not self:IsNameInputActive() then
        return false
    end

    self.bSubmittingToTitle = true

    local resolvedName = playerName
    if resolvedName == nil then
        resolvedName = self.PlayerNameBuffer
    end

    self.bNameInputActive = false
    hide_ending_name_input()

    submit_pending_leaderboard_record(resolvedName)
    return_to_title_from_ending()
    return true
end

function EndingManager:Reset(bStopCreditsMusic)
    stop_ending_sequence_coroutine()
    stop_ending_camera_fade()

    local bShouldStopMusic = bStopCreditsMusic
    if bShouldStopMusic == nil then
        bShouldStopMusic = self.bPreserveCreditsMusicOnReset ~= true
    end
    self.bPreserveCreditsMusicOnReset = false

    if bShouldStopMusic then
        hide_ending_credits_and_music()
    else
        hide_ending_credits()
        hide_ending_name_input()
    end

    stop_camera_shakes()
    self.bStaggerShakeActive = false
    reset_stagger_shake_state()
    self.bActive = false
    self.bSubmittingToTitle = false
    self.PendingLeaderboardRecord = nil
    self:DespawnVictim()
    self:DespawnMonkey()
    UIManager:ExitCutsceneMode()
    self:SetEndingLightingEnabled(false)
    self:SetHorrorLightingEnabled(true)
    self.SpawnFacingYaw = self.ENDING_SPAWN_YAW
end

local function play_wake_up_pistol_audio()
    if Audio == nil or Audio.Play == nil then
        return
    end

    pcall(function()
        Audio.Play("PistolFire", 1.0)
    end)
end

local function play_ending_siren_audio()
    if Audio == nil or Audio.Play == nil then
        return
    end

    pcall(function()
        Audio.Play(EndingManager.ENDING_SIREN_KEY, EndingManager.ENDING_SIREN_VOLUME)
    end)
end

local function get_wake_up_shot_hit(player)
    if player == nil or player.GetCamera == nil or World == nil or World.LineTraceObjects == nil then
        return nil
    end

    local camera = player:GetCamera()
    if camera == nil or camera.GetLocation == nil then
        return nil
    end

    local okStart, start = pcall(function()
        return camera:GetLocation()
    end)
    if not okStart or start == nil then
        return nil
    end

    local direction = camera.Forward
    if direction == nil then
        return nil
    end

    local target = start + direction * EndingManager.WAKE_UP_SHOT_TRACE_DISTANCE
    local okHit, traceHit = pcall(function()
        return World.LineTraceObjects(start, target, player)
    end)
    if okHit and traceHit ~= nil and traceHit.Hit then
        return traceHit
    end

    return nil
end

function EndingManager:PlayWakeUpShot(player)
    if player == nil then
        player = get_player_pawn()
    end
    if player == nil then
        return false
    end

    if HospitalPlayer ~= nil and HospitalPlayer.play_pistol_fire_effect ~= nil then
        pcall(function()
            HospitalPlayer.play_pistol_fire_effect(player)
        end)
    end

    play_wake_up_pistol_audio()

    local endingHit = get_wake_up_shot_hit(player)
    if endingHit ~= nil and GameManager._PlayAnomalyHitEffect ~= nil then
        GameManager:_PlayAnomalyHitEffect(player, endingHit)
    end

    return true
end

function EndingManager:Enter(player, hit)
    if self.bActive then
        return false
    end
    if player == nil then
        player = get_player_pawn()
    end
    if player == nil or player.SetLocation == nil then
        print("[EndingManager] Enter failed: player unavailable")
        return false
    end

    self.bActive = true

    GameManager:ClearActiveAnomalyOutline()
    require("AnomalyManager"):Reset()
    GameManager:_ClearAnomalyPlacement()
    GameManager.LastAnomalyPlacementError = nil
    require("JumpScareManager"):DeactivateAll()
    require("DoorManager"):ClearToyProjectiles()
    GameManager:_ResetFailureTimeDrain()

    LoopManager:Reset()
    GameManager.bLoopStopped = LoopManager:IsLoopStopped()
    GameManager.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    GameManager:_SetPressureStage(GameManager.Pressure.EntryStrike, "EndingEnter", false)

    local spawnLocation = self:GetSpawnLocation()
    pcall(function()
        player:SetLocation(spawnLocation)
    end)
    apply_ending_spawn_facing(player)
    play_ending_siren_audio()
    UIManager:EnterCutsceneMode()

    self:SetHorrorLightingEnabled(false)
    self:SetEndingLightingEnabled(true)
    self:SpawnVictim()
    self:SpawnMonkey()

    if GameManager._SetState ~= nil then
        GameManager:_SetState(GameManager.State.Ending, "FinalAnomalyShot")
    end

    capture_pending_leaderboard_record()
    self:PlayWakeUpShot(player)
    start_ending_sequence_coroutine(player)

    print(string.format(
        "[EndingManager] Entered ending at (%.2f, %.2f, %.2f) stage=%d",
        spawnLocation.X,
        spawnLocation.Y,
        spawnLocation.Z,
        StageManager:GetStage()
    ))
    return true
end

return EndingManager
