local AnomalyManager = {}

local PhotoInvisible = require("Anomalies/PhotoInvisible")
local NoShadow = require("Anomalies/NoShadow")
local OffscreenAnimation = require("Anomalies/OffscreenAnimation")
local OffscreenFacePlayer = require("Anomalies/OffscreenFacePlayer")
local PhotoLookAtInvisible = require("Anomalies/PhotoLookAtInvisible")
local PhotoLookAtBlackPhoto = require("Anomalies/PhotoLookAtBlackPhoto")
local BlackPhoto = require("Anomalies/BlackPhoto")
local PhotoGhostReplacement = require("Anomalies/PhotoGhostReplacement")
local PhotoBoneTwist = require("Anomalies/PhotoBoneTwist")
local NearSilentCymbalMonkey = require("Anomalies/NearSilentCymbalMonkey")

AnomalyManager.Tags = {
    Candidate = "AnomalyCandidate",
    ActiveTarget = "ActiveAnomalyTarget",
    PhotoInvisible = "PhotoInvisible",
    PhotoBlackoutTarget = "PhotoBlackoutTarget",
    PhotoGhostReplacementTarget = "PhotoGhostReplacementTarget",
    PhotoGhostReplacementActor = "PhotoGhostReplacementActor",
    PhotoBoneTwistTarget = "PhotoBoneTwistTarget"
}

AnomalyManager.Rules = {
    PhotoInvisible,
    PhotoLookAtInvisible,
    PhotoLookAtBlackPhoto,
    BlackPhoto,
    PhotoGhostReplacement,
    PhotoBoneTwist
}

AnomalyManager.AllRules = {
    PhotoInvisible,
    NoShadow,
    OffscreenAnimation,
    OffscreenFacePlayer,
    PhotoLookAtInvisible,
    PhotoLookAtBlackPhoto,
    BlackPhoto,
    PhotoGhostReplacement,
    PhotoBoneTwist,
    NearSilentCymbalMonkey
}

AnomalyManager.Active = nil
AnomalyManager.LastError = nil
AnomalyManager.RandomState = nil
AnomalyManager.RandomDrawCount = 0

local COLLISION_NO_COLLISION = 0
local SHOT_RAGDOLL_IMPULSE_STRENGTH = 0.35
local RANDOM_MODULUS = 2147483647
local RANDOM_MULTIPLIER = 48271

local function make_seed(timeSeconds)
    local rawSeed = math.floor((tonumber(timeSeconds) or 0) * 1000000)
    if rawSeed <= 0 then
        return nil
    end
    return (rawSeed % 2147483646) + 1
end

local function make_real_time_seed()
    local seed = nil
    if World ~= nil and World.GetRealTimeSeconds ~= nil then
        seed = make_seed(World.GetRealTimeSeconds())
    end

    return seed
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

local function get_rule_name(rule)
    return rule and rule.Name or "Unknown"
end

local function get_actor_name(actor)
    if actor == nil then
        return "nil"
    end

    if actor.GetName ~= nil then
        local ok, name = pcall(function()
            return actor:GetName()
        end)
        if ok and name ~= nil and tostring(name) ~= "" then
            return tostring(name)
        end
    end

    if actor.Name ~= nil then
        return tostring(actor.Name)
    end

    return "UnknownActor"
end

local function get_actor_uuid(actor)
    if actor == nil or actor.GetUUID == nil then
        return "nil"
    end

    local ok, uuid = pcall(function()
        return actor:GetUUID()
    end)
    if ok and uuid ~= nil then
        return tostring(uuid)
    end

    return "nil"
end

local function log_setting(message)
    print("[AnomalyManager] " .. tostring(message))
end

function AnomalyManager:_EnsureRandomState()
    if self.RandomState ~= nil then
        return self.RandomState
    end

    local seed = make_real_time_seed()
    if seed == nil then
        seed = 1
    end

    self.RandomState = seed
    self.RandomDrawCount = 0
    log_setting("Random seed=" .. tostring(seed))
    return self.RandomState
end

function AnomalyManager:_RandomUnit()
    local state = self:_EnsureRandomState()
    state = (state * RANDOM_MULTIPLIER) % RANDOM_MODULUS
    if state <= 0 then
        state = 1
    end

    self.RandomState = state
    self.RandomDrawCount = self.RandomDrawCount + 1
    return state / RANDOM_MODULUS
end

function AnomalyManager:_RandomIndex(count)
    count = tonumber(count) or 0
    if count <= 0 then
        return nil
    end

    local index = math.floor(self:_RandomUnit() * count) + 1
    if index > count then
        index = count
    end
    return index
end

local function get_skeletal_mesh(actor)
    if actor == nil or actor.GetSkeletalMeshComponent == nil then
        return nil
    end

    local ok, mesh = pcall(function()
        return actor:GetSkeletalMeshComponent()
    end)
    if not ok then
        return nil
    end
    return mesh
