local GameManager = {}
local AnomalyManager = require("AnomalyManager")
local LeaderboardManager = require("LeaderboardManager")
local PlacementManager = require("PlacementManager")
local JumpScareManager = require("JumpScareManager")
local LoopManager = require("LoopManager")

GameManager.State = {
    Ready = "Ready",
    Playing = "Playing",
    Paused = "Paused",
    GameOver = "GameOver",
    Clear = "Clear",
    Ending = "Ending"
}

GameManager.Pressure = {
    EntryStrike = 1,
    Warning = 2,
    FinalWarning = 3
}

GameManager.state = GameManager.State.Ready
GameManager.score = 0
GameManager.elapsedTime = 0
GameManager.totalGameTime = 0
GameManager.remainingTime = 0
GameManager.timeLimit = nil
GameManager.isPlayerDead = false
GameManager.bLoopStopped = false
GameManager.bCymbalMonkeyCycleStarted = false
GameManager.pressureStage = GameManager.Pressure.EntryStrike
GameManager.manualPressureStage = nil
GameManager.maxPlayerBulletsPerStage = 3
GameManager.playerBulletsRemaining = GameManager.maxPlayerBulletsPerStage
GameManager.failedShotCount = 0
GameManager.maxFailedShotsBeforeGameOver = 3
GameManager.bFailureTimeDrainActive = false
GameManager.failureTimeDrainDuration = 3.0
GameManager.failureTimeDrainElapsed = 0.0
GameManager.failureTimeDrainStartRemaining = 0.0
GameManager.AnomalyPlacementTemplateSetName = "Runtime"
GameManager.AnomalyPlacementTemplateExtension = ".ActorTemplate"
GameManager.AnomalyPlacementTemplateSets = {
    Debug = {
        Directory = "Content/Blueprint/AnomaliesPlacement/Debug",
        Recursive = false
    },
    Runtime = {
        Directory = "Content/Blueprint/AnomaliesPlacement/Runtime",
        Recursive = false
    }
}
GameManager.ActiveAnomalyPlacementRecord = nil
GameManager.LastAnomalyPlacementError = nil
GameManager.OutlinedAnomalyTarget = nil
GameManager.AnomalyHitEffectTag = "AnomalyHitEffect"
GameManager.AnomalyHitEffectActor = nil
GameManager.AnomalyHitEffectComponent = nil

GameManager._listeners = {
    StateChanged = {},
    ScoreChanged = {},
    PlayerDead = {},
    TimeExpired = {},
    PressureChanged = {},
    LoopStopped = {},
    LoopRested = {},
    CymbalMonkeyCycleStarted = {},
    CymbalMonkeyCycleReset = {}
}

local WARNING_REMAINING_RATIO = 0.1
local FINAL_WARNING_REMAINING_RATIO = 0.1
local ANOMALY_PLACEMENT_RECORD_ID = "GameManager_AnomalyPlacement"
local bAnomalyPlacementRandomSeeded = false

local function clamp_score(value)
    value = tonumber(value) or 0
    if value < 0 then
        return 0
    end
    return value
end

local function is_function(value)
    return type(value) == "function"
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

local function get_particle_system_component(actor)
    if not is_valid_actor(actor) or actor.GetParticleSystemComponent == nil then
        return nil
    end

    local ok, component = pcall(function()
        return actor:GetParticleSystemComponent()
    end)
    if not ok then
        return nil
    end
    return component
end

local function find_particle_actor_by_tag(tag)
    if World == nil or World.FindActorsByTag == nil then
        return nil, nil
    end

    local ok, actors = pcall(function()
        return World.FindActorsByTag(tag)
    end)
    if not ok or actors == nil then
        return nil, nil
    end

    for _, actor in pairs(actors) do
        if actor.GetClassName ~= nil then
            local classOk, className = pcall(function()
                return actor:GetClassName()
            end)
            if classOk and className == "AParticleSystemActor" then
                local component = get_particle_system_component(actor)
                if component ~= nil then
                    return actor, component
                end
            end
        end
    end

    return nil, nil
end

local function get_hit_location(actor, hit)
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

