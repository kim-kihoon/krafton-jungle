local AnimTest = {}

local N = {
    Idle = "Idle",
    IntoRun = "IntoRun",
    Run = "Run",
    Homeguard = "Homeguard",
    WalkToIdle = "WalkToIdle",
    RunToIdle = "RunToIdle",
    LightAttack1 = "LightAttack1",
    LightAttack3 = "LightAttack3",
    HeavyAttack = "HeavyAttack",
    Attack1ToIdle = "Attack1ToIdle",
    Attack2ToIdle = "Attack2ToIdle",
    Attack3ToIdle = "Attack3ToIdle",
}

AnimTest.Properties = {
    IdleAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Idle.anm.anim", Category = "Animation" },
    IntoRunAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Into_Run.anim", Category = "Animation" },
    RunAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/gwen_anim_Skeleton_Run.anm.anim", Category = "Animation" },
    HomeguardAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Gwen_Homeguard.anm.anim", Category = "Animation" },
    WalkToIdleAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_IdleIn.anim", Category = "Animation" },
    RunToIdleAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Idlein2.anim", Category = "Animation" },
    LightAttackAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack1.anim", Category = "Animation" },
    HeavyAttackAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack2.anim", Category = "Animation" },
    LightAttack3AnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack3.anim", Category = "Animation" },
    Attack1ToIdleAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack1_To_Idle.anim", Category = "Animation" },
    Attack2ToIdleAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack2_To_Idle.anim", Category = "Animation" },
    Attack3ToIdleAnimationPath = { Type = "String", Default = "Asset/SkeletalMesh/GwenFBX/Gwen_anim_Skeleton_Attack3_To_Idle.anim", Category = "Animation" },

    LightAttackSoundPath1 = { Type = "String", Default = "Asset/Audio/Gwen_Original_BasicAttack_1.ogg", Category = "Audio" },
    LightAttackSoundPath2 = { Type = "String", Default = "Asset/Audio/Gwen_Original_BasicAttack_2.ogg", Category = "Audio" },
    LightAttackSoundPath3 = { Type = "String", Default = "Asset/Audio/Gwen_Original_BasicAttack_3.ogg", Category = "Audio" },
    HeavyAttackSoundPath = { Type = "String", Default = "Asset/Audio/Gwen_Original_BasicAttack_0.ogg", Category = "Audio" },
    VoiceSoundPath1 = { Type = "String", Default = "Asset/Audio/Gwen_Original_Move_6.ogg", Category = "Audio" },
    VoiceSoundPath2 = { Type = "String", Default = "Asset/Audio/Gwen_Original_Move_8.ogg", Category = "Audio" },
    VoiceSoundPath3 = { Type = "String", Default = "Asset/Audio/Gwen_Original_Move_10.ogg", Category = "Audio" },
    VoiceSoundPath4 = { Type = "String", Default = "Asset/Audio/Gwen_Original_Move_20.ogg", Category = "Audio" },
    BGMPath = { Type = "String", Default = "Asset/Audio/StarGuardian_2017_SpawnMusic.ogg", Category = "Audio" },
    IntroSoundPath = { Type = "String", Default = "Asset/Audio/league-of-legends-original-sounds-welcome-to-summoners-rift.mp3", Category = "Audio" },
    IntroSoundDelay = { Type = "Float", Default = 10.0, Min = 0.0, Max = 30.0, Category = "Audio" },
    IntroSoundVolumeScale = { Type = "Float", Default = 2.0, Min = 0.0, Max = 5.0, Category = "Audio" },

    MoveSpeed = { Type = "Float", Default = 7.0, Min = 0.0, Max = 100.0, Category = "Movement" },
    SprintSpeedMultiplier = { Type = "Float", Default = 1.5, Min = 1.0, Max = 10.0, Category = "Movement" },
    LookSensitivityDegrees = { Type = "Float", Default = 0.12, Min = 0.0, Max = 5.0, Category = "Movement" },
    MeshTurnSpeedDegreesPerSecond = { Type = "Float", Default = 540.0, Min = 0.0, Max = 1440.0, Category = "Movement" },
    RotateMeshToMovement = { Type = "Bool", Default = true, Category = "Movement" },

    LocomotionBlendTime = { Type = "Float", Default = 0.0, Min = 0.0, Max = 5.0, Category = "Animation" },
    IntoRunDuration = { Type = "Float", Default = 1.2, Min = 0.0, Max = 5.0, Category = "Animation" },
    WalkToIdleDuration = { Type = "Float", Default = 3.533, Min = 0.0, Max = 5.0, Category = "Animation" },
    RunToIdleDuration = { Type = "Float", Default = 2.233, Min = 0.0, Max = 5.0, Category = "Animation" },
    LightAttackDuration = { Type = "Float", Default = 0.8, Min = 0.0, Max = 5.0, Category = "Animation" },
    HeavyAttackDuration = { Type = "Float", Default = 1.2, Min = 0.0, Max = 5.0, Category = "Animation" },
    LightAttack3Duration = { Type = "Float", Default = 0.8, Min = 0.0, Max = 5.0, Category = "Animation" },
    Attack1ToIdleDuration = { Type = "Float", Default = 2.1, Min = 0.0, Max = 5.0, Category = "Animation" },
    Attack2ToIdleDuration = { Type = "Float", Default = 1.967, Min = 0.0, Max = 5.0, Category = "Animation" },
    Attack3ToIdleDuration = { Type = "Float", Default = 2.633, Min = 0.0, Max = 5.0, Category = "Animation" },
    MoveStartSpeedThreshold = { Type = "Float", Default = 0.1, Min = 0.0, Max = 100.0, Category = "Animation" },
    AutoConfigureAnimation = { Type = "Bool", Default = true, Category = "Animation" },
}

