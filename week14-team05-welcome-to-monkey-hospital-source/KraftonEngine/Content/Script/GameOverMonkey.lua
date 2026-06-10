local GameOverMonkey = {}

local COMPONENT_NAME = "GameOverMonkey"
local CYMBALS_MONKEY_TAG = "CymbalsMonkey"
local POST_PROCESS_MATERIAL_PATH = "Content/Material/PostProcess/HorrorPostProcess.uasset"
local ANIMATION_PATH = "Content/Data/CymbalMonkey/CymbalMonkey_Joints_Warning.uasset"
local ANIMATION_LOOPING = false
local ANIMATION_PLAY_RATE = 1.5
local NOISE_AUDIO_KEY = "GameOverNoise"
local NOISE_AUDIO_LOOP_NAME = "GameOverNoiseLoop"
local NOISE_AUDIO_PATH = "SFX/Noise.mp3"
local NOISE_AUDIO_VOLUME = 0.1
local SCREAM_AUDIO_KEY = "GameOverMonkeyScream"
local SCREAM_AUDIO_PATH = "CymbalMonkey/MonkeyScream.mp3"
local SCREAM_AUDIO_VOLUME = 0.3
local CYMBALS_MONKEY_RISE_TARGET_SCALE = 0.1

local LOOK_AT_SECONDS = 0.1
local REVEAL_DELAY_SECONDS = 0.3
local RISE_SECONDS = 0.15
local RED_VIGNETTE_SECONDS = 2.0
local SQUEEZE_CYCLE_SECONDS = 0.08
local SQUEEZE_MAX_SCALE = 1.5
local unpack_args = table.unpack or unpack
local POST_PROCESS_SCALAR_PARAMETERS = {
    "VignetteIntensity",
    "VignetteRadius",
    "VignetteSoftness",
    "ChromaticStrength",
    "Time",
    "GrainStrength",
    "GrainScale",
    "GrainDarkPower",
    "NoiseMin",
    "NoiseMax",
}
local POST_PROCESS_VECTOR_PARAMETERS = {
    "VignetteColor",
    "NoiseColor",
}

local STATE_NONE = "None"
local STATE_LOOK_AT = "LookAt"
local STATE_REVEAL_DELAY = "RevealDelay"
local STATE_RISE = "Rise"
local STATE_RED_VIGNETTE = "RedVignette"
local STATE_FINISHED = "Finished"

GameOverMonkey.PlayerActor = nil
GameOverMonkey.Mesh = nil
GameOverMonkey.State = STATE_NONE
GameOverMonkey.StateElapsed = 0.0
GameOverMonkey.PresentationElapsed = 0.0
GameOverMonkey.OnFinished = nil
GameOverMonkey.ActiveCamera = nil
GameOverMonkey.PlayerPawn = nil
GameOverMonkey.SavedControlRotation = nil
GameOverMonkey.LookStartControlRotation = nil
GameOverMonkey.LookTargetControlRotation = nil
GameOverMonkey.SqueezeElapsed = 0.0
GameOverMonkey.OriginalScale = nil
GameOverMonkey.OriginalLocalLocation = nil
GameOverMonkey.StartLocalLocation = nil
GameOverMonkey.CymbalsMonkeyActor = nil
GameOverMonkey.CymbalsMonkeyOriginalLocation = nil
GameOverMonkey.CymbalsMonkeyOriginalScale = nil
GameOverMonkey.CymbalsMonkeyStartLocation = nil
GameOverMonkey.CymbalsMonkeyTargetLocation = nil
GameOverMonkey.CymbalsMonkeyStartScale = nil
GameOverMonkey.CymbalsMonkeyTargetScale = nil
GameOverMonkey.SavedPostProcessParameters = nil
GameOverMonkey.bNoiseAudioLoaded = false
GameOverMonkey.bNoiseAudioPlaying = false
GameOverMonkey.bScreamAudioLoaded = false

local function log_failure(message)
    print("[GameOverMonkey] " .. tostring(message))
end

local function clamp01(value)
    value = tonumber(value) or 0.0
    if value < 0.0 then
        return 0.0
    end
    if value > 1.0 then
        return 1.0
    end
    return value
end

local function smooth_step(value)
    value = clamp01(value)
    return value * value * (3.0 - 2.0 * value)
end

local function lerp(a, b, alpha)
    return (tonumber(a) or 0.0) + ((tonumber(b) or 0.0) - (tonumber(a) or 0.0)) * alpha
end

local function angle_delta(from, to)
    local delta = ((to - from + 180.0) % 360.0) - 180.0
    return delta
end