end

local function get_shot_hit_location(actor, hit)
    if hit ~= nil and hit.Location ~= nil then
        return hit.Location
    end

    if actor ~= nil and actor.GetLocation ~= nil then
        local ok, location = pcall(function()
            return actor:GetLocation()
        end)
        if ok then
            return location
        end
    end

    return nil
end

local function get_shot_direction(hit)
    if hit ~= nil and hit.ShotDirection ~= nil then
        return hit.ShotDirection
    end

    if hit ~= nil and hit.Normal ~= nil then
        return Vec3(-hit.Normal.X, -hit.Normal.Y, -hit.Normal.Z)
    end

    return nil
end

local function apply_shot_ragdoll(actor, hit)
    local mesh = get_skeletal_mesh(actor)
    if mesh == nil or mesh.EnableRagdollPhysics == nil then
        return false
    end

    local ok, result = pcall(function()
        return mesh:EnableRagdollPhysics()
    end)
    local bRagdollEnabled = ok and result == true
    if bRagdollEnabled and mesh.SetCollisionEnabled ~= nil then
        pcall(function()
            mesh:SetCollisionEnabled(COLLISION_NO_COLLISION)
        end)
    end

    if mesh.ApplyRagdollImpulse == nil then
        return bRagdollEnabled
    end

    local location = get_shot_hit_location(actor, hit)
    local direction = get_shot_direction(hit)
    if location == nil or direction == nil then
        return bRagdollEnabled
    end

    pcall(function()
        mesh:ApplyRagdollImpulse(location, direction, SHOT_RAGDOLL_IMPULSE_STRENGTH)
    end)
    return bRagdollEnabled
end

local function safe_call(rule, function_name, context)
    local fn = rule and rule[function_name]
    if type(fn) ~= "function" then
        return true
    end

    local ok, result, message = pcall(fn, rule, context)
    if not ok then
        return false, result
    end
    return result ~= false, message
end

function AnomalyManager:_BuildContext(target, rule)
    return {
        Manager = self,
        Target = target,
        Rule = rule,
        Tags = self.Tags,
        RandomIndex = function(count)
            return self:_RandomIndex(count)
        end,
        State = {}
    }
end

function AnomalyManager:_GetCandidates()
    local candidates = {}
    if World == nil or World.FindActorsByTag == nil then
        self.LastError = "World.FindActorsByTag unavailable"
        return candidates
    end

    local found = World.FindActorsByTag(self.Tags.Candidate)
    if found == nil then
        return candidates
    end

    for _, actor in pairs(found) do
        if is_valid_actor(actor) then
            table.insert(candidates, actor)
        end
    end

    return candidates
end

function AnomalyManager:_FindRuleByName(ruleName)
    if ruleName == nil then
        return nil
    end

    for _, rule in ipairs(self.AllRules) do
        if get_rule_name(rule) == ruleName then
            return rule
        end
    end

    return nil
end

function AnomalyManager:_ActivateRule(target, rule, source)
    source = source or "Unknown"
    log_setting("Setup try source=" .. tostring(source) ..
        " rule=" .. get_rule_name(rule) ..
        " target=" .. get_actor_name(target) ..
        " uuid=" .. get_actor_uuid(target))

    local context = self:_BuildContext(target, rule)

    local ok, message = safe_call(rule, "Spawn", context)
    if not ok then
        self.LastError = "Spawn failed: " .. get_rule_name(rule) .. " target=" .. get_actor_name(target) .. " reason=" .. tostring(message)
        return false
    end

    local hadActiveTag = target:HasTag(self.Tags.ActiveTarget)
    if not hadActiveTag then
        target:AddTag(self.Tags.ActiveTarget)
    end

    self.Active = {
        Target = target,
        Rule = rule,
        Context = context,
        AddedActiveTag = not hadActiveTag,
        bCleared = false
    }

    log_setting("Setup success source=" .. tostring(source) ..
        " rule=" .. get_rule_name(rule) ..
        " target=" .. get_actor_name(target) ..
        " uuid=" .. get_actor_uuid(target))
    return true
end

function AnomalyManager:HasActiveAnomaly()
    return self.Active ~= nil and is_valid_actor(self.Active.Target)
end

function AnomalyManager:GetActiveTarget()
    if not self:HasActiveAnomaly() then
        return nil
    end
    return self.Active.Target
end

function AnomalyManager:GetActiveRuleName()
    if self.Active == nil then
        return nil
    end
    return get_rule_name(self.Active.Rule)
end

function AnomalyManager:GetLastError()
    return self.LastError
end

