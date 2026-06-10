local TitleManager = {}
local LeaderboardManager = require("LeaderboardManager")
local SoundManager = require("SoundManager")
local SettingManager = require("SettingManager")

TitleManager.MainDocumentPath = "Content/UI/TitleUI.rml"
TitleManager.SettingDocumentPath = "Content/UI/SettingUI.rml"
TitleManager.CreditDocumentPath = "Content/UI/CreditUI.rml"
TitleManager.LeaderboardDocumentPath = "Content/UI/LeaderboardUI.rml"
TitleManager.StartSceneName = "Hospital.Scene"
TitleManager.MainWidget = nil
TitleManager.PopupWidget = nil
TitleManager.LeaderboardMaxRows = 20

local function set_widget_display(widget, element_id, visible)
    if widget ~= nil and widget.SetProperty ~= nil then
        widget:SetProperty(element_id, "display", visible and "block" or "none")
    end
end

local function set_widget_text(widget, element_id, text)
    if widget ~= nil and widget.SetText ~= nil then
        widget:SetText(element_id, text)
    end
end

local function format_leaderboard_seconds(total_seconds)
    total_seconds = math.max(0, math.floor(tonumber(total_seconds) or 0))
    local minutes = math.floor(total_seconds / 60)
    local seconds = total_seconds % 60
    return string.format("%d:%02d", minutes, seconds)
end

local function pad_right(text, width)
    text = tostring(text or "")
    width = math.floor(tonumber(width) or 0)
    if #text >= width then
        return text
    end
    return text .. string.rep(" ", width - #text)
end

local function format_leaderboard_row(entry, fallback_rank)
    local rank = math.floor(tonumber(entry.Rank) or fallback_rank or 0)
    local time_text = format_leaderboard_seconds(entry.TotalTimeSeconds)
    local player_name = tostring(entry.PlayerName or "Player")
    return pad_right(string.format("#%02d", rank), 7)
        .. pad_right(time_text, 11)
        .. player_name
end

local function add_widget_to_viewport(widget, z_order)
    if widget == nil then
        return false
    end

    if widget.SetWantsMouse ~= nil then
        widget:SetWantsMouse(true)
    end
    if widget.SetWantsKeyboard ~= nil then
        widget:SetWantsKeyboard(true)
    end
    if widget.SetBlocksGameInput ~= nil then
        widget:SetBlocksGameInput(true)
    end
    if widget.SetBlocksGameKeyboard ~= nil then
        widget:SetBlocksGameKeyboard(true)
    end
    if widget.SetBlocksGameMouseLook ~= nil then
        widget:SetBlocksGameMouseLook(true)
    end

    if widget.AddToViewportZ ~= nil then
        widget:AddToViewportZ(z_order)
    elseif widget.AddToViewport ~= nil then
        widget:AddToViewport()
    end

    return true
end

local function create_widget(document_path)
    if UI == nil or UI.CreateWidget == nil then
        return nil
    end

    return UI.CreateWidget(document_path)
end

function TitleManager:Show()
    if self.MainWidget ~= nil and self.MainWidget.IsInViewport ~= nil and self.MainWidget:IsInViewport() then
        return true
    end

    self.MainWidget = create_widget(self.MainDocumentPath)
    return add_widget_to_viewport(self.MainWidget, 100)
end

function TitleManager:ClosePopup()
    if self.PopupWidget ~= nil and self.PopupWidget.RemoveFromParent ~= nil then
        self.PopupWidget:RemoveFromParent()
    end
    self.PopupWidget = nil
    set_widget_display(self.MainWidget, "ui_canvas", true)
end

function TitleManager:ShowPopup(document_path)
    self:ClosePopup()
    set_widget_display(self.MainWidget, "ui_canvas", false)
    self.PopupWidget = create_widget(document_path)
    return add_widget_to_viewport(self.PopupWidget, 110)
end

function TitleManager:ShowSetting()
    local shown = self:ShowPopup(self.SettingDocumentPath)
    SettingManager:Apply()
    self:RefreshSetting()
    return shown
end

function TitleManager:RefreshSetting()
    SettingManager:RefreshWidget(self.PopupWidget)
end

function TitleManager:ShowCredit()
    return self:ShowPopup(self.CreditDocumentPath)
end

function TitleManager:PopulateLeaderboard(widget)
    if widget == nil then
        return
    end

    local entries = LeaderboardManager:GetEntries()
    local entry_count = #entries
    set_widget_display(widget, "leaderboard_empty", entry_count <= 0)

    for index = 1, self.LeaderboardMaxRows do
        local row_id = "leaderboard_row_" .. tostring(index)
        local entry = entries[index]
        if entry ~= nil then
            set_widget_text(widget, row_id, format_leaderboard_row(entry, index))
            set_widget_display(widget, row_id, true)
        else
            set_widget_text(widget, row_id, "")
            set_widget_display(widget, row_id, false)
        end
    end
end

function TitleManager:ShowRanking()
    local shown = self:ShowPopup(self.LeaderboardDocumentPath)
    self:PopulateLeaderboard(self.PopupWidget)
    return shown
end

function TitleManager:StartGame()
    SoundManager:StopTitleMusic()
    self:Dispose()
    if Engine ~= nil and Engine.TransitionToScene ~= nil then
        Engine.TransitionToScene(self.StartSceneName)
    end
end

function TitleManager:ExitGame()
    if Engine ~= nil and Engine.Exit ~= nil then
        Engine.Exit()
    end
end

function TitleManager:Dispose()
    self:ClosePopup()
    if self.MainWidget ~= nil and self.MainWidget.RemoveFromParent ~= nil then
        self.MainWidget:RemoveFromParent()
    end
    self.MainWidget = nil
end

function BeginPlay()
    SoundManager:EnterTitleState()
    TitleManager:Show()
    if SoundManager.PlayTitleMusicIfNeeded ~= nil then
        SoundManager:PlayTitleMusicIfNeeded()
    else
        SoundManager:PlayTitleMusic()
    end
end

function EndPlay()
    TitleManager:Dispose()
    SoundManager:StopTitleMusic()
end

function StartGame()
    TitleManager:StartGame()
end

function ShowSetting()
    TitleManager:ShowSetting()
end

function CycleSettingGamma()
    SettingManager:CycleGamma()
    TitleManager:RefreshSetting()
end

function CycleSettingMasterVolume()
    SettingManager:CycleMasterVolume()
    TitleManager:RefreshSetting()
end

function CycleSettingMouseSensitivity()
    SettingManager:CycleMouseSensitivity(nil)
    TitleManager:RefreshSetting()
end

function ToggleSettingInvertY()
    SettingManager:ToggleInvertY(nil)
    TitleManager:RefreshSetting()
end

function ToggleSettingHeadBob()
    SettingManager:ToggleHeadBob()
    TitleManager:RefreshSetting()
end

function ToggleSettingControlPrompt()
    SettingManager:ToggleControlPrompt()
    TitleManager:RefreshSetting()
end

function CycleSettingDisplayMode()
    SettingManager:CycleDisplayMode()
    TitleManager:RefreshSetting()
end

function ShowRanking()
    TitleManager:ShowRanking()
end

function ShowCredit()
    TitleManager:ShowCredit()
end

function ClosePopup()
    TitleManager:ClosePopup()
end

function ExitGame()
    TitleManager:ExitGame()
end

return TitleManager