local function lerp_angle(from, to, alpha)
    return from + angle_delta(from, to) * alpha
end

local function copy_vec3(value)
    if value == nil then
        return nil
    end

    return Vec3(value.X or 0.0, value.Y or 0.0, value.Z or 0.0)
end

local function make_vec4(x, y, z, w)
    return { X = x, Y = y, Z = z, W = w }
end

local function get_vec4_component(value, componentName, fallback)
    if value ~= nil and value[componentName] ~= nil then
        return value[componentName]
    end
    return fallback
end

local function lerp_vec4(from, to, alpha)
    return make_vec4(
        lerp(get_vec4_component(from, "X", 0.0), get_vec4_component(to, "X", 0.0), alpha),
        lerp(get_vec4_component(from, "Y", 0.0), get_vec4_component(to, "Y", 0.0), alpha),
        lerp(get_vec4_component(from, "Z", 0.0), get_vec4_component(to, "Z", 0.0), alpha),
        lerp(get_vec4_component(from, "W", 0.0), get_vec4_component(to, "W", 0.0), alpha)
    )
end

local function call_object_function(object, functionName, ...)
    if object == nil or object.CallFunction == nil then
        return false, nil
    end

    local args = { ... }
    local ok, result = pcall(function()
        return object:CallFunction(functionName, unpack_args(args))
    end)
    if not ok then
        return false, result
    end
    return true, result
end

local function atan2(y, x)
    if math.atan2 ~= nil then
        return math.atan2(y, x)
    end
    return math.atan(y, x)
end

local function get_location(object)
    if object == nil or object.GetLocation == nil then
        return nil
    end

    local ok, location = pcall(function()
        return object:GetLocation()
    end)
    if ok then
        return location
    end
    return nil
end

local function get_player_pawn(playerActor)
    if playerActor == nil or playerActor.AsPawn == nil then
        return nil
    end

    local ok, pawn = pcall(function()
        return playerActor:AsPawn()
    end)
    if ok then
        return pawn
    end
    return nil
end

local function get_actor_scale(actor)
    if actor == nil then
        return nil
    end

    local ok, scale = pcall(function()
        return actor.Scale
    end)
    if ok then
        return scale
    end
    return nil
end

local function set_actor_location(actor, location)
    if actor == nil or location == nil then
        return false
    end

    if actor.SetLocation ~= nil then
        local ok = pcall(function()
            actor:SetLocation(location)
        end)
        if ok then
            return true
        end
    end

    local ok = pcall(function()
        actor.Location = location
    end)
    return ok == true
end

local function set_actor_scale(actor, scale)
    if actor == nil or scale == nil then
        return false
    end

    local ok = pcall(function()
        actor.Scale = scale
    end)
    return ok == true
end

local function get_control_rotation(pawn)
    if pawn == nil or pawn.GetControlRotation == nil then
        return nil
    end

    local ok, rotation = pcall(function()
        return pawn:GetControlRotation()
    end)
    if ok then
        return rotation
    end
    return nil
end

local function set_control_rotation(pawn, rotation)
    if pawn == nil or pawn.SetControlRotation == nil or rotation == nil then
        return false
    end

    local ok = pcall(function()
        pawn:SetControlRotation(rotation)
    end)
    return ok == true
end

local function get_active_camera(playerActor)
    if CameraManager ~= nil and CameraManager.GetActiveCamera ~= nil then
        local ok, camera = pcall(function()
            return CameraManager.GetActiveCamera()
        end)
        if ok and camera ~= nil then
            return camera
        end
    end

    if playerActor ~= nil and playerActor.GetCamera ~= nil then
        local ok, camera = pcall(function()
            return playerActor:GetCamera()
        end)
        if ok then
            return camera
        end
    end

    return nil
end

local function find_first_actor_by_tag(tag)
    if World == nil then
        return nil
    end

    if World.FindFirstActorByTag ~= nil then
        local ok, actor = pcall(function()
            return World.FindFirstActorByTag(tag)
        end)
        if ok and actor ~= nil then
            return actor
        end
    end

    if World.FindActorsByTag ~= nil then
        local ok, actors = pcall(function()
            return World.FindActorsByTag(tag)
        end)
        if ok and actors ~= nil and #actors > 0 then
            return actors[1]
        end
    end

    return nil
end

