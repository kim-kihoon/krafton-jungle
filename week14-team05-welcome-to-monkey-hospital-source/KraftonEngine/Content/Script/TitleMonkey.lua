local READY_ANIMATION_PATH = "Content/Data/CymbalMonkey/CymbalMonkey_Joints_ArmOnlyCymbalEntry.uasset"
local STRIKE_ANIMATION_PATH = "Content/Data/CymbalMonkey/CymbalMonkey_Joints_ArmOnlyCymbalStrike.uasset"
local READY_ANIMATION_PLAY_RATE = 0.1
local STRIKE_ANIMATION_PLAY_RATE = 1.0

local STATE_NONE = 0
local STATE_READY = 1
local STATE_STRIKE = 2

local Mesh = nil
local CurrentState = STATE_NONE

local function cache_mesh()
    if Mesh ~= nil then
        return Mesh
    end

    if obj ~= nil and obj.GetSkeletalMeshComponent ~= nil then
        Mesh = obj:GetSkeletalMeshComponent()
    end

    return Mesh
end

local function play_animation(path, looping, playRate)
    local mesh = cache_mesh()
    if mesh == nil or mesh.PlayAnimationByPath == nil then
        return false
    end

    local ok, result = pcall(function()
        return mesh:PlayAnimationByPath(path, looping)
    end)

    if not ok or result == false then
        return false
    end

    if mesh.SetPlayRate ~= nil then
        pcall(function()
            mesh:SetPlayRate(playRate)
        end)
    end

    return true
end

function Ready()
    if not play_animation(READY_ANIMATION_PATH, false, READY_ANIMATION_PLAY_RATE) then
        return false
    end

    CurrentState = STATE_READY
    return true
end

function Reday()
    return Ready()
end

function Strike()
    if not play_animation(STRIKE_ANIMATION_PATH, false, STRIKE_ANIMATION_PLAY_RATE) then
        return false
    end

    CurrentState = STATE_STRIKE
    return true
end

function PlayStartAnimation()
    return Strike()
end

function BeginPlay()
    cache_mesh()
end

function EndPlay()
    Mesh = nil
    CurrentState = STATE_NONE
end
