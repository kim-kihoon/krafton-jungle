local WASH_SOUND = "Asset/Audio/water-pouring.wav"
local INTERACT_DISTANCE = 3.0
local wasShowingHint = false

function BeginPlay(owner)
    print("[Bucket] BeginPlay: " .. owner:GetName())
end

function EndPlay(owner)
end

function OnOverlap(owner, otherActor)
end

function OnEndOverlap(owner, otherActor)
end

function OnHit(owner, hit)
end

local function getDistance(a, b)
    local dx = a.X - b.X
    local dy = a.Y - b.Y
    local dz = a.Z - b.Z
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function getHeldCleaningToolId()
    local toolId = GetCurrentCleaningToolId()
    if toolId ~= nil and toolId ~= "" then
        return toolId
    end

    toolId = GetHeldToolId()
    if toolId ~= nil and toolId ~= "" then
        return toolId
    end

    return ""
end

local function isPlayerNear(owner)
    local player = GetPlayerActor()
    if player == nil then
        return false
    end

    return getDistance(owner:GetActorLocation(), player:GetActorLocation()) <= INTERACT_DISTANCE
end

function OnInteract(owner, interactor)
    local toolId = getHeldCleaningToolId()
    if toolId == "" then
        print("[Bucket] No held cleaning tool.")
        return
    end

    local key = "DirtLevel:" .. toolId
    local dirt = GetGameState(key) or 0.0
    if dirt > 0.0 then
        SetGameState(key, 0.0)
        PlaySound(WASH_SOUND)
        print("[Bucket] " .. toolId .. " cleaned.")
    else
        print("[Bucket] " .. toolId .. " is already clean.")
    end
end

function Tick(owner, deltaTime)
    local near = isPlayerNear(owner)
    local hasTool = getHeldCleaningToolId() ~= ""
    local canWash = near and hasTool

    if canWash then
        SetInteractionHint("Wash")
        wasShowingHint = true

        if GetKeyDown(KEY_R) then
            OnInteract(owner, GetPlayerActor())
        end
    elseif wasShowingHint then
        SetInteractionHint("None")
        wasShowingHint = false
    end
end