local function calculate_look_rotation(cameraLocation, targetLocation, baseRotation)
    if cameraLocation == nil or targetLocation == nil or baseRotation == nil then
        return nil
    end

    local dx = (targetLocation.X or 0.0) - (cameraLocation.X or 0.0)
    local dy = (targetLocation.Y or 0.0) - (cameraLocation.Y or 0.0)
    local dz = (targetLocation.Z or 0.0) - (cameraLocation.Z or 0.0)
    local length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length <= 0.0001 then
        return nil
    end

    local invLength = 1.0 / length
    local z = dz * invLength
    if z > 1.0 then
        z = 1.0
    elseif z < -1.0 then
        z = -1.0
    end

    local radToDeg = 180.0 / math.pi
    local pitch = -math.asin(z) * radToDeg
    local yaw = atan2(dy, dx) * radToDeg

    return Vec3(baseRotation.X or 0.0, pitch, yaw)
end

local function lerp_vec3(from, to, alpha)
    if from == nil or to == nil then
        return nil
    end

    return Vec3(
        lerp(from.X or 0.0, to.X or 0.0, alpha),
        lerp(from.Y or 0.0, to.Y or 0.0, alpha),
        lerp(from.Z or 0.0, to.Z or 0.0, alpha)
    )
end

local function multiply_vec3(value, scale)
    if value == nil then
        return nil
    end

    return Vec3(
        (value.X or 0.0) * scale,
        (value.Y or 0.0) * scale,
        (value.Z or 0.0) * scale
    )
end

local function get_relative_location(component)
    if component == nil then
        return nil
    end

    local ok, location = pcall(function()
        return component.RelativeLocation
    end)
    if ok then
        return copy_vec3(location)
    end
    return nil
end

local function set_relative_location(component, location)
    if component == nil or location == nil then
        return false
    end

    local ok = pcall(function()
        component.RelativeLocation = location
    end)
    return ok == true
end

local function set_post_process_scalar(camera, name, value)
    if camera == nil or camera.SetPostProcessScalarParameter == nil then
        return false
    end

    local ok, result = pcall(function()
        return camera:SetPostProcessScalarParameter(name, value)
    end)
    return ok and result ~= false
end

local function set_post_process_vector(camera, name, value)
    if camera == nil or camera.SetPostProcessVectorParameter == nil then
        return false
    end

    local ok, result = pcall(function()
        return camera:SetPostProcessVectorParameter(name, value)
    end)
    return ok and result ~= false
end

local function get_post_process_material(camera)
    if camera == nil or camera.GetPostProcessMaterial == nil then
        return nil
    end

    local ok, material = pcall(function()
        return camera:GetPostProcessMaterial()
    end)
    if ok then
        return material
    end
    return nil
end

local function ensure_horror_post_process(camera)
    if camera == nil or camera.SetPostProcessMaterial == nil then
        return false
    end

    if get_post_process_material(camera) ~= nil then
        return true
    end

    local ok, result = pcall(function()
        return camera:SetPostProcessMaterial(POST_PROCESS_MATERIAL_PATH)
    end)
    return ok and result ~= false
end

function GameOverMonkey:GetMesh()
    if self.Mesh ~= nil then
        return self.Mesh
    end

    local actor = self.PlayerActor
    if actor == nil then
        log_failure("player actor is nil")
        return nil
    end
    if actor.GetSkeletalMeshComponentByName == nil then
        log_failure("player actor has no GetSkeletalMeshComponentByName")
        return nil
    end

    local ok, componentOrError = pcall(function()
        return actor:GetSkeletalMeshComponentByName(COMPONENT_NAME)
    end)
    if not ok then
        log_failure("GetSkeletalMeshComponentByName failed: " .. tostring(componentOrError))
        return nil
    end

    if componentOrError == nil then
        log_failure("skeletal mesh component not found: " .. COMPONENT_NAME)
        return nil
    end

    self.Mesh = componentOrError
    return self.Mesh
end

function GameOverMonkey:SetVisible(visible)
    local mesh = self:GetMesh()
    if mesh == nil then
        log_failure("SetVisible failed: mesh is nil")
        return false
    end

    if mesh.SetVisibility == nil then
        log_failure("SetVisibility unavailable")
        return false
    end

    local ok, err = pcall(function()
        mesh:SetVisibility(visible == true)
    end)
    if not ok then
        log_failure("SetVisibility failed: " .. tostring(err))
        return false
    end

    return true
end

function GameOverMonkey:GetMeshScale()
    local mesh = self:GetMesh()
    if mesh == nil then
        return nil
    end

    local ok, scale = call_object_function(mesh, "GetRelativeScale")
    if ok and scale ~= nil then
        return copy_vec3(scale)
    end

    return nil
end

function GameOverMonkey:GetMeshLocalLocation()
    local mesh = self:GetMesh()
    if mesh == nil then
        return nil
    end

    return get_relative_location(mesh)
end

