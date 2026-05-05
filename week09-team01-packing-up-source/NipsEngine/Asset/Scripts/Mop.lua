-- 오염도: 0.0 = 깨끗함, 1.0 = 완전히 더러움
-- SetGameState / GetGameState 로 Player.lua / Bucket.lua 와 공유

local DIRT_KEY = "DirtLevel:mop"
local lastDirt = -1.0

local function applyDirtColor(owner, dirt)
    -- 더러울수록 어두워짐 (최소 밝기 0.25)
    local brightness = 1.0 - dirt * 0.75
    SetAllSubUVTints(owner, brightness, brightness * 0.9, brightness * 0.8)
end

function BeginPlay(owner)
    if owner == nil then
        print("[Mop] BeginPlay owner=nil")
        return
    end

    local registered = RegisterCleaningToolActor(owner, "mop")
    print("[Mop] Register result=" .. tostring(registered))

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
