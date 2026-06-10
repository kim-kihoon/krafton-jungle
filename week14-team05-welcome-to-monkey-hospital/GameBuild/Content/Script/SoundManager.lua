local SoundManager = {}

SoundManager.State = {
    Title = "Title",
    Playing = "Playing"
}

SoundManager.DoorOpenSoundKey = "DoorOpen"
SoundManager.HeavyDoorOpenSoundKey = "HeavyDoorOpen"
SoundManager.DoorCloseSoundKey = "DoorClose"
SoundManager.DoorSoundMinDistance = 1.0
SoundManager.DoorSoundMaxDistance = 12.0
SoundManager.DoorSoundVolume = 0.45
SoundManager.DoorOpenSoundVolume = 0.7
SoundManager.DoorCloseSoundDelay = 1.0
SoundManager.PartyBlowerSoundKey = "PartyBlower"
SoundManager.PartyBlowerSoundVolume = 0.25
SoundManager.EmptyGunShotSoundKey = "EmptyGunShot"
SoundManager.EmptyGunShotSoundVolume = 0.2
SoundManager.TitleMusicKey = "HospitalTitleMusic"
SoundManager.TitleMusicPath = "Music/A1 - It's just a burning memory.mp3"
SoundManager.TitleMusicVolume = 0.1
SoundManager.bTitleMusicLoaded = false
SoundManager.bTitleMusicPlaying = false
SoundManager.bPreserveBgmOnReset = false
SoundManager.bContinueEndingCreditsBgm = false
SoundManager.PendingDoorCloseSounds = {}
SoundManager.TitleMutedAudioComponents = {}
SoundManager.bTitleWorldAudioMuted = false
SoundManager.CurrentState = SoundManager.State.Title

local function get_actor_location(actor)
    if actor == nil or actor.GetLocation == nil then
        return nil
    end

    local ok, location = pcall(function()
        return actor:GetLocation()
    end)
    if ok then
        return location
    end
    return nil
end

local function is_valid_object(object)
    if object == nil or object.IsValid == nil then
        return false
    end

    local ok, valid = pcall(function()
        return object:IsValid()
    end)
    return ok and valid == true
end

local function is_audio_component(component)
    if not is_valid_object(component) or component.IsA == nil then
        return false
    end

    local ok, result = pcall(function()
        return component:IsA("UAudioComponent")
    end)
    return ok and result == true
end

local TITLE_AUDIO_EXEMPT_TAG = "Title"

local function actor_has_tag(actor, tag)
    if actor == nil or actor.HasTag == nil or tag == nil then
        return false
    end

    local ok, result = pcall(function()
        return actor:HasTag(tag)
    end)
    return ok and result == true
end

local function is_title_actor_audio_component(component)
    if component == nil or component.GetOwner == nil then
        return false
    end

    local ok, owner = pcall(function()
        return component:GetOwner()
    end)
    if not ok or owner == nil then
        return false
    end

    return actor_has_tag(owner, TITLE_AUDIO_EXEMPT_TAG)
end

local function class_exists(className)
    if Class == nil or Class.Exists == nil then
        return true
    end

    local ok, exists = pcall(function()
        return Class.Exists(className)
    end)
    return ok and exists == true
end

local function find_world_actors()
    if World == nil or World.FindActorsByClass == nil then
        return nil
    end

    local actorClassNames = { "AActor", "Actor" }
    for _, className in ipairs(actorClassNames) do
        if class_exists(className) then
            local ok, actors = pcall(function()
                return World.FindActorsByClass(className)
            end)
            if ok and actors ~= nil then
                return actors
            end
        end
    end

    return nil
end

local function get_audio_component_volume(component)
    if component == nil then
        return false, nil
    end

    if component.GetVolume ~= nil then
        local ok, volume = pcall(function()
            return component:GetVolume()
        end)
        if ok and volume ~= nil then
            return true, volume
        end
    end

    if component.CallFunction ~= nil then
        local ok, volume = pcall(function()
            return component:CallFunction("GetVolume")
        end)
        if ok and volume ~= nil then
            return true, volume
        end
    end

    return false, nil
end

local function set_audio_component_volume(component, volume)
    if component == nil then
        return false
    end

    if component.SetVolume ~= nil then
        local ok = pcall(function()
            component:SetVolume(volume)
        end)
        if ok then
            return true
        end
    end

    if component.CallFunction ~= nil then
        local ok = pcall(function()
            component:CallFunction("SetVolume", volume)
        end)
        return ok == true
    end

    return false
end