function GameOverMonkey:SetMeshLocalLocation(location)
    local mesh = self:GetMesh()
    if mesh == nil or location == nil then
        return false
    end

    if set_relative_location(mesh, location) then
        return true
    end

    log_failure("RelativeLocation unavailable")
    return false
end

function GameOverMonkey:SetMeshScale(scale)
    local mesh = self:GetMesh()
    if mesh == nil or scale == nil then
        return false
    end

    local ok = call_object_function(mesh, "SetRelativeScale", scale)
    if ok then
        return true
    end

    log_failure("SetRelativeScale unavailable")
    return false
end

function GameOverMonkey:ApplySqueeze(dt)
    if self.OriginalScale == nil then
        return false
    end

    self.SqueezeElapsed = (self.SqueezeElapsed + (tonumber(dt) or 0.0)) % SQUEEZE_CYCLE_SECONDS

    local halfCycle = SQUEEZE_CYCLE_SECONDS * 0.5
    local alpha = 0.0
    if self.SqueezeElapsed < halfCycle then
        alpha = self.SqueezeElapsed / halfCycle
    else
        alpha = 1.0 - ((self.SqueezeElapsed - halfCycle) / halfCycle)
    end

    local scaleMultiplier = lerp(1.0, SQUEEZE_MAX_SCALE, smooth_step(alpha))
    return self:SetMeshScale(multiply_vec3(self.OriginalScale, scaleMultiplier))
end

function GameOverMonkey:StopAnimation()
    local mesh = self:GetMesh()
    if mesh == nil then
        return false
    end

    local bStopped = false
    if mesh.StopAnimation ~= nil then
        local ok = pcall(function()
            mesh:StopAnimation()
        end)
        bStopped = bStopped or ok
    end
    if mesh.SetPlaying ~= nil then
        local ok = pcall(function()
            mesh:SetPlaying(false)
        end)
        bStopped = bStopped or ok
    end

    return bStopped
end

function GameOverMonkey:PlayAnimation()
    local mesh = self:GetMesh()
    if mesh == nil then
        log_failure("PlayAnimation failed: mesh is nil")
        return false
    end
    if mesh.PlayAnimationByPath == nil then
        log_failure("PlayAnimation failed: PlayAnimationByPath unavailable")
        return false
    end

    local ok, resultOrError = pcall(function()
        return mesh:PlayAnimationByPath(ANIMATION_PATH, ANIMATION_LOOPING)
    end)

    if not ok then
        log_failure("PlayAnimationByPath failed: " .. tostring(resultOrError))
        return false
    end

    if resultOrError == false then
        log_failure("PlayAnimationByPath returned false: " .. ANIMATION_PATH)
        return false
    end

    if mesh.SetPlayRate ~= nil then
        local rateOk, err = pcall(function()
            mesh:SetPlayRate(ANIMATION_PLAY_RATE)
        end)
        if not rateOk then
            log_failure("SetPlayRate failed: " .. tostring(err))
        end
    end

    return true
end

function GameOverMonkey:ResetPresentationState()
    self.State = STATE_NONE
    self.StateElapsed = 0.0
    self.PresentationElapsed = 0.0
    self.OnFinished = nil
    self.ActiveCamera = nil
    self.PlayerPawn = nil
    self.SavedControlRotation = nil
    self.LookStartControlRotation = nil
    self.LookTargetControlRotation = nil
    self.SqueezeElapsed = 0.0
    self.StartLocalLocation = nil
    self.CymbalsMonkeyActor = nil
    self.CymbalsMonkeyOriginalLocation = nil
    self.CymbalsMonkeyOriginalScale = nil
    self.CymbalsMonkeyStartLocation = nil
    self.CymbalsMonkeyTargetLocation = nil
    self.CymbalsMonkeyStartScale = nil
    self.CymbalsMonkeyTargetScale = nil
    self.SavedPostProcessParameters = nil
    self.bNoiseAudioPlaying = false
end

function GameOverMonkey:EnsureNoiseAudioLoaded()
    if self.bNoiseAudioLoaded then
        return true
    end
    if Audio == nil or Audio.Load == nil then
        return false
    end

    local ok, result = pcall(function()
        return Audio.Load(NOISE_AUDIO_KEY, NOISE_AUDIO_PATH, true)
    end)
    self.bNoiseAudioLoaded = ok and result ~= false
    return self.bNoiseAudioLoaded
end

function GameOverMonkey:StartNoiseAudio()
    if self.bNoiseAudioPlaying then
        return true
    end
    if not self:EnsureNoiseAudioLoaded() then
        return false
    end
    if Audio == nil or Audio.PlayLoop == nil then
        return false
    end

    local ok = pcall(function()
        Audio.PlayLoop(NOISE_AUDIO_KEY, NOISE_AUDIO_LOOP_NAME, 0.0)
    end)
    self.bNoiseAudioPlaying = ok == true
    return self.bNoiseAudioPlaying
