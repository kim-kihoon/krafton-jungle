-- 오염도: 0.0 = 깨끗함, 1.0 = 완전히 더러움

local DIRT_KEY = "DirtLevel:sponge"
local lastDirt = -1.0

local function applyDirtColor(owner, dirt)
    local brightness = 1.0 - dirt * 0.75
    SetAllSubUVTints(owner, brightness, brightness * 0.9, brightness * 0.8)
end

function BeginPlay(owner)
    if owner == nil then
        print("[Sponge] BeginPlay owner=nil")
        return
    end

    local registered = RegisterCleaningToolActor(owner, "sponge")
    print("[Sponge] Register result=" .. tostring(registered))

    -- 게임 시작 시 항상 오염도 초기화
    local dirt = 0.0
    SetGameState(DIRT_KEY, dirt)
    applyDirtColor(owner, dirt)
    lastDirt = dirt
end

function Tick(owner, deltaTime)
    local dirt = GetGameState(DIRT_KEY) or 0.0

    if math.abs(dirt - lastDirt) > 0.001 then
        applyDirtColor(owner, dirt)
        lastDirt = dirt
    end
end
