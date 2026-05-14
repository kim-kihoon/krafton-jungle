-- PrologueCinematic.lua
-- Attach this to one always-active actor with ULuaScriptComponent.
-- It orchestrates the prologue dialogue and camera sequence without RmlUi or C++ scene logic.

local started = false
local CAMERA_MODIFIER_SCRIPT = "Asset/Scripts/PrologueCinematicCamera.lua"

local function FinishCinematic(cameraManager)
    if cameraManager ~= nil then
        cameraManager:ClearManualCameraView()
        cameraManager:ClearLetterBox()
        cameraManager:StopCameraFade()
    end

    SetUIState("InGame")
    SetCinematicInputBlocked(false)
    Log("[PrologueCinematic] Finished.")
end

local function WaitForDialogue(autoAdvanceDelay)
    local delay = autoAdvanceDelay or 1.0

    while IsDialogueActive() do
        if IsDialogueTextComplete() then
            wait(delay)
            AdvanceDialogue()
        else
            wait(0.1)
        end
    end
end

local function WaitForDialogueManual()
    while IsDialogueActive() do
        wait(0.1)
    end
end

local function WaitSeconds(seconds)
    local remaining = seconds
    while remaining > 0.0 do
        local step = math.min(remaining, 0.1)
        wait(step)
        remaining = remaining - step
    end
end

local function PlayCameraMove(cameraManager, toLocation, lookAt, duration, sideOffset)
    cameraManager:StartCameraTransitionLookAtBezier(toLocation, lookAt, duration, 3.0, 0.6, sideOffset or 6.0)
    WaitSeconds(duration)
end

local function RunPrologueCinematic()
    SetCinematicInputBlocked(true)
    Log("[PrologueCinematic] Started.")

    local cameraManager = GetPlayerCameraManager()
    if cameraManager == nil then
        Log("[PrologueCinematic] No player camera manager.")
        SetCinematicInputBlocked(false)
        return
    end

    SetUIState("InGame")
    cameraManager:SetManualCameraFade(FVector.new(0.0, 0.0, 0.0), 1.0)

    ShowDialogue("집주인", "저 방이에요.")
    QueueDialogue("집주인", "오랫동안 손을 못 댔어요.")
    QueueDialogue("집주인", "…청소 부탁드려요.")
    WaitForDialogue(1.2)

    if not AddLuaCameraModifier(CAMERA_MODIFIER_SCRIPT) then
        Log("[PrologueCinematic] Failed to add camera modifier.")
    end

    local pointA = FVector.new(13.8, -10.8, 5.2)
    local lookAtA = FVector.new(-1.3, -1.3, 3.5)
    local pointB = FVector.new(6.0, 6.7, 9.3)
    local lookAtB = FVector.new(-2.8, 1.5, 4.2)
    -- local pointC = FVector.new(-12.4, -7.7, 2.3)
    -- local lookAtC = FVector.new(-4.5, -3.0, 3.8)

    cameraManager:SetManualCameraViewLookAt(pointA, lookAtA)

    cameraManager:SetLetterBox(0.15)
    cameraManager:StartCameraFade(FVector.new(0.0, 0.0, 0.0), 1.0, 0.0, 1.5, false)
    WaitSeconds(1.5)

    PlayCameraMove(cameraManager, pointB, lookAtB, 4.0, 6.0)
    -- PlayCameraMove(cameraManager, pointC, lookAtC, 7.5, -12.0)

    cameraManager:StartCameraFade(FVector.new(0.0, 0.0, 0.0), 0.0, 1.0, 0.8, true)
    WaitSeconds(0.8)
    cameraManager:ClearManualCameraView()
    cameraManager:ClearLetterBox()
    cameraManager:StartCameraFade(FVector.new(0.0, 0.0, 0.0), 1.0, 0.0, 0.6, false)
    WaitSeconds(0.6)

    ShowDialogue("주인공", "들었던 대로 낡고 더러운 방이다.")
    QueueDialogue("주인공", "하지만 귀중한 물건도 보이는 것 같은데...")
    QueueDialogue("주인공", "상자에 보관해 뒀다가 청소가 끝나면 돌려주자.")
    WaitForDialogue(1.2)

    FinishCinematic(cameraManager)
end

function BeginPlay(owner)
    if started then
        return
    end

    started = true
    StartCoroutine(RunPrologueCinematic)
end

function EndPlay(owner)
    SetCinematicInputBlocked(false)
end