local function normalize_pressure(pressure)
    pressure = tonumber(pressure)
    if pressure == nil then
        return nil
    end

    pressure = math.floor(pressure)
    if pressure < GameManager.Pressure.EntryStrike or pressure > GameManager.Pressure.FinalWarning then
        return nil
    end

    return pressure
end

local function make_random_seed(timeSeconds)
    local rawSeed = math.floor((tonumber(timeSeconds) or 0) * 1000000)
    if rawSeed <= 0 then
        return nil
    end
    return (rawSeed % 2147483646) + 1
end

local function seed_anomaly_placement_random_once()
    if bAnomalyPlacementRandomSeeded then
        return
    end

    local seed = nil
    if World ~= nil and World.GetRealTimeSeconds ~= nil then
        seed = make_random_seed(World.GetRealTimeSeconds())
    end

    if seed ~= nil then
        math.randomseed(seed)
        bAnomalyPlacementRandomSeeded = true
    end
end

local function ease_out_cubic(value)
    if value < 0.0 then
        value = 0.0
    elseif value > 1.0 then
        value = 1.0
    end

    local inverse = 1.0 - value
    return 1.0 - inverse * inverse * inverse
end

function GameManager:_GetRemainingRatio()
    local timeLimit = tonumber(self.timeLimit)
    if timeLimit == nil or timeLimit <= 0 then
        return 1.0
    end

    local remainingTime = tonumber(self.remainingTime) or timeLimit
    local ratio = remainingTime / timeLimit
    if ratio < 0.0 then
        return 0.0
    end
    if ratio > 1.0 then
        return 1.0
    end
    return ratio
end

function GameManager:_GetPressureStageFromTime()
    local remainingRatio = self:_GetRemainingRatio()

    if remainingRatio <= FINAL_WARNING_REMAINING_RATIO then
        return self.Pressure.FinalWarning
    end
    if remainingRatio <= WARNING_REMAINING_RATIO then
        return self.Pressure.Warning
    end
    return self.Pressure.EntryStrike
end

function GameManager:_ResolvePressureStage()
    local manualPressure = normalize_pressure(self.manualPressureStage)
    if manualPressure ~= nil then
        return manualPressure
    end
    return self:_GetPressureStageFromTime()
end

function GameManager:_FireEvent(eventName, ...)
    local listeners = self._listeners[eventName]
    if listeners == nil then
        return
    end

    for i = #listeners, 1, -1 do
        local callback = listeners[i]
        if is_function(callback) then
            local ok, err = pcall(callback, ...)
            if not ok then
                print("[GameManager] " .. eventName .. " callback error: " .. tostring(err))
            end
        else
            table.remove(listeners, i)
        end
    end
end

function GameManager:_ResetAnomalyHitEffectCache()
    if self.AnomalyHitEffectComponent ~= nil and self.AnomalyHitEffectComponent.Deactivate ~= nil then
        pcall(function()
            self.AnomalyHitEffectComponent:Deactivate()
        end)
    end

    self.AnomalyHitEffectActor = nil
    self.AnomalyHitEffectComponent = nil
end

function GameManager:_CacheAnomalyHitEffect()
    if is_valid_actor(self.AnomalyHitEffectActor) and self.AnomalyHitEffectComponent ~= nil then
        return true
    end

    self.AnomalyHitEffectActor, self.AnomalyHitEffectComponent = find_particle_actor_by_tag(self.AnomalyHitEffectTag)
    if self.AnomalyHitEffectComponent == nil then
        self.AnomalyHitEffectActor = nil
        return false
    end

    if self.AnomalyHitEffectComponent.Deactivate ~= nil then
        pcall(function()
            self.AnomalyHitEffectComponent:Deactivate()
        end)
    end
    return true
end

