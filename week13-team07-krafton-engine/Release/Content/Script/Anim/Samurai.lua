-- Samurai animation state script.
-- Player input, movement, and dash movement live in Script/PlayerCharacter.lua.

local PlayerCharacter = require("PlayerCharacter")

local IDLE_PATH = "Content/Animation/Samurai_UE4/SamuraiIdle.uasset"
local WALK_PATH = "Content/Animation/Samurai_UE4/SamuraiWalk.uasset"
local RUN_PATH  = "Content/Animation/Samurai_UE4/SamuraiSprint.uasset"
local JUMP_PATH = "Content/Animation/Samurai_UE4/SamuraiJump.uasset"

local ATTACK1_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack1.uasset"
local ATTACK2_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack2.uasset"
local ATTACK3_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack3.uasset"
local ATTACK4_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack4.uasset"
local ATTACK5_PATH = "Content/Animation/Samurai_UE4/SamuraiAttack5.uasset"

local DASH_SLASH_PATH = "Content/Animation/Samurai_UE4/SamuraiAttackHeavy1_Start.uasset"
local ULTIMATE_ATTACK_PATH = "Content/Animation/Samurai_UE4/SamuraiAttackUltimate.uasset"

local WALK_THRESHOLD = 0.1
local RUN_THRESHOLD  = 8.0
local RUN_SAMPLE_SPEED = 10.0
local LOCOMOTION_SPEED_RESPONSE = 12.0
local JUMP_LOOP = false

local ATTACK_BLEND_IN  = 0.08
local ATTACK_BLEND_OUT = 0.15

local DASH_SLASH_BLEND_IN  = 0.05
local DASH_SLASH_BLEND_OUT = 0.12

local ULTIMATE_ATTACK_BLEND_IN  = 0.05
local ULTIMATE_ATTACK_BLEND_OUT = 0.12

local VK_SHIFT = 0x10

local function ResetAttack(self, unlockMovement)
    self.AttackIndex = 0
    self.ComboWindow = false
    self.ComboQueued = false
    self.AttackEnd = false

    if unlockMovement ~= false then
        PlayerCharacter.SetMovementInputEnabled(self, true)
    end

    PlayerCharacter.SetKatanaTrailActive(false)
end

local function BeginAttack(self, index)
    self.AttackIndex = index
    self.ComboWindow = false
    self.ComboQueued = false
    self.AttackEnd = false
    PlayerCharacter.StopMovementImmediately(self)
    PlayerCharacter.StepAttackForward(self)
    PlayerCharacter.SetKatanaTrailActive(true)
end

local function BeginDashSlash(self)
    ResetAttack(self, false)
    PlayerCharacter.BeginDashSlash(self)
end

local function EndDashSlash(self)
    PlayerCharacter.EndDashSlash(self)
end