local function clamp(value, min_value, max_value)
    value = tonumber(value) or min_value
    if value < min_value then
        return min_value
    end
    if max_value and value > max_value then
        return max_value
    end
    return value
end

local function normalize_axis(degrees)
    degrees = degrees % 360.0
    if degrees > 180.0 then
        degrees = degrees - 360.0
    end
    return degrees
end

local function axis_length(axis)
    if not axis then
        return 0.0
    end
    return math.sqrt(axis.X * axis.X + axis.Y * axis.Y)
end

local function is_action_active(action_name)
    local input = Engine.API.Input
    return input.WasActionStarted(action_name) or input.IsActionTriggered(action_name)
end

local function is_action_started(action_name)
    return Engine.API.Input.WasActionStarted(action_name)
end

local function condition(name, parameter_type, compare_op, value)
    return {
        ParameterName = name,
        Type = parameter_type,
        CompareOp = compare_op,
        Value = value,
    }
end

function AnimTest.new(component, properties)
    local self = {
        Component = component,
        Actor = component and component:GetOwner() or Actor,
        Properties = properties or {},
        LocomotionStateMachine = nil,
        NextLightAttackUsesAttack1 = true,
        VoiceTimer = 0.0,
        CurrentVoiceIndex = 1,
        IntroSoundTimer = 0.0,
        IntroSoundPlayed = false,
    }
    setmetatable(self, { __index = AnimTest })
    return self
end

function AnimTest:GetProperty(name, fallback)
    local value = self.Properties[name]
    if value == nil then
        return fallback
    end
    return value
end

function AnimTest:CacheComponents()
    self.SkeletalMeshComp = self.Actor and self.Actor:GetSkeletalMeshComponent() or nil
    self.SpringArmComp = self.Actor and self.Actor:GetSpringArmComponent() or nil
    self.CameraComp = self.Actor and self.Actor:GetCameraComponent() or nil

    if not self.SkeletalMeshComp then
        self.SkeletalMeshComp = self.Actor and self.Actor:GetComponent("SkeletalMeshComponent") or nil
    end
    if not self.SpringArmComp then
        self.SpringArmComp = self.Actor and self.Actor:GetComponent("SpringArmComponent") or nil
    end
    if not self.CameraComp then
        self.CameraComp = self.Actor and self.Actor:GetComponent("CameraComponent") or nil
    end
end

