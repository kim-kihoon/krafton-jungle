local SettingManager = {}

SettingManager.GammaOptions = {
    { Label = "Darker", Value = 2.0 },
    { Label = "Default", Value = 2.4 },
    { Label = "Brighter", Value = 2.8 },
}

SettingManager.MasterVolumeOptions = {
    { Label = "0", Value = 0.0 },
    { Label = "50", Value = 0.5 },
    { Label = "100", Value = 1.0 },
}

SettingManager.MouseSensitivityOptions = {
    { Label = "Low", Value = 0.1 },
    { Label = "Normal", Value = 0.2 },
    { Label = "High", Value = 0.35 },
}

SettingManager.DisplayModeOptions = {
    { Label = "Windowed", bFullscreen = false },
    { Label = "Fullscreen", bFullscreen = true },
}

SettingManager.GammaIndex = 2
SettingManager.MasterVolumeIndex = 3
SettingManager.MouseSensitivityIndex = 2
SettingManager.DisplayModeIndex = 2
SettingManager.bInvertY = false
SettingManager.bHeadBob = true
SettingManager.bControlPrompt = true
SettingManager.bLoaded = false

local SAVE_KEY_GAMMA = "GammaIndex"
local SAVE_KEY_MASTER_VOLUME = "MasterVolumeIndex"
local SAVE_KEY_MOUSE_SENSITIVITY = "MouseSensitivityIndex"
local SAVE_KEY_DISPLAY_MODE = "DisplayModeIndex"
local SAVE_KEY_INVERT_Y = "InvertY"
local SAVE_KEY_HEAD_BOB = "HeadBob"
local SAVE_KEY_CONTROL_PROMPT = "ControlPrompt"

local function cycle_index(currentIndex, count)
    currentIndex = tonumber(currentIndex) or 1
    count = tonumber(count) or 1
    if count <= 0 then
        return 1
    end
    return (currentIndex % count) + 1
end

local function clamp_index(value, count, fallback)
    value = math.floor(tonumber(value) or fallback or 1)
    if value < 1 or value > count then
        return fallback or 1
    end
    return value
end

local function set_widget_text(widget, elementId, text)
    if widget == nil or widget.SetText == nil then
        return
    end

    pcall(function()
        widget:SetText(elementId, text)
    end)
end

local function format_toggle(bEnabled)
    return bEnabled and "On" or "Off"
end

function SettingManager:GetGammaOption()
    return self.GammaOptions[self.GammaIndex] or self.GammaOptions[2]
end

function SettingManager:GetMasterVolumeOption()
    return self.MasterVolumeOptions[self.MasterVolumeIndex] or self.MasterVolumeOptions[3]
end

function SettingManager:GetMouseSensitivityOption()
    return self.MouseSensitivityOptions[self.MouseSensitivityIndex] or self.MouseSensitivityOptions[2]
end

function SettingManager:GetDisplayModeOption()
    return self.DisplayModeOptions[self.DisplayModeIndex] or self.DisplayModeOptions[2]
end

function SettingManager:IsHeadBobEnabled()
    return self.bHeadBob == true
end

function SettingManager:IsControlPromptEnabled()
    return self.bControlPrompt == true
end