end

function GameOverMonkey:SetNoiseAudioVolume(alpha)
    if not self.bNoiseAudioPlaying or Audio == nil or Audio.SetLoopVolume == nil then
        return false
    end

    local volume = NOISE_AUDIO_VOLUME * clamp01(alpha)
    local ok = pcall(function()
        Audio.SetLoopVolume(NOISE_AUDIO_LOOP_NAME, volume)
    end)
    return ok == true
end

function GameOverMonkey:StopNoiseAudio()
    if not self.bNoiseAudioPlaying then
        return true
    end

    if Audio ~= nil and Audio.StopLoop ~= nil then
        pcall(function()
            Audio.StopLoop(NOISE_AUDIO_LOOP_NAME)
        end)
    end
    self.bNoiseAudioPlaying = false
    return true
end

function GameOverMonkey:EnsureScreamAudioLoaded()
    if self.bScreamAudioLoaded then
        return true
    end
    if Audio == nil or Audio.Load == nil then
        return false
    end

    local ok, result = pcall(function()
        return Audio.Load(SCREAM_AUDIO_KEY, SCREAM_AUDIO_PATH, false)
    end)
    self.bScreamAudioLoaded = ok and result ~= false
    return self.bScreamAudioLoaded
end

function GameOverMonkey:PlayScreamAudio()
    if not self:EnsureScreamAudioLoaded() then
        return false
    end
    if Audio == nil or Audio.Play == nil then
        return false
    end

    local ok = pcall(function()
        Audio.Play(SCREAM_AUDIO_KEY, SCREAM_AUDIO_VOLUME)
    end)
    return ok == true
end

function GameOverMonkey:SavePostProcessParameters()
    local camera = self.ActiveCamera or get_active_camera(self.PlayerActor)
    if camera == nil then
        return false
    end

    if not ensure_horror_post_process(camera) then
        return false
    end

    local material = get_post_process_material(camera)
    if material == nil then
        return false
    end

    local saved = {
        Scalars = {},
        Vectors = {},
    }

    if material.GetScalarParameterValue ~= nil then
        for _, name in ipairs(POST_PROCESS_SCALAR_PARAMETERS) do
            local ok, value = pcall(function()
                return material:GetScalarParameterValue(name)
            end)
            if ok then
                saved.Scalars[name] = value
            end
        end
    end

    if material.GetVector4ParameterValue ~= nil then
        for _, name in ipairs(POST_PROCESS_VECTOR_PARAMETERS) do
            local ok, value = pcall(function()
                return material:GetVector4ParameterValue(name)
            end)
            if ok and value ~= nil then
                saved.Vectors[name] = value
            end
        end
    end

    self.ActiveCamera = camera
    self.SavedPostProcessParameters = saved
    return true
end

function GameOverMonkey:RestorePostProcessParameters()
    local saved = self.SavedPostProcessParameters
    if saved == nil then
        return false
    end

    local camera = self.ActiveCamera or get_active_camera(self.PlayerActor)
    if camera == nil or not ensure_horror_post_process(camera) then
        return false
    end

    if saved.Scalars ~= nil then
        for name, value in pairs(saved.Scalars) do
            set_post_process_scalar(camera, name, value)
        end
    end

    if saved.Vectors ~= nil then
        for name, value in pairs(saved.Vectors) do
            set_post_process_vector(camera, name, value)
        end
    end

    return true
end

function GameOverMonkey:GetSavedPostProcessScalar(name, fallback)
    local saved = self.SavedPostProcessParameters
    if saved ~= nil and saved.Scalars ~= nil and saved.Scalars[name] ~= nil then
        return saved.Scalars[name]
    end
    return fallback
end

function GameOverMonkey:GetSavedPostProcessVector(name, fallback)
    local saved = self.SavedPostProcessParameters
    if saved ~= nil and saved.Vectors ~= nil and saved.Vectors[name] ~= nil then
        return saved.Vectors[name]
    end
    return fallback
end