function AnimTest:ConfigureLocomotionStateMachine()
    if not self.SkeletalMeshComp then
        return
    end

    local paths = {
        Idle = self:GetProperty("IdleAnimationPath", ""),
        IntoRun = self:GetProperty("IntoRunAnimationPath", ""),
        Run = self:GetProperty("RunAnimationPath", ""),
        Homeguard = self:GetProperty("HomeguardAnimationPath", ""),
        WalkToIdle = self:GetProperty("WalkToIdleAnimationPath", ""),
        RunToIdle = self:GetProperty("RunToIdleAnimationPath", ""),
        LightAttack1 = self:GetProperty("LightAttackAnimationPath", ""),
        LightAttack3 = self:GetProperty("LightAttack3AnimationPath", ""),
        HeavyAttack = self:GetProperty("HeavyAttackAnimationPath", ""),
        Attack1ToIdle = self:GetProperty("Attack1ToIdleAnimationPath", ""),
        Attack2ToIdle = self:GetProperty("Attack2ToIdleAnimationPath", ""),
        Attack3ToIdle = self:GetProperty("Attack3ToIdleAnimationPath", ""),
    }

    for _, path in pairs(paths) do
        if path == "" then
            return
        end
    end

    local anim_instance = self.SkeletalMeshComp:GetAnimInstance()
    if not anim_instance then
        self.SkeletalMeshComp:UseDefaultAnimInstance()
        anim_instance = self.SkeletalMeshComp:GetAnimInstance()
    end
    if not anim_instance then
        return
    end

    anim_instance:RegisterAnimationPath(N.Idle, paths.Idle)
    anim_instance:RegisterAnimationPath(N.IntoRun, paths.IntoRun)
    anim_instance:RegisterAnimationPath(N.Run, paths.Run)
    anim_instance:RegisterAnimationPath(N.Homeguard, paths.Homeguard)
    anim_instance:RegisterAnimationPath(N.WalkToIdle, paths.WalkToIdle)
    anim_instance:RegisterAnimationPath(N.RunToIdle, paths.RunToIdle)
    anim_instance:RegisterAnimationPath(N.LightAttack1, paths.LightAttack1)
    anim_instance:RegisterAnimationPath(N.LightAttack3, paths.LightAttack3)
    anim_instance:RegisterAnimationPath(N.HeavyAttack, paths.HeavyAttack)
    anim_instance:RegisterAnimationPath(N.Attack1ToIdle, paths.Attack1ToIdle)
    anim_instance:RegisterAnimationPath(N.Attack2ToIdle, paths.Attack2ToIdle)
    anim_instance:RegisterAnimationPath(N.Attack3ToIdle, paths.Attack3ToIdle)
    anim_instance:SetAnimFloatParameter("Speed", 0.0)
    anim_instance:SetAnimFloatParameter("GroundSpeed", 0.0)
    anim_instance:SetAnimBoolParameter("bMoving", false)
    anim_instance:SetAnimBoolParameter("bSprinting", false)

    local machine = CreateAnimStateMachineAsset()
    machine:SetEntryState(N.Idle)
    machine:AddStateWithPath(N.Idle, N.Idle, paths.Idle, true)
    machine:AddStateWithPath(N.IntoRun, N.IntoRun, paths.IntoRun, false)
    machine:AddStateWithPath(N.Run, N.Run, paths.Run, true)
    machine:AddStateWithPath(N.Homeguard, N.Homeguard, paths.Homeguard, true)
    machine:AddStateWithPath(N.WalkToIdle, N.WalkToIdle, paths.WalkToIdle, false)
    machine:AddStateWithPath(N.RunToIdle, N.RunToIdle, paths.RunToIdle, false)
    machine:AddStateWithPath(N.LightAttack1, N.LightAttack1, paths.LightAttack1, false)
    machine:AddStateWithPath(N.LightAttack3, N.LightAttack3, paths.LightAttack3, false)
    machine:AddStateWithPath(N.HeavyAttack, N.HeavyAttack, paths.HeavyAttack, false)
    machine:AddStateWithPath(N.Attack1ToIdle, N.Attack1ToIdle, paths.Attack1ToIdle, false)
    machine:AddStateWithPath(N.Attack2ToIdle, N.Attack2ToIdle, paths.Attack2ToIdle, false)
    machine:AddStateWithPath(N.Attack3ToIdle, N.Attack3ToIdle, paths.Attack3ToIdle, false)

    machine:AddParameter("bMoving", "Bool")
    machine:AddParameter("bSprinting", "Bool")
    machine:AddParameter("StateElapsedTime", "Float")
    machine:AddParameter(N.LightAttack1, "Trigger")
    machine:AddParameter(N.LightAttack3, "Trigger")
    machine:AddParameter(N.HeavyAttack, "Trigger")

    local blend = clamp(self:GetProperty("LocomotionBlendTime", 0.0), 0.0)
    local into_run = clamp(self:GetProperty("IntoRunDuration", 1.2), 0.0)
    local walk_to_idle = clamp(self:GetProperty("WalkToIdleDuration", 3.533), 0.0)
    local run_to_idle = clamp(self:GetProperty("RunToIdleDuration", 2.233), 0.0)
    local light_attack = clamp(self:GetProperty("LightAttackDuration", 0.8), 0.0)
    local heavy_attack = clamp(self:GetProperty("HeavyAttackDuration", 1.2), 0.0)
    local light_attack3 = clamp(self:GetProperty("LightAttack3Duration", 0.8), 0.0)
    local attack1_to_idle = clamp(self:GetProperty("Attack1ToIdleDuration", 2.1), 0.0)
    local attack2_to_idle = clamp(self:GetProperty("Attack2ToIdleDuration", 1.967), 0.0)
    local attack3_to_idle = clamp(self:GetProperty("Attack3ToIdleDuration", 2.633), 0.0)

    machine:AddBoolTransition(N.Idle, N.Homeguard, "bSprinting", "IsTrue", true, blend, 20, "EaseInOut")
    machine:AddBoolTransition(N.Idle, N.IntoRun, "bMoving", "IsTrue", true, blend, 10, "EaseInOut")
    machine:AddBoolTransition(N.IntoRun, N.Homeguard, "bSprinting", "IsTrue", true, blend, 110, "EaseInOut")
    machine:AddBoolTransition(N.IntoRun, N.WalkToIdle, "bMoving", "IsFalse", false, blend, 100, "EaseInOut")
    machine:AddFloatTransition(N.IntoRun, N.Run, "StateElapsedTime", "GreaterEqual", into_run, blend, 10, "EaseInOut")
    machine:AddBoolTransition(N.Run, N.Homeguard, "bSprinting", "IsTrue", true, blend, 110, "EaseInOut")
    machine:AddBoolTransition(N.Run, N.WalkToIdle, "bMoving", "IsFalse", false, blend, 100, "EaseInOut")
    machine:AddBoolTransition(N.Homeguard, N.RunToIdle, "bMoving", "IsFalse", false, blend, 100, "EaseInOut")
    machine:AddBoolTransition(N.Homeguard, N.Run, "bSprinting", "IsFalse", false, blend, 90, "EaseInOut")
    machine:AddBoolTransition(N.WalkToIdle, N.Homeguard, "bSprinting", "IsTrue", true, blend, 120, "EaseInOut")
    machine:AddBoolTransition(N.WalkToIdle, N.Run, "bMoving", "IsTrue", true, blend, 110, "EaseInOut")
    machine:AddFloatTransition(N.WalkToIdle, N.Idle, "StateElapsedTime", "GreaterEqual", walk_to_idle, blend, 100, "EaseInOut")
    machine:AddBoolTransition(N.RunToIdle, N.Homeguard, "bSprinting", "IsTrue", true, blend, 120, "EaseInOut")
    machine:AddBoolTransition(N.RunToIdle, N.Run, "bMoving", "IsTrue", true, blend, 110, "EaseInOut")
    machine:AddFloatTransition(N.RunToIdle, N.Idle, "StateElapsedTime", "GreaterEqual", run_to_idle, blend, 100, "EaseInOut")

    local locomotion_states = { N.Idle, N.IntoRun, N.Run, N.Homeguard, N.WalkToIdle, N.RunToIdle }
    local recovery_states = { N.Attack1ToIdle, N.Attack2ToIdle, N.Attack3ToIdle }
    for _, state in ipairs(locomotion_states) do
        machine:AddTriggerTransition(state, N.HeavyAttack, N.HeavyAttack, blend, 200, "EaseInOut")
        machine:AddTriggerTransition(state, N.LightAttack1, N.LightAttack1, blend, 190, "EaseInOut")
        machine:AddTriggerTransition(state, N.LightAttack3, N.LightAttack3, blend, 190, "EaseInOut")
    end
    for _, state in ipairs(recovery_states) do
        machine:AddTriggerTransition(state, N.HeavyAttack, N.HeavyAttack, blend, 200, "EaseInOut")
        machine:AddTriggerTransition(state, N.LightAttack1, N.LightAttack1, blend, 190, "EaseInOut")
        machine:AddTriggerTransition(state, N.LightAttack3, N.LightAttack3, blend, 190, "EaseInOut")
    end

    machine:AddTransition(N.LightAttack1, N.Homeguard, {
        condition("StateElapsedTime", "Float", "GreaterEqual", light_attack),
        condition("bSprinting", "Bool", "IsTrue", true),
    }, blend, 120, "EaseInOut")
    machine:AddTransition(N.LightAttack1, N.Run, {
        condition("StateElapsedTime", "Float", "GreaterEqual", light_attack),
        condition("bMoving", "Bool", "IsTrue", true),
    }, blend, 110, "EaseInOut")
    machine:AddFloatTransition(N.LightAttack1, N.Attack1ToIdle, "StateElapsedTime", "GreaterEqual", light_attack, blend, 100, "EaseInOut")

    machine:AddTransition(N.HeavyAttack, N.Homeguard, {
        condition("StateElapsedTime", "Float", "GreaterEqual", heavy_attack),
        condition("bSprinting", "Bool", "IsTrue", true),
    }, blend, 120, "EaseInOut")
    machine:AddTransition(N.HeavyAttack, N.Run, {
        condition("StateElapsedTime", "Float", "GreaterEqual", heavy_attack),
        condition("bMoving", "Bool", "IsTrue", true),
    }, blend, 110, "EaseInOut")
    machine:AddFloatTransition(N.HeavyAttack, N.Attack2ToIdle, "StateElapsedTime", "GreaterEqual", heavy_attack, blend, 100, "EaseInOut")

    machine:AddTransition(N.LightAttack3, N.Homeguard, {
        condition("StateElapsedTime", "Float", "GreaterEqual", light_attack3),
        condition("bSprinting", "Bool", "IsTrue", true),
    }, blend, 120, "EaseInOut")
    machine:AddTransition(N.LightAttack3, N.Run, {
        condition("StateElapsedTime", "Float", "GreaterEqual", light_attack3),
        condition("bMoving", "Bool", "IsTrue", true),
    }, blend, 110, "EaseInOut")
    machine:AddFloatTransition(N.LightAttack3, N.Attack3ToIdle, "StateElapsedTime", "GreaterEqual", light_attack3, blend, 100, "EaseInOut")

    machine:AddBoolTransition(N.Attack1ToIdle, N.Homeguard, "bSprinting", "IsTrue", true, blend, 120, "EaseInOut")
    machine:AddBoolTransition(N.Attack1ToIdle, N.Run, "bMoving", "IsTrue", true, blend, 110, "EaseInOut")
    machine:AddFloatTransition(N.Attack1ToIdle, N.Idle, "StateElapsedTime", "GreaterEqual", attack1_to_idle, blend, 100, "EaseInOut")
    machine:AddBoolTransition(N.Attack2ToIdle, N.Homeguard, "bSprinting", "IsTrue", true, blend, 120, "EaseInOut")
    machine:AddBoolTransition(N.Attack2ToIdle, N.Run, "bMoving", "IsTrue", true, blend, 110, "EaseInOut")
    machine:AddFloatTransition(N.Attack2ToIdle, N.Idle, "StateElapsedTime", "GreaterEqual", attack2_to_idle, blend, 100, "EaseInOut")
    machine:AddBoolTransition(N.Attack3ToIdle, N.Homeguard, "bSprinting", "IsTrue", true, blend, 120, "EaseInOut")
    machine:AddBoolTransition(N.Attack3ToIdle, N.Run, "bMoving", "IsTrue", true, blend, 110, "EaseInOut")
    machine:AddFloatTransition(N.Attack3ToIdle, N.Idle, "StateElapsedTime", "GreaterEqual", attack3_to_idle, blend, 100, "EaseInOut")

    if self.SkeletalMeshComp:UseStateMachine(machine) then
        self.LocomotionStateMachine = machine
    else
        LogWarning("[AnimTest] Failed to activate locomotion state machine")
    end
