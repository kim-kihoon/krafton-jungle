local FIREWORKS_SOUND_NAME = "Fireworks"
local FIREWORKS_SOUND_PATH = "Fireworks.mp3"
local FIREWORKS_LOOP_NAME = "TanabataFireworksLoop"
local FIREWORKS_START_DELAY = 3.0
local FIREWORKS_VOLUME = 1.0
local fireworksTimer = 0.0
local bFireworksStarted = false

function BeginPlay()
    AudioManager.PlayBGM("TanjiroNoUta", 0.5, 189.0)
    if not AudioManager.Load(FIREWORKS_SOUND_NAME, FIREWORKS_SOUND_PATH, true) then
        print("[DefaultBGMPlayer] Failed to load " .. FIREWORKS_SOUND_PATH)
    end

    fireworksTimer = 0.0
    bFireworksStarted = false
end

function EndPlay()
    AudioManager.StopLoop(FIREWORKS_LOOP_NAME)
    AudioManager.StopBGM()
end

function Tick(dt)
    if bFireworksStarted then
        return
    end

    fireworksTimer = fireworksTimer + dt
    if fireworksTimer >= FIREWORKS_START_DELAY then
        bFireworksStarted = true
        AudioManager.PlayLoop(FIREWORKS_SOUND_NAME, FIREWORKS_LOOP_NAME, FIREWORKS_VOLUME, 1.0)
    end
end