function GameOverMonkey:ApplyPostProcess(vignetteAlpha, noiseAlpha)
    local camera = self.ActiveCamera or get_active_camera(self.PlayerActor)
    if camera == nil then
        return false
    end

    self.ActiveCamera = camera
    ensure_horror_post_process(camera)

    vignetteAlpha = clamp01(vignetteAlpha)
    noiseAlpha = clamp01(noiseAlpha)

    set_post_process_vector(camera, "VignetteColor", lerp_vec4(
        self:GetSavedPostProcessVector("VignetteColor", make_vec4(0.0, 0.0, 0.0, 0.0)),
        make_vec4(1.0, 0.0, 0.0, 1.0),
        vignetteAlpha
    ))
    --set_post_process_scalar(camera, "VignetteIntensity", lerp(self:GetSavedPostProcessScalar("VignetteIntensity", 0.0), 1.35, vignetteAlpha))
    --set_post_process_scalar(camera, "VignetteRadius", lerp(self:GetSavedPostProcessScalar("VignetteRadius", 0.05), 0.05, vignetteAlpha))
    --set_post_process_scalar(camera, "VignetteSoftness", lerp(self:GetSavedPostProcessScalar("VignetteSoftness", 1.0), 1.0, vignetteAlpha))
    --set_post_process_scalar(camera, "ChromaticStrength", lerp(self:GetSavedPostProcessScalar("ChromaticStrength", 0.0), 0.5, vignetteAlpha))
    set_post_process_scalar(camera, "Time", self.PresentationElapsed)

    set_post_process_scalar(camera, "GrainStrength", lerp(self:GetSavedPostProcessScalar("GrainStrength", 0.0), 3.0, noiseAlpha))
    set_post_process_scalar(camera, "GrainScale", lerp(self:GetSavedPostProcessScalar("GrainScale", 1.0), 1.0, noiseAlpha))
    set_post_process_scalar(camera, "GrainDarkPower", lerp(self:GetSavedPostProcessScalar("GrainDarkPower", 0.0), 0.0, noiseAlpha))
    set_post_process_scalar(camera, "NoiseMin", lerp(self:GetSavedPostProcessScalar("NoiseMin", 0.0), 0.0, noiseAlpha))
    set_post_process_scalar(camera, "NoiseMax", lerp(self:GetSavedPostProcessScalar("NoiseMax", 1.0), 1.0, noiseAlpha))
    set_post_process_vector(camera, "NoiseColor", lerp_vec4(
        self:GetSavedPostProcessVector("NoiseColor", make_vec4(1.0, 1.0, 1.0, 0.0)),
        make_vec4(1.0, 1.0, 1.0, 1.0),
        noiseAlpha
    ))

    return true
end

function GameOverMonkey:ClearPostProcess()
    return self:RestorePostProcessParameters()
end

function GameOverMonkey:StartLookAtCymbalsMonkey()
    self.State = STATE_LOOK_AT
    self.StateElapsed = 0.0
    self.ActiveCamera = get_active_camera(self.PlayerActor)
    self.PlayerPawn = get_player_pawn(self.PlayerActor)

    local camera = self.ActiveCamera
    local monkey = find_first_actor_by_tag(CYMBALS_MONKEY_TAG)
    if camera == nil or monkey == nil or self.PlayerPawn == nil then
        self:StartRevealDelay()
        return false
    end

    local cameraLocation = get_location(camera)
    local monkeyLocation = get_location(monkey)
    local startControlRotation = get_control_rotation(self.PlayerPawn)
    local targetLookRotation = calculate_look_rotation(cameraLocation, monkeyLocation, startControlRotation)
    if startControlRotation == nil or targetLookRotation == nil then
        self:StartRevealDelay()
        return false
    end

    self.SavedControlRotation = self.SavedControlRotation or copy_vec3(startControlRotation)
    self.LookStartControlRotation = copy_vec3(startControlRotation)
    self.LookTargetControlRotation = Vec3(
        targetLookRotation.X or 0.0,
        targetLookRotation.Y or 0.0,
        targetLookRotation.Z or 0.0
    )
    return true
end

function GameOverMonkey:TickLookAtCymbalsMonkey(dt)
    self.StateElapsed = self.StateElapsed + dt
    local alpha = smooth_step(self.StateElapsed / LOOK_AT_SECONDS)

    if self.PlayerPawn ~= nil
        and self.LookStartControlRotation ~= nil
        and self.LookTargetControlRotation ~= nil then
        set_control_rotation(self.PlayerPawn, Vec3(
            lerp_angle(self.LookStartControlRotation.X or 0.0, self.LookTargetControlRotation.X or 0.0, alpha),
            lerp_angle(self.LookStartControlRotation.Y or 0.0, self.LookTargetControlRotation.Y or 0.0, alpha),
            lerp_angle(self.LookStartControlRotation.Z or 0.0, self.LookTargetControlRotation.Z or 0.0, alpha)
        ))
    end

    if self.StateElapsed >= LOOK_AT_SECONDS then
        if self.PlayerPawn ~= nil and self.LookTargetControlRotation ~= nil then
            set_control_rotation(self.PlayerPawn, self.LookTargetControlRotation)
        end
        self:StartRevealDelay()
    end