end

function AnimTest:BeginPlay()
    self:CacheComponents()

    if self:GetProperty("AutoConfigureAnimation", true) then
        self:ConfigureLocomotionStateMachine()
    end

    local bgm_path = self:GetProperty("BGMPath", "")
    if bgm_path ~= "" then
        Engine.API.Audio.PlayBGM(bgm_path)
    end

    self.IntroSoundTimer = clamp(self:GetProperty("IntroSoundDelay", 10.0), 0.0)
    self.IntroSoundPlayed = false
end

function AnimTest:Tick(delta_time)
    self:UpdateIntroSound(delta_time)
    self:UpdateLocomotion(delta_time)
end

function AnimTest:UpdateIntroSound(delta_time)
    if self.IntroSoundPlayed then
        return
    end

    self.IntroSoundTimer = self.IntroSoundTimer - clamp(delta_time, 0.0)
    if self.IntroSoundTimer > 0.0 then
        return
    end

    local intro_path = self:GetProperty("IntroSoundPath", "")
    if intro_path ~= "" then
        local volume_scale = clamp(self:GetProperty("IntroSoundVolumeScale", 2.0), 0.0)
        Engine.API.Audio.PlaySFX(intro_path, volume_scale)
    end

    self.IntroSoundPlayed = true
end