function GameManager:_PlayAnomalyHitEffect(actor, hit)
    local location = get_hit_location(actor, hit)
    if location == nil then
        return false
    end
    if not self:_CacheAnomalyHitEffect() then
        return false
    end

    if self.AnomalyHitEffectActor ~= nil and self.AnomalyHitEffectActor.SetLocation ~= nil then
        pcall(function()
            self.AnomalyHitEffectActor:SetLocation(location)
        end)
    end

    local component = self.AnomalyHitEffectComponent
    if component == nil then
        return false
    end
    if component.Deactivate ~= nil then
        pcall(function()
            component:Deactivate()
        end)
    end
    if component.ResetParticles ~= nil then
        pcall(function()
            component:ResetParticles()
        end)
    end
    if component.Activate ~= nil then
        pcall(function()
            component:Activate()
        end)
    end
    return true
end

function GameManager:_SetState(nextState, reason)
    if self.state == nextState then
        return false
    end

    local previousState = self.state
    self.state = nextState
    self:_FireEvent("StateChanged", nextState, previousState, reason)
    return true
end

function GameManager:_SetPressureStage(nextStage, reason, forceNotify)
    nextStage = normalize_pressure(nextStage) or self.Pressure.EntryStrike

    local previousStage = self.pressureStage
    self.pressureStage = nextStage

    if forceNotify or previousStage ~= nextStage then
        self:_FireEvent("PressureChanged", nextStage, previousStage, reason)
        return true
    end

    return false
end

function GameManager:_RefreshPressureStage(reason, forceNotify)
    return self:_SetPressureStage(self:_ResolvePressureStage(), reason, forceNotify)
end

function GameManager:_ResetPlayerBulletsForStage()
    self.playerBulletsRemaining = self.maxPlayerBulletsPerStage
    self.failedShotCount = 0
    self:_ResetFailureTimeDrain()
end

function GameManager:_ResetFailureTimeDrain()
    self.bFailureTimeDrainActive = false
    self.failureTimeDrainElapsed = 0.0
    self.failureTimeDrainStartRemaining = 0.0
end

function GameManager:_StartFailureTimeDrain(reason)
    if self.bFailureTimeDrainActive then
        return false
    end

    self.bFailureTimeDrainActive = true
    self.failureTimeDrainElapsed = 0.0
    self.failureTimeDrainStartRemaining = math.max(0.0, tonumber(self.remainingTime) or 0.0)
    self:_SetPressureStage(self.Pressure.FinalWarning, reason or "FailedShots", true)
    return true
end

function GameManager:_TickFailureTimeDrain(dt)
    if not self.bFailureTimeDrainActive then
        return false
    end

    local duration = tonumber(self.failureTimeDrainDuration) or 0.0
    if duration <= 0.0 then
        self.remainingTime = 0
    else
        self.failureTimeDrainElapsed = self.failureTimeDrainElapsed + dt
        local alpha = self.failureTimeDrainElapsed / duration
        local drainedRatio = ease_out_cubic(alpha)
        self.remainingTime = self.failureTimeDrainStartRemaining * (1.0 - drainedRatio)
        if self.remainingTime < 0.0 then
            self.remainingTime = 0
        end
    end

    self:_RefreshPressureStage("FailureTimeDrain", false)

    if self.remainingTime <= 0.0 then
        self.remainingTime = 0
        self:_FireEvent("TimeExpired")
        self:GameOver("FailedShots")
    end

    return true
end

function GameManager:_ClearAnomalyPlacement()
    local record = self.ActiveAnomalyPlacementRecord
    self.ActiveAnomalyPlacementRecord = nil

    if record == nil then
        return false
    end

    PlacementManager:Destroy(record)
    return true
end

function GameManager:_SetAnomalyTargetOutline(actor, bEnabled)
    if actor == nil or actor.SetGameplayOutline == nil then
        return false
    end

    local ok, result = pcall(function()
        return actor:SetGameplayOutline(bEnabled == true)
    end)
    return ok and result ~= false
end

function GameManager:ClearActiveAnomalyOutline()
    local target = self.OutlinedAnomalyTarget
    self.OutlinedAnomalyTarget = nil

    if target == nil then
        return false
    end

    self:_SetAnomalyTargetOutline(target, false)
    return true
end