function init(self)
    self.Speed = 0.0
    self.BlendSpeed = 0.0

    self.AttackPressed = false
    self.DashSlashPressed = false

    self.DashSlashActive = false
    self.DashSlashElapsed = 0.0
    self.DashSlashEnd = false

    PlayerCharacter.Initialize(self, obj)
    ResetAttack(self)

    local loco = Anim.create_blend_space_1d(0.0)
    Anim.blend_space_1d_add_sample(loco, IDLE_PATH, 0.0, 1.0, true)
    Anim.blend_space_1d_add_sample(loco, WALK_PATH, RUN_THRESHOLD, 1.0, true)
    Anim.blend_space_1d_add_sample(loco, RUN_PATH, RUN_SAMPLE_SPEED, 1.0, true)
    self.LocomotionBlendSpace = loco

    local top = Anim.create_state_machine("Top")

    Anim.sm_add_state(top, "Locomotion", loco)
    Anim.sm_add_state(top, "Jump", Anim.create_sequence_player(JUMP_PATH, 1.0, JUMP_LOOP))

    Anim.sm_add_state(top, "Attack1", Anim.create_sequence_player(ATTACK1_PATH, 1.5, false))
    Anim.sm_add_state(top, "Attack2", Anim.create_sequence_player(ATTACK2_PATH, 1.5, false))
    Anim.sm_add_state(top, "Attack3", Anim.create_sequence_player(ATTACK3_PATH, 1.5, false))
    Anim.sm_add_state(top, "Attack4", Anim.create_sequence_player(ATTACK4_PATH, 1.5, false))
    Anim.sm_add_state(top, "Attack5", Anim.create_sequence_player(ATTACK5_PATH, 1.5, false))

    Anim.sm_add_state(top, "DashSlash", Anim.create_sequence_player(DASH_SLASH_PATH, 3.0, false))

    Anim.sm_add_transition(top, "AnyState", "DashSlash",
        function()
            if self.DashSlashPressed and not self.DashSlashActive and not Anim.is_owner_falling() and not PlayerCharacter.IsUltimateRunning then
                BeginDashSlash(self)
                return true
            end
            return false
        end,
        DASH_SLASH_BLEND_IN
    )

    Anim.sm_add_state(top, "UltimateAttack", Anim.create_sequence_player(ULTIMATE_ATTACK_PATH, 1.2, false))

    Anim.sm_add_transition(top, "AnyState", "UltimateAttack",
        function()
            return PlayerCharacter.IsInUltimateMode == true
        end,
        ULTIMATE_ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "UltimateAttack", "Locomotion",
        function()
            return PlayerCharacter.IsInUltimateMode ~= true
        end,
        ULTIMATE_ATTACK_BLEND_OUT
    )

    Anim.sm_add_transition(top, "DashSlash", "Locomotion",
        function()
            if self.DashSlashEnd then
                EndDashSlash(self)
                return true
            end
            return false
        end,
        DASH_SLASH_BLEND_OUT
    )

    Anim.sm_add_transition(top, "Locomotion", "Jump",
        function()
            return Anim.is_owner_falling()
        end,
        0.1
    )

    Anim.sm_add_transition(top, "Jump", "Locomotion",
        function()
            return not Anim.is_owner_falling()
        end,
        0.2
    )

    Anim.sm_add_transition(top, "Locomotion", "Attack1",
        function()
            if self.AttackPressed then
                BeginAttack(self, 1)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "Attack1", "Attack2",
        function()
            if self.AttackEnd and self.ComboQueued then
                BeginAttack(self, 2)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "Attack1", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    Anim.sm_add_transition(top, "Attack2", "Attack3",
        function()
            if self.AttackEnd and self.ComboQueued then
                BeginAttack(self, 3)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "Attack2", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    Anim.sm_add_transition(top, "Attack3", "Attack4",
        function()
            if self.AttackEnd and self.ComboQueued then
                BeginAttack(self, 4)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "Attack3", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    Anim.sm_add_transition(top, "Attack4", "Attack5",
        function()
            if self.AttackEnd and self.ComboQueued then
                BeginAttack(self, 5)
                return true
            end
            return false
        end,
        ATTACK_BLEND_IN
    )

    Anim.sm_add_transition(top, "Attack4", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    Anim.sm_add_transition(top, "Attack5", "Locomotion",
        function()
            if self.AttackEnd then
                ResetAttack(self)
                return true
            end
            return false
        end,
        ATTACK_BLEND_OUT
    )

    Anim.sm_set_initial_state(top, "Locomotion")

    local root = Anim.create_slot("DefaultSlot", top)
    Anim.set_root_node(root)
end

function update(self, dt)
    self.Speed = Anim.get_owner_speed()
    local blendAlpha = math.min(dt * LOCOMOTION_SPEED_RESPONSE, 1.0)
    self.BlendSpeed = self.BlendSpeed + (self.Speed - self.BlendSpeed) * blendAlpha
    Anim.blend_space_1d_set_input(self.LocomotionBlendSpace, self.BlendSpeed)

    self.AttackPressed = Anim.is_left_mouse_pressed()
    self.DashSlashPressed = Anim.is_key_pressed(VK_SHIFT)

    if self.AttackPressed and self.AttackIndex > 0 and self.ComboWindow then
        self.ComboQueued = true
    end

    if self.DashSlashActive then
        PlayerCharacter.UpdateDashSlash(self, dt)
    elseif self.AttackIndex == 0 then
        PlayerCharacter.ApplyMoveInput(self)
    else
        local dir = PlayerCharacter.GetMoveInputWorldDirection(self)
        if dir ~= nil then
            PlayerCharacter.SmoothFaceOwnerToDirection(self, dir, dt)
        end
    end
end

function on_notify(self, name)
    print("[LuaAnim] notify: " .. name)

    if name == "ComboWindowOpen" then
        self.ComboWindow = true
        return
    end

    if name == "ComboWindowClose" then
        self.ComboWindow = false
        return
    end

    if name == "AttackEnd" then
        self.ComboWindow = false
        self.AttackEnd = true
        return
    end

    if name == "DashSlashEnd" then
        self.DashSlashEnd = true
        return
    end
end