function AnimTest:UpdateLocomotion(delta_time)
    if not self.Actor or not self.SkeletalMeshComp then
        return
    end

    if delta_time <= 0.0 then
        self:UpdateAnimationParameters(0.0, false, false)
        return
    end

    local input = Engine.API.Input
    local move_axis = input.GetActionAxis2D("Move")
    local look_axis = input.GetActionAxis2D("Look")
    local light_attack_started = is_action_started("Attack")
    local heavy_attack_started = is_action_started("HeavyAttack")
    local in_main_attack = self:IsInMainAttackState()
    local lock_movement = self:IsInAttackState() or light_attack_started or heavy_attack_started

    if self.SpringArmComp and look_axis and is_action_active("Look") then
        local sensitivity = clamp(self:GetProperty("LookSensitivityDegrees", 0.12), 0.0)
        self.SpringArmComp:AddYawInput(look_axis.X * sensitivity)
        self.SpringArmComp:AddPitchInput(-look_axis.Y * sensitivity)
    end

    local move_axis_length = math.min(axis_length(move_axis), 1.0)
    local sprinting = move_axis_length > 0.0 and is_action_active("Sprint")
    local move_speed = clamp(self:GetProperty("MoveSpeed", 7.0), 0.0)
    local sprint_multiplier = clamp(self:GetProperty("SprintSpeedMultiplier", 1.5), 1.0)
    local ground_speed = move_axis_length * move_speed * (sprinting and sprint_multiplier or 1.0)
    local moving = ground_speed > clamp(self:GetProperty("MoveStartSpeedThreshold", 0.1), 0.0)

    if moving and not lock_movement then
        local move_direction = self:BuildCameraRelativeMoveDirection(move_axis)
        self.Actor:Add_Actor_World_Offset(move_direction * (ground_speed * delta_time))

        if self:GetProperty("RotateMeshToMovement", true) then
            local atan2 = math.atan2 or math.atan
            local target_yaw = math.deg(atan2(move_direction.Y, move_direction.X)) - 90.0
            local mesh_rotation = self.SkeletalMeshComp.Rotation
            local actor_rotation = self.Actor.Rotation
            local target_relative_yaw = normalize_axis(target_yaw - actor_rotation.Z)
            local delta_yaw = normalize_axis(target_relative_yaw - mesh_rotation.Z)
            local max_step = clamp(self:GetProperty("MeshTurnSpeedDegreesPerSecond", 540.0), 0.0) * delta_time
            local yaw_step = clamp(delta_yaw, -max_step, max_step)
            mesh_rotation.Z = normalize_axis(mesh_rotation.Z + yaw_step)
            self.SkeletalMeshComp.Rotation = mesh_rotation
        end

        self:UpdateMoveVoice(delta_time)
    end

    self:UpdateAnimationParameters(ground_speed, moving, sprinting)

    local anim_instance = self.SkeletalMeshComp:GetAnimInstance()
    if not anim_instance or in_main_attack then
        return
    end

    if heavy_attack_started then
        anim_instance:SetAnimTriggerParameter(N.HeavyAttack)
    elseif light_attack_started then
        if self.NextLightAttackUsesAttack1 then
            anim_instance:SetAnimTriggerParameter(N.LightAttack1)
            self.NextLightAttackUsesAttack1 = false
        else
            anim_instance:SetAnimTriggerParameter(N.LightAttack3)
            self.NextLightAttackUsesAttack1 = true
        end
    end