function GameManager:SetActiveAnomalyOutlineVisible(bVisible)
    if bVisible ~= true then
        return self:ClearActiveAnomalyOutline()
    end

    local target = AnomalyManager:GetActiveTarget()
    if target == nil then
        self:ClearActiveAnomalyOutline()
        return false
    end

    if self.OutlinedAnomalyTarget == target then
        if not self:_SetAnomalyTargetOutline(target, true) then
            print("[GameManager] Active anomaly target outline failed")
            return false
        end
        return true
    end

    self:ClearActiveAnomalyOutline()
    if not self:_SetAnomalyTargetOutline(target, true) then
        print("[GameManager] Active anomaly target outline failed")
        return false
    end

    self.OutlinedAnomalyTarget = target
    return true
end

function GameManager:_GetAnomalyPlacementTemplateSet()
    local setName = self.AnomalyPlacementTemplateSetName
    local templateSet = self.AnomalyPlacementTemplateSets[setName]
    if templateSet == nil then
        self.LastAnomalyPlacementError = "Anomaly placement template set not found: " .. tostring(setName)
        return nil
    end

    if type(templateSet.Directory) ~= "string" or templateSet.Directory == "" then
        self.LastAnomalyPlacementError = "Anomaly placement directory is empty: " .. tostring(setName)
        return nil
    end

    return templateSet
end

function GameManager:_FindAnomalyPlacementTemplates(templateSet)
    if World == nil or World.FindFilesByExtension == nil then
        self.LastAnomalyPlacementError = "World.FindFilesByExtension unavailable"
        return nil
    end

    local templates = World.FindFilesByExtension(
        templateSet.Directory,
        self.AnomalyPlacementTemplateExtension,
        templateSet.Recursive == true
    )

    if type(templates) ~= "table" then
        self.LastAnomalyPlacementError = "Anomaly placement template query failed"
        return nil
    end

    if #templates <= 0 then
        self.LastAnomalyPlacementError = "Anomaly placement template not found: " .. tostring(templateSet.Directory)
        return nil
    end

    return templates
end

