-- LuaScriptComponent contract
-- owner: AActor bound from C++
-- otherActor: AActor or nil
-- hit: FHitResult
-- Log(message): writes to the editor console
-- StartCoroutine(function() ... end), wait(seconds): coroutine helpers

local CLEAN_STRENGTH = 100     -- 지울 강도 (uint8, 클수록 빠르게 지워짐)
local REACH_DISTANCE = 300.0  -- 레이캐스트 최대 거리

-- 도구별 오염 누적 속도 (초당 오염도 증가량)
local DIRT_RATE = 0.08

-- 오염도가 높을수록 청소 효율 감소 (0.6 = 완전 오염 시 효율 40% 남음)
local DIRT_POWER_PENALTY = 0.9
local DIRT_POWER_CURVE = 5.0

function BeginPlay(owner)
    print("Player Script Started: ", owner:GetName())
end

function EndPlay(owner)
end

function OnOverlap(owner, otherActor)
end

function OnEndOverlap(owner, otherActor)
end

function OnHit(owner, hit)
end

function OnInteract(owner, interactor)
end

function Tick(owner, deltaTime)
    local hit = RaycastCenter(REACH_DISTANCE)
    if not hit or not hit.bHit then
        print("NoHit")
        return
    end

    local decal = hit:GetDecalComponent()
    if not decal then
        return
    end

    local currentToolId = GetCurrentCleaningToolId()
    if currentToolId ~= "" and GetKey(KEY_LEFT_MOUSE) then
        -- 현재 도구의 오염도
        local dirtKey = "DirtLevel:" .. currentToolId
        local dirt = GetGameState(dirtKey) or 0.0

        -- 오염도에 따라 청소력 감소
        local cleanPower = GetCurrentCleaningToolPower()
        local dirtPenalty = (dirt ^ DIRT_POWER_CURVE) * DIRT_POWER_PENALTY
        local effectivePower = cleanPower * (1.0 - dirtPenalty)
        local cleanStrength = math.floor(CLEAN_STRENGTH * effectivePower + 0.5)

        print("[player] cleanStrength: ", cleanStrength);

        if cleanStrength > 0 then
            decal:PaintAtWorldPos(hit.Location, GetCurrentCleaningToolRadius(), cleanStrength)
        end

        -- 청소할수록 오염도 증가
        dirt = math.min(1.0, dirt + DIRT_RATE * deltaTime)
        SetGameState(dirtKey, dirt)

        local pct = decal:GetCleanPercentage()
        --SetProgress(pct)
    end
end
