local DebugManager = {}

DebugManager.bEnabled = true
DebugManager.OutlineKey = "L"
DebugManager.OutlineActionName = "DebugAnomalyOutline"
DebugManager.ClearKey = "C"

DebugManager.Scenarios = {
    {
        Key = "1",
        RuleName = "PhotoInvisible"
    },
    {
        Key = "2",
        RuleName = "PhotoLookAtInvisible"
    },
    {
        Key = "3",
        RuleName = "PhotoLookAtBlackPhoto"
    },
    {
        Key = "4",
        RuleName = "BlackPhoto"
    },
    {
        Key = "5",
        RuleName = "PhotoGhostReplacement"
    },
    {
        Key = "6",
        RuleName = "PhotoBoneTwist"
    }
}

function DebugManager:SetEnabled(bEnabled)
    self.bEnabled = bEnabled == true
end

function DebugManager:IsEnabled()
    return self.bEnabled == true
end

function DebugManager:LoadAnomalyScenario(gameManager, ruleName)
    local GameManager = gameManager
    if GameManager == nil or GameManager.DebugSpawnAnomalyRule == nil then
        print("[DebugManager] GameManager.DebugSpawnAnomalyRule unavailable")
        return false
    end

    local ok = GameManager:DebugSpawnAnomalyRule(ruleName)
    if ok then
        print("[DebugManager] Loaded anomaly scenario: " .. tostring(ruleName))
        return true
    end

    local reason = nil
    if GameManager.GetLastAnomalyError ~= nil then
        reason = GameManager:GetLastAnomalyError()
    end
    print("[DebugManager] Failed to load anomaly scenario: " .. tostring(ruleName) .. " reason=" .. tostring(reason))
    return false
end

function DebugManager:SetActiveAnomalyOutlineVisible(gameManager, bVisible)
    local GameManager = gameManager
    if GameManager == nil or GameManager.SetActiveAnomalyOutlineVisible == nil then
        print("[DebugManager] GameManager.SetActiveAnomalyOutlineVisible unavailable")
        return false
    end

    return GameManager:SetActiveAnomalyOutlineVisible(bVisible == true)
end

function DebugManager:ClearGame(gameManager)
    if gameManager == nil or gameManager.ClearGame == nil then
        print("[DebugManager] GameManager.ClearGame unavailable")
        return false
    end

    local ok = gameManager:ClearGame("DebugClear")
    if ok then
        print("[DebugManager] ClearGame triggered")
    end
    return ok == true
end

function DebugManager:Tick(dt, gameManager)
    if not self:IsEnabled() then
        return
    end
    if gameManager ~= nil
        and gameManager.IsEnding ~= nil
        and gameManager:IsEnding() then
        return
    end
    if Input == nil then
        return
    end

    local bOutlineHeld = false
    if Input.GetKey ~= nil and Input.GetKey(self.OutlineKey) then
        bOutlineHeld = true
    end
    if not bOutlineHeld and Input.GetAction ~= nil and Input.GetAction(self.OutlineActionName) then
        bOutlineHeld = true
    end
    self:SetActiveAnomalyOutlineVisible(gameManager, bOutlineHeld)

    if Input.GetKeyDown == nil then
        return
    end

    if Input.GetKeyDown(self.ClearKey) then
        self:ClearGame(gameManager)
        return
    end

    for _, scenario in ipairs(self.Scenarios) do
        if Input.GetKeyDown(scenario.Key) then
            self:LoadAnomalyScenario(gameManager, scenario.RuleName)
            return
        end
    end

    if Input.GetKeyDown("9") then
        if gameManager ~= nil and gameManager.DebugSetStage ~= nil then
            gameManager:DebugSetStage(6)
        end
        return
    end

    if Input.GetKeyDown("0") then
        if gameManager ~= nil and gameManager.DebugEnterEnding ~= nil then
            gameManager:DebugEnterEnding()
        end
        return
    end
end

return DebugManager
