-- CharacterAnimStateMachine.lua
-- speed(), jumpState() 는 C++에서 BindProperty로 주입된 함수
--
-- jumpState() 반환값 (ACharacter::GetJumpState):
--   0 = Ground  : 지상 (LandingTimer 소진 포함)
--   1 = Rise    : 상승 중 (Z 속도 > 0)
--   2 = Fall    : 하강 중 (Z 속도 <= 0)
--   3 = Land    : 착지 직후 (C++ LandingTimer 소진 전)

------------------------------------------------------------------------
-- 상태 정의
------------------------------------------------------------------------
local STATES = {
    Idle = "Idle",
    Walk = "Walk",
    Jump = "Jump",
    Fall = "Fall",
    Land = "Land",
}

local STATE_SEQUENCE = {
    [STATES.Idle] = "Asset/Mesh/Manny/Anims/MM_Idle_AnimSequence.uasset",
    [STATES.Walk] = "Asset/Mesh/Manny/Anims/MF_Unarmed_Walk_Fwd_AnimSequence.uasset",
    [STATES.Jump] = "Asset/Mesh/Manny/Anims/MM_Jump_AnimSequence.uasset",
    [STATES.Fall] = "Asset/Mesh/Manny/Anims/MM_Fall_Loop_AnimSequence.uasset",
    [STATES.Land] = "Asset/Mesh/Manny/Anims/MM_Land_AnimSequence.uasset",
}

------------------------------------------------------------------------
-- 헬퍼
------------------------------------------------------------------------
local function isAirborne()
    local js = jumpState()
    return js == 1 or js == 2   -- Rise 또는 Fall
end

------------------------------------------------------------------------
-- 전환 규칙
------------------------------------------------------------------------
local TRANSITIONS = {
    -- ── 지상 이동 ──────────────────────────────────────────────────
    { from = STATES.Idle, to = STATES.Walk,
      condition = function() return speed() >  0.5 end },
    { from = STATES.Walk, to = STATES.Idle,
      condition = function() return speed() <= 0.5 end },

    -- ── 지상 → 공중 ────────────────────────────────────────────────
    { from = STATES.Idle, to = STATES.Jump,
      condition = isAirborne },
    { from = STATES.Walk, to = STATES.Jump,
      condition = isAirborne },

    -- ── 공중 → 착지 ────────────────────────────────────────────────
    -- LandingTimer > 0 : Land 애니메이션 재생
    { from = STATES.Jump, to = STATES.Fall,
      condition = function() return jumpState() == 2 end },
    -- LandingTimer 없이 즉시 Ground 로 복귀하는 엣지케이스
    { from = STATES.Fall, to = STATES.Land,
      condition = function() return jumpState() == 0 end },

    -- ── 착지 종료 (LandingTimer 소진) ──────────────────────────────
    { from = STATES.Land, to = STATES.Idle,
      condition = function() return speed() <= 0.5 end },
    { from = STATES.Land, to = STATES.Walk,
      condition = function() return speed() >  0.5 end },
    -- 착지 도중 연속 점프
    { from = STATES.Land, to = STATES.Jump,
      condition = isAirborne },
}

------------------------------------------------------------------------
-- 콜백
------------------------------------------------------------------------
function onInit()
    self:transitionTo(STATES.Idle)
    print("[AnimSM] onInit -> " .. STATES.Idle)
end

function onUpdate(dt)
    local cur = self:getCurrentState()
    for _, rule in ipairs(TRANSITIONS) do
        if rule.from == cur and rule.condition() then
            self.blendDuration = 0.15
            self:transitionTo(rule.to)
            break
        end
    end
end

function onTransition(newState)
    local seqName = STATE_SEQUENCE[newState]
    if seqName then
        self:setSequence(seqName)
    end
    print("[AnimSM] -> " .. newState)
end