local function mute_audio_component_for_startup(component)
    if component == nil then
        return false
    end

    if component.MuteForStartup ~= nil then
        local ok, result = pcall(function()
            return component:MuteForStartup()
        end)
        if ok then
            return result ~= false
        end
    end

    if component.CallFunction ~= nil then
        local ok, result = pcall(function()
            return component:CallFunction("MuteForStartup")
        end)
        if ok and result ~= nil then
            return result ~= false
        end
    end

    return false
end

local function restore_audio_component_startup_mute(component)
    if component == nil then
        return false
    end

    if component.RestoreStartupMute ~= nil then
        local ok, result = pcall(function()
            return component:RestoreStartupMute()
        end)
        if ok then
            return result == true
        end
    end

    if component.CallFunction ~= nil then
        local ok, result = pcall(function()
            return component:CallFunction("RestoreStartupMute")
        end)
        if ok then
            return result == true
        end
    end

    return false
end

local function ensure_title_actor_audio_audible(component)
    if not is_audio_component(component) then
        return
    end

    restore_audio_component_startup_mute(component)

    local okVolume, volume = get_audio_component_volume(component)
    if okVolume and volume ~= nil and volume <= 0.001 then
        set_audio_component_volume(component, 1.0)
    end
end

function SoundManager:Play(key, volume)
    if Audio == nil or Audio.Play == nil or key == nil then
        return false
    end

    local ok = pcall(function()
        Audio.Play(key, tonumber(volume) or 1.0)
    end)
    return ok == true
end

function SoundManager:PlayTitle(key, volume)
    return self:Play(key, volume)
end

function SoundManager:PlayGameplay(key, volume)
    return self:Play(key, volume)
end

function SoundManager:PlayAt(key, volume, location, minDistance, maxDistance)
    if Audio == nil or key == nil or location == nil then
        return false
    end

    if Audio.PlayAt ~= nil then
        local ok = pcall(function()
            Audio.PlayAt(
                key,
                tonumber(volume) or 1.0,
                location,
                tonumber(minDistance) or self.DoorSoundMinDistance,
                tonumber(maxDistance) or self.DoorSoundMaxDistance
            )
        end)
        if ok then
            return true
        end
    end

    return self:Play(key, volume)
end

function SoundManager:PlayTitleAt(key, volume, location, minDistance, maxDistance)
    return self:PlayAt(key, volume, location, minDistance, maxDistance)
end

function SoundManager:PlayGameplayAt(key, volume, location, minDistance, maxDistance)
    return self:PlayAt(key, volume, location, minDistance, maxDistance)
end

function SoundManager:PlayAtActor(actor, key, volume, minDistance, maxDistance)
    local location = get_actor_location(actor)
    if location == nil then
        return false
    end

    return self:PlayAt(key, volume, location, minDistance, maxDistance)
end

function SoundManager:PlayTitleAtActor(actor, key, volume, minDistance, maxDistance)
    return self:PlayAtActor(actor, key, volume, minDistance, maxDistance)
end

function SoundManager:PlayGameplayAtActor(actor, key, volume, minDistance, maxDistance)
    return self:PlayAtActor(actor, key, volume, minDistance, maxDistance)
end

function SoundManager:PlayDoorOpen(actor, bHeavy)
    local key = bHeavy and self.HeavyDoorOpenSoundKey or self.DoorOpenSoundKey
    local volume = bHeavy and self.DoorSoundVolume or self.DoorOpenSoundVolume
    return self:PlayGameplayAtActor(actor, key, volume, self.DoorSoundMinDistance, self.DoorSoundMaxDistance)
end

function SoundManager:QueueDoorClose(actor)
    if actor == nil then
        return false
    end

    table.insert(self.PendingDoorCloseSounds, {
        Delay = self.DoorCloseSoundDelay,
        Actor = actor
    })
    return true
end

function SoundManager:TickGameplaySounds(dt)
    local deltaTime = tonumber(dt) or 0.0
    if deltaTime <= 0.0 then
        return
    end

    local index = 1
    while index <= #self.PendingDoorCloseSounds do
        local pending = self.PendingDoorCloseSounds[index]
        pending.Delay = pending.Delay - deltaTime
        if pending.Delay <= 0.0 then
            self:PlayGameplayAtActor(
                pending.Actor,
                self.DoorCloseSoundKey,
                self.DoorSoundVolume,
                self.DoorSoundMinDistance,
                self.DoorSoundMaxDistance
            )
            table.remove(self.PendingDoorCloseSounds, index)
        else
            index = index + 1
        end
    end
end

function SoundManager:Tick(dt)
    self:TickGameplaySounds(dt)
end

