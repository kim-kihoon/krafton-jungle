local FootIK = {}
FootIK.__index = FootIK

local DEFAULTS = {
    traceUp = 0.6,
    traceDown = 2.0,
    footOffset = 0.25,
    interpSpeed = 18.0,
    maxCorrection = 1.2,
    poleForward = 1.5,
    autoFootOffset = true,
    maxAutoFootOffset = 0.6,
    movingSpeedThreshold = 0.15,
    plantEnterClearance = 0.28,
    plantExitClearance = 0.48,
}

local LEG_BONES = {
    left = {
        root = "thigh_l",
        mid = "calf_l",
        foot = "foot_l",
    },
    right = {
        root = "thigh_r",
        mid = "calf_r",
        foot = "foot_r",
    },
}

local LEG_ORDER = { "left", "right" }

local function clamp01(value)
    if value < 0.0 then return 0.0 end
    if value > 1.0 then return 1.0 end
    return value
end

local function clamp(value, minValue, maxValue)
    if value < minValue then return minValue end
    if value > maxValue then return maxValue end
    return value
end

local function copyDefaults()
    local result = {}
    for key, value in pairs(DEFAULTS) do
        result[key] = value
    end
    return result
end

local function isGrounded(movement)
    if movement == nil then
        return true
    end
    if movement.IsMovingOnGround ~= nil then
        return movement:IsMovingOnGround()
    end
    if movement.IsWalking ~= nil then
        return movement:IsWalking()
    end
    if movement.IsFalling ~= nil then
        return not movement:IsFalling()
    end
    return true
end

local function getSpeed2D(movement)
    if movement ~= nil and movement.GetSpeed2D ~= nil then
        return movement:GetSpeed2D()
    end
    return 0.0
end

local function getPreIKBoneLocation(mesh, boneIndex)
    if mesh ~= nil and mesh.GetPreIKBoneLocationByIndex ~= nil then
        return mesh:GetPreIKBoneLocationByIndex(boneIndex)
    end
    return mesh:GetBoneLocationByIndex(boneIndex)
end

function FootIK.new(actor, options)
    local self = setmetatable({}, FootIK)
    self.actor = actor
    self.mesh = actor and actor:GetMesh() or nil
    self.movement = actor and actor:GetCharacterMovement() or nil
    self.config = copyDefaults()
    self.enabled = false
    self.legs = {}

    if options ~= nil then
        for key, value in pairs(options) do
            self.config[key] = value
        end
    end

    self:InitializeChains()
    return self
end

function FootIK:InitializeChains()
    if self.mesh == nil or self.actor == nil then
        return
    end

    self.mesh:SetTwoBoneIKEnabled(true)
    self.mesh:ClearTwoBoneIKChains()

    local poleWorld = self.actor.Location + self.actor.Forward * self.config.poleForward
    for _, side in ipairs(LEG_ORDER) do
        local bones = LEG_BONES[side]
        local chainIndex = self.mesh:AddTwoBoneIKChainByName(bones.root, bones.mid, bones.foot, poleWorld)
        local footIndex = self.mesh:FindBoneIndexByName(bones.foot)
        if chainIndex >= 0 and footIndex >= 0 then
            self.legs[side] = {
                chainIndex = chainIndex,
                footIndex = footIndex,
                footOffset = nil,
                targetZ = nil,
                planted = false,
            }
            self.mesh:SetIKChainEnabled(chainIndex, false)
            self.enabled = true
        end
    end
end

function FootIK:SetAllChainsEnabled(enabled)
    if self.mesh == nil then
        return
    end

    for _, leg in pairs(self.legs) do
        self.mesh:SetIKChainEnabled(leg.chainIndex, enabled)
        if not enabled then
            leg.targetZ = nil
            leg.planted = false
        end
    end
end

function FootIK:Disable()
    self:SetAllChainsEnabled(false)
end

function FootIK:DisableLeg(leg)
    self.mesh:SetIKChainEnabled(leg.chainIndex, false)
    leg.targetZ = nil
    leg.planted = false
end

function FootIK:UpdateLeg(leg, dt, isMoving)
    local footWorld = getPreIKBoneLocation(self.mesh, leg.footIndex)
    local traceStart = footWorld + Vector.Up() * self.config.traceUp
    local traceLength = self.config.traceUp + self.config.traceDown
    local hit = World.PhysicsRaycast(traceStart, Vector.Down(), traceLength, self.actor, "FootIK")

    if hit == nil or hit.bHit == false then
        self:DisableLeg(leg)
        return
    end

    local clearance = footWorld.Z - hit.WorldHitLocation.Z
    if isMoving then
        local shouldPlant = false
        if leg.planted then
            shouldPlant = clearance <= self.config.plantExitClearance
        else
            shouldPlant = clearance <= self.config.plantEnterClearance
        end

        if not shouldPlant then
            self:DisableLeg(leg)
            return
        end
    end

    leg.planted = true

    local footOffset = self.config.footOffset
    if self.config.autoFootOffset then
        if leg.footOffset == nil then
            local measuredOffset = footWorld.Z - hit.WorldHitLocation.Z
            leg.footOffset = clamp(measuredOffset, 0.0, self.config.maxAutoFootOffset)
        end
        footOffset = leg.footOffset
    end

    local desiredZ = hit.WorldHitLocation.Z + footOffset
    local correction = math.abs(footWorld.Z - desiredZ)
    if correction > self.config.maxCorrection then
        self:DisableLeg(leg)
        return
    end

    local alpha = clamp01((dt or 0.0) * self.config.interpSpeed)
    if leg.targetZ == nil then
        leg.targetZ = desiredZ
    else
        leg.targetZ = leg.targetZ + (desiredZ - leg.targetZ) * alpha
    end

    local target = Vector.new(footWorld.X, footWorld.Y, leg.targetZ)
    self.mesh:SetIKTargetPosition(leg.chainIndex, target)
    self.mesh:SetIKChainEnabled(leg.chainIndex, true)
end

function FootIK:Tick(dt)
    if not self.enabled or self.mesh == nil then
        return
    end

    if not isGrounded(self.movement) then
        self:Disable()
        return
    end

    local speed = getSpeed2D(self.movement)
    local isMoving = speed > self.config.movingSpeedThreshold
    for _, leg in pairs(self.legs) do
        self:UpdateLeg(leg, dt, isMoving)
    end
end

return FootIK