end

function GameOverMonkey:StartRevealDelay()
    self.State = STATE_REVEAL_DELAY
    self.StateElapsed = 0.0
end

function GameOverMonkey:TickRevealDelay(dt)
    self.StateElapsed = self.StateElapsed + dt
    if self.StateElapsed >= REVEAL_DELAY_SECONDS then
        self:StartRiseMonkey()
    end
end

function GameOverMonkey:StartRiseMonkey()
    self.State = STATE_RISE
    self.StateElapsed = 0.0

    local originalLocation = self.OriginalLocalLocation or self:GetMeshLocalLocation()
    if originalLocation == nil then
        log_failure("StartRiseMonkey failed: original local location is nil")
        self:StartRedVignetteAndShake()
        return false
    end

    self.OriginalLocalLocation = copy_vec3(originalLocation)
    self.StartLocalLocation = Vec3(
        self.OriginalLocalLocation.X or 0.0,
        self.OriginalLocalLocation.Y or 0.0,
        (self.OriginalLocalLocation.Z or 0.0) - 1.0
    )
    self:SetMeshLocalLocation(self.StartLocalLocation)
    self:SetVisible(true)
    self:StartCymbalsMonkeyApproach()
    self:PlayScreamAudio()
    return true
end

function GameOverMonkey:StartCymbalsMonkeyApproach()
    local monkey = find_first_actor_by_tag(CYMBALS_MONKEY_TAG)
    local playerLocation = get_location(self.PlayerPawn)
        or get_location(self.PlayerActor)
        or get_location(self.ActiveCamera)
    local monkeyLocation = get_location(monkey)
    local monkeyScale = get_actor_scale(monkey)
    if monkey == nil or playerLocation == nil or monkeyLocation == nil or monkeyScale == nil then
        return false
    end

    self.CymbalsMonkeyActor = monkey
    self.CymbalsMonkeyOriginalLocation = copy_vec3(monkeyLocation)
    self.CymbalsMonkeyOriginalScale = copy_vec3(monkeyScale)
    self.CymbalsMonkeyStartLocation = copy_vec3(monkeyLocation)
    self.CymbalsMonkeyTargetLocation = copy_vec3(playerLocation)
    self.CymbalsMonkeyStartScale = copy_vec3(monkeyScale)
    self.CymbalsMonkeyTargetScale = Vec3(
        CYMBALS_MONKEY_RISE_TARGET_SCALE,
        CYMBALS_MONKEY_RISE_TARGET_SCALE,
        CYMBALS_MONKEY_RISE_TARGET_SCALE
    )
    return true
end

function GameOverMonkey:TickRiseMonkey(dt)
    self.StateElapsed = self.StateElapsed + dt
    local alpha = smooth_step(self.StateElapsed / RISE_SECONDS)
    local location = lerp_vec3(self.StartLocalLocation, self.OriginalLocalLocation, alpha)
    self:SetMeshLocalLocation(location)
    self:TickCymbalsMonkeyApproach(clamp01(self.StateElapsed / RISE_SECONDS))

    if self.StateElapsed >= RISE_SECONDS then
        self:SetMeshLocalLocation(self.OriginalLocalLocation)
        self:CompleteCymbalsMonkeyApproach()
        self:StartRedVignetteAndShake()
    end
end

function GameOverMonkey:TickCymbalsMonkeyApproach(alpha)
    if self.CymbalsMonkeyActor == nil then
        return false
    end

    local location = lerp_vec3(self.CymbalsMonkeyStartLocation, self.CymbalsMonkeyTargetLocation, alpha)
    local scale = lerp_vec3(self.CymbalsMonkeyStartScale, self.CymbalsMonkeyTargetScale, alpha)
    set_actor_location(self.CymbalsMonkeyActor, location)
    set_actor_scale(self.CymbalsMonkeyActor, scale)
    return true
end

function GameOverMonkey:CompleteCymbalsMonkeyApproach()
    if self.CymbalsMonkeyActor == nil then
        return false
    end

    set_actor_location(self.CymbalsMonkeyActor, self.CymbalsMonkeyTargetLocation)
    set_actor_scale(self.CymbalsMonkeyActor, self.CymbalsMonkeyTargetScale)
    return true
end

