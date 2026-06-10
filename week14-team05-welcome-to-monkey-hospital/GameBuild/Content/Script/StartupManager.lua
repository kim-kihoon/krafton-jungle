local UIManager = require("UIManager")

local StartupManager = {}

StartupManager.LOGO_FADE_SECONDS = 0.5
StartupManager.LOGO_HOLD_SECONDS = 2.5
StartupManager.WARNING_DISPLAY_SECONDS = 5.0
StartupManager.LOGO_FADE_STEPS = 20

StartupManager.bActive = false
StartupManager.SequenceCoroutine = nil

local function stop_startup_sequence()
    StartupManager.SequenceCoroutine = nil
    StartupManager.bActive = false
end

local function fade_in_studio_logo()
    local fadeSeconds = StartupManager.LOGO_FADE_SECONDS
    local holdSeconds = StartupManager.LOGO_HOLD_SECONDS
    local fadeSteps = math.max(1, math.floor(tonumber(StartupManager.LOGO_FADE_STEPS) or 20))
    local stepSeconds = fadeSeconds / fadeSteps

    UIManager:SetStartupLogoOpacity(0.0)
    for stepIndex = 1, fadeSteps do
        local opacity = stepIndex / fadeSteps
        UIManager:SetStartupLogoOpacity(opacity)
        Wait(stepSeconds)
    end

    UIManager:SetStartupLogoOpacity(1.0)
    if holdSeconds > 0.0 then
        Wait(holdSeconds)
    end
end

local function run_startup_sequence(onComplete)
    if UIManager:ShowStartupIntro() ~= true then
        stop_startup_sequence()
        if type(onComplete) == "function" then
            onComplete()
        end
        return
    end

    fade_in_studio_logo()
    UIManager:ShowStartupWarning()
    Wait(StartupManager.WARNING_DISPLAY_SECONDS)
    UIManager:HideStartupIntro()

    stop_startup_sequence()
    if type(onComplete) == "function" then
        onComplete()
    end
end

function StartupManager:IsActive()
    return self.bActive == true
end

function StartupManager:Begin(onComplete)
    self:Cancel()

    if StartCoroutine == nil or Wait == nil then
        if type(onComplete) == "function" then
            onComplete()
        end
        return false
    end

    self.bActive = true
    self.SequenceCoroutine = StartCoroutine(function()
        run_startup_sequence(onComplete)
    end)
    return true
end

function StartupManager:Cancel()
    stop_startup_sequence()
    if UIManager ~= nil and UIManager.HideStartupIntro ~= nil then
        UIManager:HideStartupIntro()
    end
end

return StartupManager
