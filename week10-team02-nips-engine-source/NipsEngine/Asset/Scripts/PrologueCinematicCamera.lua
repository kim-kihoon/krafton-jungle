-- PrologueCinematicCamera.lua
-- Loaded through APlayerCameraManager:AddLuaCameraModifier or AddLuaCameraModifier().
-- Keeps the prologue post-process look in Lua while the main prologue script drives timing.

local elapsed = 0.0
local vignetteInTime = 1.5
local vignetteOutStart = 10.5
local vignetteOutEnd = 12.0
local gammaValue = 1.6
local defaultGamma = 1. 

local function Saturate(value)
    if value < 0.0 then return 0.0 end
    if value > 1.0 then return 1.0 end
    return value
end

function OnLoaded()
    elapsed = 0.0
end

function ModifyPostProcess(settings, deltaTime)
    elapsed = elapsed + deltaTime

    local inAlpha = Saturate(elapsed / vignetteInTime)
    local outAlpha = 1.0 - Saturate((elapsed - vignetteOutStart) / (vignetteOutEnd - vignetteOutStart))
    local alpha = math.min(inAlpha, outAlpha)

    settings.VignetteIntensity = 0.55 * alpha
    settings.VignetteRadius = 0.78
    settings.VignetteSoftness = 0.28
    settings.Gamma = elapsed >= vignetteOutEnd and defaultGamma or gammaValue

    return true
end