end

function AnimTest:UpdateMoveVoice(delta_time)
    self.VoiceTimer = self.VoiceTimer - delta_time
    if self.VoiceTimer > 0.0 then
        return
    end

    local voices = {}
    for i = 1, 4 do
        local path = self:GetProperty("VoiceSoundPath" .. tostring(i), "")
        if path ~= "" then
            voices[#voices + 1] = path
        end
    end

    if #voices > 0 then
        if self.CurrentVoiceIndex > #voices then
            self.CurrentVoiceIndex = 1
        end
        self:PlaySoundAtActor(voices[self.CurrentVoiceIndex])
        self.CurrentVoiceIndex = self.CurrentVoiceIndex + 1
    end

    self.VoiceTimer = 3.0 + Engine.API.Random.RandomFloat(0.0, 5.0)
end

function AnimTest:HandleAnimNotify(notify_name)
    if notify_name == "GwenLightAttack1Start" or notify_name == "GwenLightAttack3Start" then
        self:PlayLightAttackSound()
    elseif notify_name == "GwenHeavyAttackStart" then
        self:PlayHeavyAttackSound()
    end
end

function AnimTest:PlayLightAttackSound()
    local sound_paths = {}
    for i = 1, 3 do
        local path = self:GetProperty("LightAttackSoundPath" .. tostring(i), "")
        if path ~= "" then
            sound_paths[#sound_paths + 1] = path
        end
    end

    if #sound_paths == 0 then
        return
    end

    local index = Engine.API.Random.RandomInt(1, #sound_paths)
    if index < 1 then
        index = 1
    elseif index > #sound_paths then
        index = #sound_paths
    end
    self:PlaySoundAtActor(sound_paths[index])
end

function AnimTest:PlayHeavyAttackSound()
    self:PlaySoundAtActor(self:GetProperty("HeavyAttackSoundPath", ""))
end

function AnimTest:PlaySoundAtActor(sound_path)
    if sound_path ~= "" then
        Engine.API.Audio.PlaySFX(sound_path)
    end
end

function AnimTest:GetCurrentState()
    local anim_instance = self.SkeletalMeshComp and self.SkeletalMeshComp:GetAnimInstance() or nil
    return anim_instance and anim_instance:GetCurrentState() or ""
end

function AnimTest:IsInAttackState()
    local state = self:GetCurrentState()
    return state == N.LightAttack1
        or state == N.LightAttack3
        or state == N.HeavyAttack
        or state == N.Attack1ToIdle
        or state == N.Attack2ToIdle
        or state == N.Attack3ToIdle
end

function AnimTest:IsInMainAttackState()
    local state = self:GetCurrentState()
    return state == N.LightAttack1 or state == N.LightAttack3 or state == N.HeavyAttack
end

function AnimTest:BuildCameraRelativeMoveDirection(move_axis)
    local forward = self.SpringArmComp and self.SpringArmComp.Forward or self.Actor:Get_Actor_Forward()
    local right = self.SpringArmComp and self.SpringArmComp.Right or self.Actor:Get_Actor_Right()

    forward.Z = 0.0
    right.Z = 0.0
    forward = forward:GetSafeNormal()
    right = right:GetSafeNormal()

    local direction = forward * move_axis.Y + right * move_axis.X
    if direction:Normalize() then
        return direction
    end
    return self.Actor:Get_Actor_Forward()
end

function AnimTest:UpdateAnimationParameters(ground_speed, moving, sprinting)
    local anim_instance = self.SkeletalMeshComp and self.SkeletalMeshComp:GetAnimInstance() or nil
    if not anim_instance then
        return
    end

    anim_instance:SetAnimFloatParameter("Speed", ground_speed)
    anim_instance:SetAnimFloatParameter("GroundSpeed", ground_speed)
    anim_instance:SetAnimBoolParameter("bMoving", moving)
    anim_instance:SetAnimBoolParameter("bSprinting", sprinting)
end

return AnimTest