function GameManager:_SpawnRandomAnomalyPlacement(reason)
    seed_anomaly_placement_random_once()

    local templateSet = self:_GetAnomalyPlacementTemplateSet()
    if templateSet == nil then
        return false
    end

    local templates = self:_FindAnomalyPlacementTemplates(templateSet)
    if templates == nil then
        return false
    end

    local templatePath = templates[math.random(1, #templates)]
    local record, message = PlacementManager:Spawn(templatePath, {
        Id = ANOMALY_PLACEMENT_RECORD_ID
    })
    if record == nil then
        self.LastAnomalyPlacementError = "Anomaly placement spawn failed: " .. tostring(message or templatePath)
        return false
    end

    self.ActiveAnomalyPlacementRecord = record
    self.LastAnomalyPlacementError = nil
    return true
end

function GameManager:_SetupAnomaly(reason)
    reason = reason or "SetupAnomaly"
    self:ClearActiveAnomalyOutline()
    self:_ResetPlayerBulletsForStage()
    AnomalyManager:DespawnCurrent(reason)
    self:_ClearAnomalyPlacement()
    local bPlacementReady = self:_SpawnRandomAnomalyPlacement(reason)
    local bAnomalyReady = AnomalyManager:SelectAndSpawn()
    return bPlacementReady and bAnomalyReady
end

function GameManager:AddListener(eventName, callback)
    if not is_function(callback) then
        print("[GameManager] AddListener failed: callback must be a function")
        return nil
    end

    local listeners = self._listeners[eventName]
    if listeners == nil then
        print("[GameManager] AddListener failed: unknown event " .. tostring(eventName))
        return nil
    end

    table.insert(listeners, callback)
    return callback
end

function GameManager:RemoveListener(eventName, callback)
    local listeners = self._listeners[eventName]
    if listeners == nil or callback == nil then
        return false
    end

    for i = #listeners, 1, -1 do
        if listeners[i] == callback then
            table.remove(listeners, i)
            return true
        end
    end

    return false
end

function GameManager:OnStateChanged(callback)
    return self:AddListener("StateChanged", callback)
end

function GameManager:OnScoreChanged(callback)
    return self:AddListener("ScoreChanged", callback)
end

function GameManager:OnPlayerDead(callback)
    return self:AddListener("PlayerDead", callback)
end

function GameManager:OnTimeExpired(callback)
    return self:AddListener("TimeExpired", callback)
end

function GameManager:OnPressureChanged(callback)
    return self:AddListener("PressureChanged", callback)
end

function GameManager:OnLoopStopped(callback)
    return self:AddListener("LoopStopped", callback)
end

function GameManager:OnLoopRested(callback)
    return self:AddListener("LoopRested", callback)
end

function GameManager:OnCymbalMonkeyCycleStarted(callback)
    return self:AddListener("CymbalMonkeyCycleStarted", callback)
end

function GameManager:OnCymbalMonkeyCycleReset(callback)
    return self:AddListener("CymbalMonkeyCycleReset", callback)
end

function GameManager:IsCymbalMonkeyCycleStarted()
    return LoopManager:IsCymbalMonkeyCycleStarted()
end

function GameManager:IsCymbalDoorTriggerUsed()
    return LoopManager:IsCymbalDoorTriggerUsed()
end

function GameManager:StartCymbalMonkeyCycle()
    local bStarted = LoopManager:StartCymbalMonkeyCycle(self)
    self.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    if bStarted
        and LoopManager:GetWarpCount() == 0
        and not LoopManager:HasConsumedFirstTimerChaos() then
        local bScheduled = require("DoorManager"):TryScheduleChaosSingleDoorToggles("first_timer")
        if bScheduled then
            LoopManager:MarkFirstTimerChaosConsumed()
        end
    end
    return bStarted
end

function GameManager:ResetCymbalMonkeyCycle()
    local bWasStarted = LoopManager:ResetCymbalMonkeyCycle(self, "ResetCymbalMonkeyCycle")
    self.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    return bWasStarted
end

function GameManager:IsLoopStopped()
    return LoopManager:IsLoopStopped()
end

function GameManager:StopLoop(reason)
    local bStopped = LoopManager:StopLoop(self, reason)
    self.bLoopStopped = LoopManager:IsLoopStopped()
    self.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    return bStopped
end

function GameManager:RestLoop(reason)
    reason = reason or "RestLoop"
    return self:OnLoopStart(reason)
end

function GameManager:Reset()
    self:ClearActiveAnomalyOutline()
    self:_ResetAnomalyHitEffectCache()
    AnomalyManager:Reset()
    JumpScareManager:DeactivateAll()
    self:_ClearAnomalyPlacement()
    self.LastAnomalyPlacementError = nil
    self.score = 0
    self.elapsedTime = 0
    self.totalGameTime = 0
    self.remainingTime = self.timeLimit or 0
    self.isPlayerDead = false
    self:_ResetPlayerBulletsForStage()
    LoopManager:Reset()
    self.bLoopStopped = LoopManager:IsLoopStopped()
    self.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    self.manualPressureStage = nil
    self:_SetPressureStage(self.Pressure.EntryStrike, "Reset", false)
    require("EndingManager"):Reset()
    self:_SetState(self.State.Ready, "Reset")
end

function GameManager:StartGame()
    self.elapsedTime = 0
    self.totalGameTime = 0
    self.remainingTime = self.timeLimit or 0
    self.isPlayerDead = false
    self.failedShotCount = 0
    self:_ResetFailureTimeDrain()
    self:_ResetAnomalyHitEffectCache()
    self:_CacheAnomalyHitEffect()
    LoopManager:StartStopped()
    self.bLoopStopped = LoopManager:IsLoopStopped()
    self.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    self:_SetState(self.State.Playing, "StartGame")
    self:_RefreshPressureStage("StartGame", true)
    self:_SetupAnomaly("StartGame")
end

function GameManager:PauseGame()
    if self.state ~= self.State.Playing then
        return false
    end

    return self:_SetState(self.State.Paused, "PauseGame")
end

function GameManager:ResumeGame()
    if self.state ~= self.State.Paused then
        return false
    end

    return self:_SetState(self.State.Playing, "ResumeGame")
end

function GameManager:GameOver(reason)
    if self.state == self.State.GameOver then
        return false
    end

    self:ClearActiveAnomalyOutline()
    AnomalyManager:Reset()
    JumpScareManager:DeactivateAll()
    self:_ClearAnomalyPlacement()
    self.LastAnomalyPlacementError = nil
    self:_ResetFailureTimeDrain()
    LoopManager:Reset()
    self.bLoopStopped = LoopManager:IsLoopStopped()
    self.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    self:_SetPressureStage(self.Pressure.EntryStrike, reason or "GameOver", false)
    return self:_SetState(self.State.GameOver, reason or "GameOver")
end

function GameManager:ClearGame(reason)
    if self.state == self.State.Clear then
        return false
    end

    local clearReason = reason or "ClearGame"
    local createdAtSeconds = 0
    if World ~= nil and World.GetRealTimeSeconds ~= nil then
        createdAtSeconds = tonumber(World.GetRealTimeSeconds()) or 0
    end

    LeaderboardManager:AddClearRecord({
        TotalTimeSeconds = self.totalGameTime,
        ElapsedTimeSeconds = self.elapsedTime,
        Score = self.score,
        ClearReason = clearReason,
        CreatedAtSeconds = createdAtSeconds
    })

    self:ClearActiveAnomalyOutline()
    AnomalyManager:Reset()
    JumpScareManager:DeactivateAll()
    self:_ClearAnomalyPlacement()
    self.LastAnomalyPlacementError = nil
    self:_ResetFailureTimeDrain()
    LoopManager:Reset()
    self.bLoopStopped = LoopManager:IsLoopStopped()
    self.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    self:_SetPressureStage(self.Pressure.EntryStrike, clearReason, false)
    return self:_SetState(self.State.Clear, clearReason)
end

function GameManager:RestartGame()
    self:Reset()
    self:StartGame()
end

function GameManager:Tick(dt)
    if self.state == self.State.Ending then
        return
    end

    if self.state ~= self.State.Playing then
        return
    end

    dt = tonumber(dt) or 0
    if dt < 0 then
        dt = 0
    end

    self.totalGameTime = self.totalGameTime + dt

    AnomalyManager:Tick(dt)
    if self:_TickFailureTimeDrain(dt) then
        return
    end

    if LoopManager:IsLoopStopped() then
        return
    end

    if not LoopManager:IsCymbalMonkeyCycleStarted() then
        return
    end

    self.elapsedTime = self.elapsedTime + dt

    if self.timeLimit ~= nil then
        self.remainingTime = self.remainingTime - dt
        if self.remainingTime <= 0 then
            self.remainingTime = 0
            self:_RefreshPressureStage("Tick", false)
            self:_FireEvent("TimeExpired")
            self:GameOver("TimeUp")
            return
        end
    end

    self:_RefreshPressureStage("Tick", false)
    require("DoorManager"):UpdateChaosSingleDoorToggles(self.remainingTime)
end

function GameManager:AddScore(amount)
    amount = tonumber(amount) or 0
    return self:SetScore(self.score + amount)
end

function GameManager:SetScore(value)
    local previousScore = self.score
    self.score = clamp_score(value)

    if previousScore ~= self.score then
        self:_FireEvent("ScoreChanged", self.score, previousScore)
    end

    return self.score
end

function GameManager:GetScore()
    return self.score
end

function GameManager:SetTimeLimit(seconds)
    seconds = tonumber(seconds)
    if seconds == nil or seconds <= 0 then
        self:ClearTimeLimit()
        return
    end

    self.timeLimit = seconds
    self.remainingTime = seconds
    if self.state == self.State.Playing then
        self:_RefreshPressureStage("SetTimeLimit", false)
    end
end

function GameManager:ClearTimeLimit()
    self.timeLimit = nil
    self.remainingTime = 0
    if self.state == self.State.Playing then
        self:_RefreshPressureStage("ClearTimeLimit", false)
    end
end

function GameManager:GetElapsedTime()
    return self.elapsedTime
end

function GameManager:GetTotalGameTime()
    return self.totalGameTime
end

function GameManager:GetLeaderboardEntries()
    return LeaderboardManager:GetEntries()
end

function GameManager:GetLeaderboardEntryCount()
    return LeaderboardManager:GetEntryCount()
end

function GameManager:GetLeaderboardEntry(index)
    return LeaderboardManager:GetEntry(index)
end

function GameManager:GetBestLeaderboardEntry()
    return LeaderboardManager:GetBestEntry()
end

function GameManager:GetLastLeaderboardRecord()
    return LeaderboardManager:GetLastRecord()
end

function GameManager:SerializeLeaderboard()
    return LeaderboardManager:Serialize()
end

function GameManager:DeserializeLeaderboard(text)
    return LeaderboardManager:Deserialize(text)
end

function GameManager:LoadLeaderboard()
    return LeaderboardManager:Load()
end

function GameManager:SaveLeaderboard()
    return LeaderboardManager:Save()
end

function GameManager:ReloadLeaderboard()
    return LeaderboardManager:Reload()
end

function GameManager:ClearSavedLeaderboard()
    return LeaderboardManager:ClearSavedRecords()
end

function GameManager:GetRemainingTime()
    return self.remainingTime
end

function GameManager:GetPressureStage()
    return self.pressureStage
end

function GameManager:GetPlayerBulletsRemaining()
    return self.playerBulletsRemaining
end

function GameManager:ConsumePlayerBullet()
    local bulletsRemaining = tonumber(self.playerBulletsRemaining) or 0
    if bulletsRemaining <= 0 then
        self.playerBulletsRemaining = 0
        return false
    end

    self.playerBulletsRemaining = bulletsRemaining - 1
    return true
end

function GameManager:ReportPlayerShotFailure(reason)
    if self.state ~= self.State.Playing then
        return false
    end
    if self.bFailureTimeDrainActive then
        return true
    end

    self.failedShotCount = math.min(
        self.maxFailedShotsBeforeGameOver,
        (tonumber(self.failedShotCount) or 0) + 1
    )

    if self.failedShotCount >= self.maxFailedShotsBeforeGameOver then
        return self:_StartFailureTimeDrain(reason or "FailedShots")
    end

    return false
end

function GameManager:GetFailedShotCount()
    return self.failedShotCount
end

function GameManager:SetPressureStageOverride(pressure)
    local rawPressure = pressure
    pressure = normalize_pressure(pressure)
    if pressure == nil then
        print("[GameManager] Unknown pressure: " .. tostring(rawPressure))
        return false
    end

    self.manualPressureStage = pressure
    return self:_SetPressureStage(pressure, "SetPressureStageOverride", true)
end

function GameManager:ClearPressureStageOverride()
    self.manualPressureStage = nil
    return self:_RefreshPressureStage("ClearPressureStageOverride", true)
end

function GameManager:KillPlayer(reason)
    if self.isPlayerDead then
        return false
    end

    self.isPlayerDead = true
    self:_FireEvent("PlayerDead", reason or "PlayerDead")
    self:GameOver(reason or "PlayerDead")
    return true
end

function GameManager:IsPlaying()
    return self.state == self.State.Playing
end

function GameManager:IsEnding()
    return self.state == self.State.Ending
end

function GameManager:GetState()
    return self.state
end

function GameManager:GetWarpCount()
    return LoopManager:GetWarpCount()
end

function GameManager:OnWarp(reason)
    if reason == "PlayerWarp" then
        local StageManager = require("StageManager")
        if not StageManager:CanZoneWarp() then
            return false
        end
    end

    local bWarped = LoopManager:OnWarp(self, reason, function(setupReason)
        return self:_SetupAnomaly(setupReason)
    end)
    if bWarped and LoopManager:GetWarpCount() >= 1 then
        require("DoorManager"):TryScheduleChaosSingleDoorToggles("warp")
    end
    self.bLoopStopped = LoopManager:IsLoopStopped()
    self.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    return bWarped
end

function GameManager:OnLoopStart(reason)
    local bStarted = LoopManager:OnLoopStart(self, reason, function(startReason)
        self.remainingTime = self.timeLimit or 0
        self:_RefreshPressureStage(startReason, true)
        JumpScareManager:ActivateRandom()
    end)
    self.bLoopStopped = LoopManager:IsLoopStopped()
    self.bCymbalMonkeyCycleStarted = LoopManager:IsCymbalMonkeyCycleStarted()
    return bStarted
end

function GameManager:SetJumpScareActiveCount(count)
    return JumpScareManager:SetActiveCount(count)
end

function GameManager:GetJumpScareActiveCount()
    return JumpScareManager:GetActiveCount()
end

function GameManager:GetLastJumpScareError()
    return JumpScareManager:GetLastError()
end

function GameManager:AdvanceAnomalyLoop()
    if self.state ~= self.State.Playing then
        return false
    end

    local bWarped = self:OnWarp("AdvanceAnomalyLoop")
    local bStarted = self:OnLoopStart("AdvanceAnomalyLoop")
    return bWarped and bStarted
end

function GameManager:ReportAnomalyShot(actor, hit)
    local bHitAnomaly = AnomalyManager:ReportShot(actor, hit)
    if bHitAnomaly then
        self:_PlayAnomalyHitEffect(actor, hit)
        local StageManager = require("StageManager")
        if StageManager:IsFinalStage() then
            return require("EndingManager"):Enter(nil, hit)
        end
        self:StopLoop("AnomalyShot")
    end
    return bHitAnomaly
end

function GameManager:NotifyPhotoCaptureRequested()
    return AnomalyManager:NotifyPhotoCaptureRequested()
end

function GameManager:GetActiveAnomalyTarget()
    return AnomalyManager:GetActiveTarget()
end

function GameManager:GetActiveAnomalyRuleName()
    return AnomalyManager:GetActiveRuleName()
end

function GameManager:GetLastAnomalyError()
    return AnomalyManager:GetLastError()
end

function GameManager:SetAnomalyPlacementTemplateSetName(name)
    if type(name) ~= "string" or self.AnomalyPlacementTemplateSets[name] == nil then
        self.LastAnomalyPlacementError = "Unknown anomaly placement template set: " .. tostring(name)
        return false
    end

    self.AnomalyPlacementTemplateSetName = name
    self.LastAnomalyPlacementError = nil
    return true
end

function GameManager:GetAnomalyPlacementTemplateSetName()
    return self.AnomalyPlacementTemplateSetName
end

function GameManager:GetLastAnomalyPlacementError()
    return self.LastAnomalyPlacementError
end

function GameManager:DebugSpawnAnomalyRule(ruleName)
    if self.state ~= self.State.Playing then
        print("[GameManager] DebugSpawnAnomalyRule ignored: game is not playing")
        return false
    end

    self:ClearActiveAnomalyOutline()
    return AnomalyManager:SelectAndSpawnRule(ruleName)
end

function GameManager:DebugSetStage(stage)
    if self.state ~= self.State.Playing and self.state ~= self.State.Ending then
        print("[GameManager] DebugSetStage ignored: state=" .. tostring(self.state))
        return false
    end

    local StageManager = require("StageManager")
    stage = math.floor(tonumber(stage) or 1)
    stage = math.max(1, math.min(stage, StageManager.MAX_STAGE))

    if self.state == self.State.Ending then
        require("EndingManager"):Reset()
        self:_SetState(self.State.Playing, "DebugSetStage")
    end

    LoopManager.warpCount = stage - 1
    self:_ResetPlayerBulletsForStage()
    self:RestLoop("DebugSetStage")
    self:_SetupAnomaly("DebugSetStage")
    self:_RefreshPressureStage("DebugSetStage", true)

    print(string.format(
        "[GameManager] DebugSetStage -> stage %d (warpCount=%d)",
        stage,
        LoopManager.warpCount
    ))
    return true
end

function GameManager:DebugEnterEnding()
    if self.state ~= self.State.Playing and self.state ~= self.State.Ending then
        print("[GameManager] DebugEnterEnding ignored: state=" .. tostring(self.state))
        return false
    end

    return require("EndingManager"):Enter(nil, nil)
end

return GameManager