function GameOverMonkey:RestoreCymbalsMonkeyTransform()
    if self.CymbalsMonkeyActor == nil then
        return false
    end

    if self.CymbalsMonkeyOriginalLocation ~= nil then
        set_actor_location(self.CymbalsMonkeyActor, self.CymbalsMonkeyOriginalLocation)
    end
    if self.CymbalsMonkeyOriginalScale ~= nil then
        set_actor_scale(self.CymbalsMonkeyActor, self.CymbalsMonkeyOriginalScale)
    end
    return true
end

function GameOverMonkey:StartRedVignetteAndShake()
    self.State = STATE_RED_VIGNETTE
    self.StateElapsed = 0.0
    self.SqueezeElapsed = 0.0
    self:PlayAnimation()
    self:ApplyPostProcess(0.0, 0.0)
    self:StartNoiseAudio()
    self:SetNoiseAudioVolume(0.0)
end

function GameOverMonkey:TickRedVignetteAndShake(dt)
    self.StateElapsed = self.StateElapsed + dt
    local progress = self.StateElapsed / RED_VIGNETTE_SECONDS
    local vignetteAlpha = clamp01(progress)
    local noiseAlpha = clamp01(progress)
    self:ApplyPostProcess(vignetteAlpha, noiseAlpha)
    self:SetNoiseAudioVolume(noiseAlpha)
    self:ApplySqueeze(dt)

    if self.StateElapsed >= RED_VIGNETTE_SECONDS then
        self:StartNoiseAndMenu()
    end
end

function GameOverMonkey:StartNoiseAndMenu()
    self.State = STATE_FINISHED
    self.StateElapsed = 0.0
    self:SetMeshScale(self.OriginalScale)
    self:ApplyPostProcess(1.0, 1.0)
    self:SetNoiseAudioVolume(1.0)

    local callback = self.OnFinished
    self.OnFinished = nil
    if callback ~= nil then
        pcall(callback)
    end
end

function GameOverMonkey:StartPresentation(onFinished)
    self:ClearPresentation()
    self.OnFinished = onFinished
    self.OriginalScale = self:GetMeshScale()
    self.OriginalLocalLocation = self:GetMeshLocalLocation()
    self.ActiveCamera = get_active_camera(self.PlayerActor)
    self:SavePostProcessParameters()
    self.PlayerPawn = get_player_pawn(self.PlayerActor)
    self.SavedControlRotation = copy_vec3(get_control_rotation(self.PlayerPawn))
    self:StartLookAtCymbalsMonkey()
    return true
end

function GameOverMonkey:Tick(dt)
    if self.State == STATE_NONE or self.State == STATE_FINISHED then
        return
    end

    dt = tonumber(dt) or 0.0
    if dt < 0.0 then
        dt = 0.0
    end
    self.PresentationElapsed = self.PresentationElapsed + dt

    if self.State == STATE_LOOK_AT then
        self:TickLookAtCymbalsMonkey(dt)
    elseif self.State == STATE_REVEAL_DELAY then
        self:TickRevealDelay(dt)
    elseif self.State == STATE_RISE then
        self:TickRiseMonkey(dt)
    elseif self.State == STATE_RED_VIGNETTE then
        self:TickRedVignetteAndShake(dt)
    end
end

function GameOverMonkey:PlayPresentationAnimation()
    return self:StartPresentation(nil)
end

function GameOverMonkey:Hide()
    self:StopAnimation()
    self:SetMeshScale(self.OriginalScale)
    self:SetMeshLocalLocation(self.OriginalLocalLocation)
    return self:SetVisible(false)
end

function GameOverMonkey:ClearPresentation()
    local savedControlRotation = self.SavedControlRotation
    local pawn = self.PlayerPawn

    self:StopAnimation()
    if self.OriginalScale ~= nil then
        self:SetMeshScale(self.OriginalScale)
    end
    if self.OriginalLocalLocation ~= nil then
        self:SetMeshLocalLocation(self.OriginalLocalLocation)
    end
    self:SetVisible(false)
    self:RestoreCymbalsMonkeyTransform()
    self:ClearPostProcess()
    self:StopNoiseAudio()

    if savedControlRotation ~= nil then
        set_control_rotation(pawn, savedControlRotation)
    end

    self:ResetPresentationState()
    return true
end

function GameOverMonkey:Initialize(playerActor)
    self.PlayerActor = playerActor
    self.Mesh = nil
    self.OriginalScale = self:GetMeshScale()
    self.OriginalLocalLocation = self:GetMeshLocalLocation()
    self:ClearPresentation()
end

function GameOverMonkey:Shutdown()
    self:ClearPresentation()
    self.PlayerActor = nil
    self.Mesh = nil
    self.OriginalScale = nil
    self.OriginalLocalLocation = nil
    self.SavedControlRotation = nil
end

return GameOverMonkey