function SoundManager:LoadTitleMusic()
    if Audio == nil or Audio.Load == nil then
        self.bTitleMusicLoaded = false
        return false
    end

    local ok, result = pcall(function()
        return Audio.Load(self.TitleMusicKey, self.TitleMusicPath, true)
    end)
    self.bTitleMusicLoaded = ok and result ~= false
    return self.bTitleMusicLoaded
end

function SoundManager:IsTitleMusicPlaying()
    return self.bTitleMusicPlaying == true
end

function SoundManager:SetPreserveBgmOnReset(bPreserve)
    self.bPreserveBgmOnReset = bPreserve == true
end

function SoundManager:SetContinueEndingCreditsBgm(bContinue)
    self.bContinueEndingCreditsBgm = bContinue == true
end

function SoundManager:PlayTitleMusicIfNeeded()
    if self.bContinueEndingCreditsBgm then
        return true
    end
    if self:IsTitleMusicPlaying() then
        return true
    end
    return self:PlayTitleMusic()
end

function SoundManager:PlayTitleMusic()
    if Audio == nil or Audio.PlayBGM == nil then
        self.bTitleMusicPlaying = false
        return false
    end

    if self:IsTitleMusicPlaying() then
        return true
    end

    if not self:LoadTitleMusic() then
        self.bTitleMusicPlaying = false
        return false
    end

    local ok = pcall(function()
        Audio.PlayBGM(self.TitleMusicKey, self.TitleMusicVolume)
    end)
    self.bTitleMusicPlaying = ok == true
    return self.bTitleMusicPlaying
end

function SoundManager:StopTitleMusic()
    if Audio ~= nil and Audio.StopBGM ~= nil then
        pcall(function()
            Audio.StopBGM()
        end)
    end

    self.bTitleMusicPlaying = false
    self.bContinueEndingCreditsBgm = false
end

function SoundManager:ResetTitleSounds()
    self:StopTitleMusic()
    self.bTitleMusicLoaded = false
    self:RestoreTitleWorldAudio()
end

function SoundManager:ResetGameplaySounds()
    self.PendingDoorCloseSounds = {}
end

function SoundManager:Reset()
    self:ResetGameplaySounds()
    if self.bPreserveBgmOnReset == true then
        self.bPreserveBgmOnReset = false
    else
        self:ResetTitleSounds()
    end
    self.CurrentState = self.State.Title
end

function SoundManager:PlayPartyBlower()
    return self:PlayGameplay(self.PartyBlowerSoundKey, self.PartyBlowerSoundVolume)
end

function SoundManager:PlayEmptyGunShot()
    return self:PlayGameplay(self.EmptyGunShotSoundKey, self.EmptyGunShotSoundVolume)
end

function SoundManager:EnterTitleState()
    self.CurrentState = self.State.Title
    self:ResetGameplaySounds()
    self:MuteTitleWorldAudio()
    return true
end

function SoundManager:ExitTitleState()
    self:StopTitleMusic()
    self:RestoreTitleWorldAudio()
end

function SoundManager:EnterPlayingState()
    self.CurrentState = self.State.Playing
    self:StopTitleMusic()
    self:RestoreTitleWorldAudio()
end

function SoundManager:IsTitleState()
    return self.CurrentState == self.State.Title
end

function SoundManager:IsPlayingState()
    return self.CurrentState == self.State.Playing
end

function SoundManager:MuteTitleWorldAudio()
    self:RestoreTitleWorldAudio()

    local actors = find_world_actors()
    if actors == nil then
        return false
    end

    for _, actor in ipairs(actors) do
        if actor ~= nil and actor.GetComponents ~= nil then
            local okComponents, components = pcall(function()
                return actor:GetComponents()
            end)
            if okComponents and components ~= nil then
                for _, component in ipairs(components) do
                    if is_audio_component(component) then
                        if is_title_actor_audio_component(component) then
                            ensure_title_actor_audio_audible(component)
                        else
                            local okVolume, volume = get_audio_component_volume(component)
                            if okVolume and (mute_audio_component_for_startup(component) or set_audio_component_volume(component, 0.0)) then
                                table.insert(self.TitleMutedAudioComponents, {
                                    Component = component,
                                    Volume = volume
                                })
                            end
                        end
                    end
                end
            end
        end
    end

    self.bTitleWorldAudioMuted = #self.TitleMutedAudioComponents > 0
    return self.bTitleWorldAudioMuted
end

function SoundManager:RestoreTitleWorldAudio()
    for _, entry in ipairs(self.TitleMutedAudioComponents) do
        local component = entry.Component
        if is_audio_component(component) and not restore_audio_component_startup_mute(component) then
            set_audio_component_volume(component, entry.Volume or 1.0)
        end
    end

    self.TitleMutedAudioComponents = {}
    self.bTitleWorldAudioMuted = false
end

return SoundManager