function SettingManager:Load()
    if self.bLoaded then
        return
    end
    self.bLoaded = true

    if UserSettings == nil then
        return
    end

    if UserSettings.LoadInt ~= nil then
        self.GammaIndex = clamp_index(UserSettings.LoadInt(SAVE_KEY_GAMMA, self.GammaIndex), #self.GammaOptions, 2)
        self.MasterVolumeIndex = clamp_index(UserSettings.LoadInt(SAVE_KEY_MASTER_VOLUME, self.MasterVolumeIndex), #self.MasterVolumeOptions, 3)
        self.MouseSensitivityIndex = clamp_index(UserSettings.LoadInt(SAVE_KEY_MOUSE_SENSITIVITY, self.MouseSensitivityIndex), #self.MouseSensitivityOptions, 2)
        self.DisplayModeIndex = clamp_index(UserSettings.LoadInt(SAVE_KEY_DISPLAY_MODE, self.DisplayModeIndex), #self.DisplayModeOptions, 2)
    end

    if UserSettings.LoadBool ~= nil then
        self.bInvertY = UserSettings.LoadBool(SAVE_KEY_INVERT_Y, self.bInvertY)
        self.bHeadBob = UserSettings.LoadBool(SAVE_KEY_HEAD_BOB, self.bHeadBob)
        self.bControlPrompt = UserSettings.LoadBool(SAVE_KEY_CONTROL_PROMPT, self.bControlPrompt)
    end
end

function SettingManager:Save()
    if UserSettings == nil then
        return false
    end

    local bSaved = false
    if UserSettings.SaveInt ~= nil then
        bSaved = UserSettings.SaveInt(SAVE_KEY_GAMMA, self.GammaIndex) or bSaved
        bSaved = UserSettings.SaveInt(SAVE_KEY_MASTER_VOLUME, self.MasterVolumeIndex) or bSaved
        bSaved = UserSettings.SaveInt(SAVE_KEY_MOUSE_SENSITIVITY, self.MouseSensitivityIndex) or bSaved
        bSaved = UserSettings.SaveInt(SAVE_KEY_DISPLAY_MODE, self.DisplayModeIndex) or bSaved
    end

    if UserSettings.SaveBool ~= nil then
        bSaved = UserSettings.SaveBool(SAVE_KEY_INVERT_Y, self.bInvertY == true) or bSaved
        bSaved = UserSettings.SaveBool(SAVE_KEY_HEAD_BOB, self.bHeadBob == true) or bSaved
        bSaved = UserSettings.SaveBool(SAVE_KEY_CONTROL_PROMPT, self.bControlPrompt == true) or bSaved
    end
    return bSaved
end

function SettingManager:ApplyDisplayMode()
    self:Load()

    if Engine == nil or Engine.SetFullscreen == nil then
        return
    end

    local option = self:GetDisplayModeOption()
    pcall(function()
        Engine.SetFullscreen(option.bFullscreen == true)
    end)
end

function SettingManager:Apply()
    self:Load()
    self:ApplyDisplayMode()

    local gamma = self:GetGammaOption().Value
    if Engine ~= nil then
        if Engine.SetGammaCorrectionEnabled ~= nil then
            pcall(function()
                Engine.SetGammaCorrectionEnabled(true)
            end)
        end
        if Engine.SetGamma ~= nil then
            pcall(function()
                Engine.SetGamma(gamma)
            end)
        end
    end

    if Audio ~= nil and Audio.SetMasterVolume ~= nil then
        local volume = self:GetMasterVolumeOption().Value
        pcall(function()
            Audio.SetMasterVolume(volume)
        end)
    end
end

function SettingManager:ApplyPlayerSettings(player)
    self:Load()

    if player == nil then
        return
    end

    local sensitivity = self:GetMouseSensitivityOption().Value
    if player.SetMouseSensitivity ~= nil then
        pcall(function()
            player:SetMouseSensitivity(sensitivity)
        end)
    end

    if player.SetInvertMouseY ~= nil then
        pcall(function()
            player:SetInvertMouseY(self.bInvertY == true)
        end)
    end
end

function SettingManager:ApplyAll(player)
    self:Apply()
    self:ApplyPlayerSettings(player)
end

function SettingManager:RefreshWidget(widget)
    self:Load()

    set_widget_text(widget, "setting_gamma_button", "Gamma: " .. self:GetGammaOption().Label)
    set_widget_text(widget, "setting_volume_button", "Master Volume: " .. self:GetMasterVolumeOption().Label)
    set_widget_text(widget, "setting_mouse_button", "Mouse Sensitivity: " .. self:GetMouseSensitivityOption().Label)
    set_widget_text(widget, "setting_invert_button", "Invert Y: " .. format_toggle(self.bInvertY))
    set_widget_text(widget, "setting_headbob_button", "Head Bob: " .. format_toggle(self.bHeadBob))
    set_widget_text(widget, "setting_control_prompt_button", "Control Prompt: " .. format_toggle(self.bControlPrompt))
    set_widget_text(widget, "setting_display_mode_button", "Display Mode: " .. self:GetDisplayModeOption().Label)
end

function SettingManager:CycleGamma()
    self:Load()
    self.GammaIndex = cycle_index(self.GammaIndex, #self.GammaOptions)
    self:Apply()
    self:Save()
end

function SettingManager:CycleMasterVolume()
    self:Load()
    self.MasterVolumeIndex = cycle_index(self.MasterVolumeIndex, #self.MasterVolumeOptions)
    self:Apply()
    self:Save()
end

function SettingManager:CycleMouseSensitivity(player)
    self:Load()
    self.MouseSensitivityIndex = cycle_index(self.MouseSensitivityIndex, #self.MouseSensitivityOptions)
    self:ApplyPlayerSettings(player)
    self:Save()
end

function SettingManager:ToggleInvertY(player)
    self:Load()
    self.bInvertY = not self.bInvertY
    self:ApplyPlayerSettings(player)
    self:Save()
end

function SettingManager:ToggleHeadBob()
    self:Load()
    self.bHeadBob = not self.bHeadBob
    self:Save()
end

function SettingManager:ToggleControlPrompt()
    self:Load()
    self.bControlPrompt = not self.bControlPrompt
    self:Save()
end

function SettingManager:CycleDisplayMode()
    self:Load()
    self.DisplayModeIndex = cycle_index(self.DisplayModeIndex, #self.DisplayModeOptions)
    self:ApplyDisplayMode()
    self:Save()
end

return SettingManager