function AnomalyManager:DespawnCurrent(reason)
    local active = self.Active
    self.Active = nil

    if active == nil then
        return false
    end

    log_setting("Despawn reason=" .. tostring(reason) ..
        " rule=" .. get_rule_name(active.Rule) ..
        " target=" .. get_actor_name(active.Target) ..
        " uuid=" .. get_actor_uuid(active.Target))

    if is_valid_actor(active.Target) and active.AddedActiveTag then
        active.Target:RemoveTag(self.Tags.ActiveTarget)
    end

    local context = active.Context
    if context ~= nil then
        context.Reason = reason
        safe_call(active.Rule, "Despawn", context)
    end

    return true
end

function AnomalyManager:SelectAndSpawn()
    self:DespawnCurrent("SelectAndSpawn")
    self.LastError = nil

    local candidates = self:_GetCandidates()
    if #candidates <= 0 then
        self.LastError = "AnomalyCandidate tag actor not found"
        print("[AnomalyManager] " .. self.LastError)
        return false
    end

    if #self.Rules <= 0 then
        self.LastError = "Anomaly rule pool is empty"
        print("[AnomalyManager] " .. self.LastError)
        return false
    end

    local targetIndex = self:_RandomIndex(#candidates)
    local ruleIndex = self:_RandomIndex(#self.Rules)
    local target = candidates[targetIndex]
    local rule = self.Rules[ruleIndex]
    log_setting("Setup begin source=Random candidates=" .. tostring(#candidates) .. " rules=" .. tostring(#self.Rules))
    log_setting("Random selected targetIndex=" .. tostring(targetIndex) ..
        " ruleIndex=" .. tostring(ruleIndex) ..
        " drawCount=" .. tostring(self.RandomDrawCount))
    if not self:_ActivateRule(target, rule, "Random") then
        print("[AnomalyManager] " .. self.LastError)
        return false
    end

    return true
end

function AnomalyManager:SelectAndSpawnRule(ruleName)
    self:DespawnCurrent("SelectAndSpawnRule")
    self.LastError = nil

    local rule = self:_FindRuleByName(ruleName)
    if rule == nil then
        self.LastError = "Anomaly rule not found: " .. tostring(ruleName)
        print("[AnomalyManager] " .. self.LastError)
        return false
    end

    local candidates = self:_GetCandidates()
    if #candidates <= 0 then
        self.LastError = "AnomalyCandidate tag actor not found"
        print("[AnomalyManager] " .. self.LastError)
        return false
    end

    local startIndex = self:_RandomIndex(#candidates)
    log_setting("Setup begin source=Debug rule=" .. tostring(ruleName) .. " candidates=" .. tostring(#candidates))
    for offset = 0, #candidates - 1 do
        local index = ((startIndex + offset - 1) % #candidates) + 1
        if self:_ActivateRule(candidates[index], rule, "Debug") then
            return true
        end
    end

    print("[AnomalyManager] " .. self.LastError)
    return false
end

function AnomalyManager:Tick(dt)
    local active = self.Active
    if active == nil then
        return
    end

    if not is_valid_actor(active.Target) then
        self:DespawnCurrent("TargetInvalid")
        return
    end

    if active.bCleared then
        return
    end

    active.Context.DeltaTime = dt
    safe_call(active.Rule, "Tick", active.Context)

    local ok, cleared = pcall(function()
        if type(active.Rule.IsCleared) ~= "function" then
            return false
        end
        return active.Rule:IsCleared(active.Context)
    end)

    if ok and cleared then
        self:OnClear(active, "RuleCleared")
    end
end

function AnomalyManager:OnClear(active, reason)
    if active == nil or active.bCleared then
        return false
    end

    active.bCleared = true
    if active.Context ~= nil and active.Context.State ~= nil then
        active.Context.State.bCleared = true
        active.Context.ClearReason = reason or "Clear"
    end
    return true
end

function AnomalyManager:ReportShot(actor, hit)
    if actor == nil or self.Active == nil then
        return false
    end

    local active = self.Active
    if not is_valid_actor(active.Target) then
        self:DespawnCurrent("TargetInvalid")
        return false
    end

    local bHitActiveTarget = actor == active.Target
    if not bHitActiveTarget and actor.HasTag ~= nil then
        bHitActiveTarget = actor:HasTag(self.Tags.ActiveTarget)
    end

    if not bHitActiveTarget then
        return false
    end

    apply_shot_ragdoll(actor, hit)
    return self:OnClear(active, "Shot")
end

function AnomalyManager:NotifyPhotoCaptureRequested()
    local active = self.Active
    if active == nil or active.bCleared then
        return false
    end

    if not is_valid_actor(active.Target) then
        self:DespawnCurrent("TargetInvalid")
        return false
    end

    local ok, message = safe_call(active.Rule, "OnPhotoCapture", active.Context)
    if not ok then
        self.LastError = "OnPhotoCapture failed: " .. get_rule_name(active.Rule) .. " reason=" .. tostring(message)
        print("[AnomalyManager] " .. self.LastError)
        return false
    end

    return true
end

function AnomalyManager:Reset()
    self:DespawnCurrent("Reset")
    self.LastError = nil
end

return AnomalyManager
