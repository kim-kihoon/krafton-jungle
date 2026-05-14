local FALL_CAMERA_PITCH_DEGREES = 89.0
local FALL_CAMERA_DURATION_SECONDS = 2.0
local TRIP_COOLDOWN_SECONDS = 5.0
local SLOMO_DILATION = 0.4
local SLOMO_HOLD_SECONDS = 0.35
local SLOMO_BLEND_IN_SECONDS = 0.25
local SLOMO_BLEND_OUT_SECONDS = 0.45
local CONTROL_LOCK_AFTER_FALL_SECONDS = 1.0

local tripped = false
local cooldownRemaining = 0.0
local lastDeltaTime = 0.016

local function isPlayer(actor)
    if actor == nil then
        return false
    end

    local player = GetPlayerActor()
    if player ~= nil and actor:GetUUID() == player:GetUUID() then
        return true
    end

    local name = actor:GetName()
    return string.find(name, "Player") ~= nil or string.find(name, "Pawn") ~= nil
end

local function smoothPitchCameraUp(player)
    local camera = GetCameraComponent(player)
    if camera == nil then
        print("[TripDecal] Player has no camera component.")
        return false
    end

    TriggerKnockback(
        player,
        FVector.new(0.0, 0.0, 0.0),
        0.0,
        FALL_CAMERA_DURATION_SECONDS + CONTROL_LOCK_AFTER_FALL_SECONDS)

    StartCoroutine(function()
        local elapsed = 0.0
        local previousPitch = camera:GetPitchDegrees()
        local startPitch = previousPitch

        while elapsed < FALL_CAMERA_DURATION_SECONDS do
            yield()
            elapsed = math.min(FALL_CAMERA_DURATION_SECONDS, elapsed + lastDeltaTime)

            local alpha = elapsed / FALL_CAMERA_DURATION_SECONDS
            alpha = 1.0 - ((1.0 - alpha) * (1.0 - alpha))

            local targetPitch = startPitch + (FALL_CAMERA_PITCH_DEGREES - startPitch) * alpha
            camera:AddPitchInput(targetPitch - previousPitch)
            previousPitch = targetPitch
        end

        camera:AddPitchInput(FALL_CAMERA_PITCH_DEGREES - camera:GetPitchDegrees())
    end)

    return true
end

local function tripPlayer(owner, player)
    if cooldownRemaining > 0.0 then
        print("[TripDecal] Trip ignored during cooldown.")
        return
    end

    if tripped or not isPlayer(player) then
        return
    end

    tripped = true
    cooldownRemaining = TRIP_COOLDOWN_SECONDS
    if owner ~= nil then
        SetActorCollisionEnabled(owner, false)
    end

    StartSlomo(SLOMO_DILATION, SLOMO_HOLD_SECONDS, SLOMO_BLEND_IN_SECONDS, SLOMO_BLEND_OUT_SECONDS)

    if not smoothPitchCameraUp(player) then
        tripped = false
        cooldownRemaining = 0.0
        if owner ~= nil then
            SetActorCollisionEnabled(owner, true)
        end
        return
    end

    print("[TripDecal] Player tripped.")
end

function BeginPlay(owner)
    if owner ~= nil then
        print("[TripDecal] Ready: " .. owner:GetName())
    end
end

function OnHit(owner, hit)
    local ownerName = owner ~= nil and owner:GetName() or "nil"
    local hitActor = hit ~= nil and hit:GetHitActor() or nil
    local hitActorName = hitActor ~= nil and hitActor:GetName() or "nil"
    print("[TripDecal] OnHit owner=" .. ownerName .. " hitActor=" .. hitActorName)

    tripPlayer(owner, hitActor)
end

function OnInteract(owner, interactor)
end

function Tick(owner, deltaTime)
    lastDeltaTime = deltaTime

    if cooldownRemaining > 0.0 then
        cooldownRemaining = math.max(0.0, cooldownRemaining - deltaTime)
        if cooldownRemaining == 0.0 then
            tripped = false
            SetActorCollisionEnabled(owner, true)
        end
    end
end
