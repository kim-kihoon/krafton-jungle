local PlayerCharacter = _G.PlayerCharacter or {}
_G.PlayerCharacter = PlayerCharacter

local VK_W = string.byte("W")
local VK_A = string.byte("A")
local VK_S = string.byte("S")
local VK_D = string.byte("D")
local VK_Q = string.byte("Q")
local VK_SPACE = 0x20

local DASH_SLASH_DISTANCE = 8.0
local DASH_SLASH_DURATION = 0.2
local ATTACK_STEP_FORWARD_DISTANCE = 1.5
local ATTACK_TURN_SPEED = 12.0

local KATANA_MESH_PATH = "Content/Mesh/Katana/source/red cyber katana_StaticMesh.uasset"
local KATANA_SOCKET_NAME = "WeaponR"
local KATANA_PS_PATH = "Content/Data/SwordTrail2.uasset"

local ULTIMATE_CAMERA_BACK_DISTANCE = 7.0
local ULTIMATE_CAMERA_HEIGHT = 10

local ULTIMATE_MOVE_START_DISTANCE = 100.0
local ULTIMATE_MOVE_END_DISTANCE = 30
local ULTIMATE_MOVE_SIDE_OFFSET = -10.0
local ULTIMATE_MOVE_CONTROL_SIDE_OFFSET = 40.0
local ULTIMATE_MOVE_CONTROL_HEIGHT = 1.2
local ULTIMATE_MOVE_DURATION = 0.3
local ULTIMATE_FRAME_STEP = 1.0 / 60.0
local ULTIMATE_MOVE_END_RIGHT_DISTANCE = 5

local ULTIMATE_CAMERA_PITCH_SWING = 0.0
local ULTIMATE_CAMERA_YAW_SWING = 0.0
local ULTIMATE_CAMERA_ROLL_SWING = 2.0
local ULTIMATE_CAMERA_ROTATION_START = 0.5

local function IsKeyDown(vk)
    if Input ~= nil and Input.GetKey ~= nil then
        return Input.GetKey(vk)
    end

    if Input ~= nil and Input.is_key_down ~= nil then
        return Input.is_key_down(vk)
    end

    if Anim ~= nil and Anim.is_key_down ~= nil then
        return Anim.is_key_down(vk)
    end

    if Anim ~= nil and Anim.is_key_pressed ~= nil then
        return Anim.is_key_pressed(vk)
    end

    return false
end

local function IsKeyPressed(vk)
    if Input ~= nil and Input.GetKeyDown ~= nil then
        return Input.GetKeyDown(vk)
    end

    if Input ~= nil and Input.is_key_pressed ~= nil then
        return Input.is_key_pressed(vk)
    end

    if Anim ~= nil and Anim.is_key_pressed ~= nil then
        return Anim.is_key_pressed(vk)
    end

    return false
end

local function GetOwner(ctx)
    if ctx ~= nil and ctx.Owner ~= nil then
        return ctx.Owner
    end

    return obj
end

function PlayerCharacter.AttachKatanaToWeaponSocket(ctx)
    local owner = GetOwner(ctx)
    if owner == nil then
        return
    end

    if PlayerCharacter.KatanaComponent ~= nil and PlayerCharacter.KatanaComponent:IsValid() then
        -- PlayerCharacter 가 전역 Lua Table 에 등록되어있어서 이전 Handle 이 남아있을 수 있음
        if PlayerCharacter.KatanaComponent:GetOwner() == owner then
            print("Katana already exists")
            return
        end

        PlayerCharacter.KatanaComponent = nil
    end

    if owner.GetSkeletalMeshComponent == nil or owner.AddStaticMeshComponent == nil then
        print("Katana attach API not available")
        return
    end

    local meshComp = owner:GetSkeletalMeshComponent()
    if meshComp == nil then
        print("Player skeletal mesh component not found")
        return
    end

    local katana = owner:AddStaticMeshComponent()
    if katana == nil then
        print("Failed to create Katana static mesh component")
        return
    end

    katana:SetMeshPath(KATANA_MESH_PATH)
    katana:AttachToComponentWithSocket(meshComp, KATANA_SOCKET_NAME)
    katana.RelativeLocation = Vector(0.0, 0.0, 0.0)
    katana:SetRotation(Vector(0.0, 0.0, 0.0))
    katana:SetRelativeScale(Vector(1.0, 1.0, 1.0))

    PlayerCharacter.KatanaComponent = katana
end

function PlayerCharacter.SetKatanaTrailActive(active)
    local PSC = PlayerCharacter.KatanaPSC
    if PSC == nil or not PSC:IsValid() then
        return
    end

    if active then
        PSC:Activate()
    else
        PSC:Deactivate()
    end
end

-- Katana 에 Particle System Component 부착
function PlayerCharacter.AttachPSCToWeaponSocket(ctx)
    local owner = GetOwner(ctx)
    if owner == nil then 
        return
    end

    if PlayerCharacter.KatanaPSC ~= nil and PlayerCharacter.KatanaPSC:IsValid() then
        return 
    end

    local meshComp = owner:GetSkeletalMeshComponent()
    if meshComp == nil then 
        print("Player skeletal mesh component not found")
        return
    end

    local PSC = owner:AddParticleSystemComponent()
    if PSC == nil then
        print("Failed to create PSC")
        return
    end

    PSC:SetTemplatePath(KATANA_PS_PATH)
    PSC:SetAnimTrailSourceComponent(meshComp)
    PSC:AttachToComponentWithSocket(meshComp, KATANA_SOCKET_NAME)
    PSC.RelativeLocation = Vector(0.0, 0.0, 0.0)
    PSC:SetRotation(Vector(0.0, 0.0, 0.0))
    PSC:SetRelativeScale(Vector(1.0, 1.0, 1.0))

    PlayerCharacter.KatanaPSC = PSC
    PlayerCharacter.SetKatanaTrailActive(false)

end

local function SetMovementInputEnabled(ctx, enabled)
    if ctx.MovementComp ~= nil then
        Reflection.Call(ctx.MovementComp, "SetMovementInputEnabled", enabled)
    end
end

local function StopMovementImmediately(ctx)
    if ctx.MovementComp ~= nil then
        Reflection.Call(ctx.MovementComp, "StopMovementImmediately")
    end
end

local function SetOrientRotationToMovement(ctx, enabled)
    if ctx.MovementComp ~= nil then
        Reflection.SetProperty(ctx.MovementComp, "bOrientRotationToMovement", enabled)
    end
end

function PlayerCharacter.Initialize(ctx, owner)
    ctx.Owner = owner
    ctx.LastMoveInputDirection = nil
    ctx.DashSlashPrevOrientRotationToMovement = nil
    ctx.DashSlashMoveDirection = nil

    ctx.MovementComp = nil
    if owner ~= nil then
        Reflection.SetProperty(owner, "bUseControllerRotationYaw", false)

        if owner.GetCharacterMovement ~= nil then
            ctx.MovementComp = owner:GetCharacterMovement()
        end
        if ctx.MovementComp == nil and owner.GetFloatingPawnMovement ~= nil then
            ctx.MovementComp = owner:GetFloatingPawnMovement()
        end
    end
end

local function BuildGroundDecalAABBScale3(a, b, c, padding, minSize, projectionDepth)
    padding = padding or 5.0
    minSize = minSize or 8.0
    projectionDepth = projectionDepth or 16.0

    local minX = math.min(a.X, b.X, c.X)
    local maxX = math.max(a.X, b.X, c.X)
    local minY = math.min(a.Y, b.Y, c.Y)
    local maxY = math.max(a.Y, b.Y, c.Y)

    local sizeX = math.max((maxX - minX) + padding * 2.0, minSize)
    local sizeY = math.max((maxY - minY) + padding * 2.0, minSize)

    local center = Vector(
        (minX + maxX) * 0.5,
        (minY + maxY) * 0.5,
        a.Z 
    )

    return center, Vector(sizeX, sizeY, projectionDepth)
end

function PlayerCharacter.SetMovementInputEnabled(ctx, enabled)
    SetMovementInputEnabled(ctx, enabled)
end

function PlayerCharacter.StopMovementImmediately(ctx)
    StopMovementImmediately(ctx)
end

function PlayerCharacter.StepAttackForward(ctx)
    local owner = GetOwner(ctx)
    if owner == nil then
        return
    end

    local forward = PlayerCharacter.GetOwnerForward2D(ctx)
    if forward == nil then
        return
    end

    Reflection.Call(owner, "AddActorWorldOffset", forward * ATTACK_STEP_FORWARD_DISTANCE)
end

function PlayerCharacter.GetMoveInputWorldDirection(ctx)
    local owner = GetOwner(ctx)
    if owner == nil then
        return nil
    end

    local moveForward = 0.0
    local moveRight = 0.0

    if IsKeyDown(VK_W) then moveForward = moveForward + 1.0 end
    if IsKeyDown(VK_S) then moveForward = moveForward - 1.0 end
    if IsKeyDown(VK_D) then moveRight = moveRight + 1.0 end
    if IsKeyDown(VK_A) then moveRight = moveRight - 1.0 end

    if math.abs(moveForward) < 0.001 and math.abs(moveRight) < 0.001 then
        return nil
    end

    local forward = nil
    local right = nil
    local controlRot = Reflection.Call(owner, "GetControlRotation")

    if controlRot ~= nil then
        local yawRad = controlRot.Z * math.pi / 180.0
        forward = Vector(math.cos(yawRad), math.sin(yawRad), 0.0)
        right = Vector(-math.sin(yawRad), math.cos(yawRad), 0.0)
    else
        forward = Reflection.Call(owner, "GetActorForward")
        right = Reflection.Call(owner, "GetActorRight")

        if forward == nil or right == nil then
            return nil
        end

        forward.Z = 0.0
        right.Z = 0.0
    end

    local dir = forward * moveForward + right * moveRight
    dir.Z = 0.0

    if dir:Length() <= 0.001 then
        return nil
    end

    return dir:Normalized()
end

function PlayerCharacter.GetOwnerForward2D(ctx)
    local owner = GetOwner(ctx)
    if owner == nil then
        return nil
    end

    local dir = Reflection.Call(owner, "GetActorForward")
    if dir == nil then
        return nil
    end

    dir.Z = 0.0

    if dir:Length() <= 0.001 then
        return nil
    end

    return dir:Normalized()
end

function PlayerCharacter.ResolveDashDirection(ctx)
    local dir = PlayerCharacter.GetMoveInputWorldDirection(ctx)

    if dir ~= nil then
        return dir
    end

    if ctx.LastMoveInputDirection ~= nil then
        return ctx.LastMoveInputDirection
    end

    return PlayerCharacter.GetOwnerForward2D(ctx)
end

function PlayerCharacter.FaceOwnerToDirection(ctx, dir)
    local owner = GetOwner(ctx)
    if owner == nil or dir == nil then
        return
    end

    local targetYaw = math.atan2(dir.Y, dir.X) * 180.0 / math.pi
    Reflection.Call(owner, "SetActorRotation", Vector(0.0, 0.0, targetYaw))
end

function PlayerCharacter.SmoothFaceOwnerToDirection(ctx, dir, dt)
    local owner = GetOwner(ctx)
    if owner == nil or dir == nil or dt == nil then
        return
    end

    local currentRot = Reflection.Call(owner, "GetActorRotation")
    if currentRot == nil then
        PlayerCharacter.FaceOwnerToDirection(ctx, dir)
        return
    end

    local targetYaw = math.atan2(dir.Y, dir.X) * 180.0 / math.pi
    local deltaYaw = (targetYaw - currentRot.Z + 180.0) % 360.0 - 180.0
    local alpha = dt * ATTACK_TURN_SPEED
    if alpha > 1.0 then
        alpha = 1.0
    end
    local nextYaw = currentRot.Z + deltaYaw * alpha

    Reflection.Call(owner, "SetActorRotation", Vector(currentRot.X, currentRot.Y, nextYaw))
end

local function Clamp(v, minValue, maxValue)
    if v < minValue then return minValue end
    if v > maxValue then return maxValue end
    return v
end

local function EaseOutCubic(t)
    local u = 1.0 - t
    return 1.0 - u * u * u
end

local function Bezier2(a, b, c, t)
    local u = 1.0 - t
    return a * (u * u) + b * (2.0 * u * t) + c * (t * t)
end

local function GetUltimateCameraRotation(baseRotation, t)
    if t <= ULTIMATE_CAMERA_ROTATION_START then
        return baseRotation
    end

    local rotationT = Clamp((t - ULTIMATE_CAMERA_ROTATION_START) / (1.0 - ULTIMATE_CAMERA_ROTATION_START), 0.0, 1.0)

    local pitch =
        baseRotation.X
        - math.sin(rotationT * math.pi) * ULTIMATE_CAMERA_PITCH_SWING
        + math.sin(rotationT * math.pi * 4.0) * (ULTIMATE_CAMERA_PITCH_SWING * 0.35)

    local roll = baseRotation.Y + math.sin(rotationT * math.pi * 4.0) * ULTIMATE_CAMERA_ROLL_SWING
    local yaw = baseRotation.Z + math.sin(rotationT * math.pi * 2.0) * ULTIMATE_CAMERA_YAW_SWING

    return Vector(pitch, roll, yaw)
end

local function GetSafeOwnerBasis(owner)
    local forward = Reflection.Call(owner, "GetActorForward")
    if forward == nil then
        return nil, nil
    end

    forward.Z = 0.0
    if forward:Length() <= 0.001 then
        return nil, nil
    end
    forward = forward:Normalized()

    local right = Reflection.Call(owner, "GetActorRight")
    if right == nil then
        right = Vector(-forward.Y, forward.X, 0.0)
    else
        right.Z = 0.0
        if right:Length() <= 0.001 then
            right = Vector(-forward.Y, forward.X, 0.0)
        else
            right = right:Normalized()
        end
    end

    return forward, right
end

function PlayerCharacter.ApplyMoveInput(ctx)
    local owner = GetOwner(ctx)
    if owner == nil then
        return
    end

    local dir = PlayerCharacter.GetMoveInputWorldDirection(ctx)
    if dir ~= nil then
        ctx.LastMoveInputDirection = dir
        Reflection.Call(owner, "AddMovementInput", dir, 1.0)
    end

    if IsKeyPressed(VK_SPACE) then
        Reflection.Call(owner, "Jump")
    end
end

function PlayerCharacter.BeginDashSlash(ctx)
    if ctx.MovementComp ~= nil then
        ctx.DashSlashPrevOrientRotationToMovement =
            Reflection.GetProperty(ctx.MovementComp, "bOrientRotationToMovement")
        SetOrientRotationToMovement(ctx, false)
    end

    SetMovementInputEnabled(ctx, false)

    local dashDir = PlayerCharacter.ResolveDashDirection(ctx)
    PlayerCharacter.FaceOwnerToDirection(ctx, dashDir)
    ctx.DashSlashMoveDirection = dashDir

    ctx.DashSlashActive = true
    ctx.DashSlashElapsed = 0.0
    ctx.DashSlashEnd = false

    PlayerCharacter.SetKatanaTrailActive(true)
end

function PlayerCharacter.EndDashSlash(ctx)
    ctx.DashSlashActive = false
    ctx.DashSlashElapsed = 0.0
    ctx.DashSlashEnd = false

    if ctx.DashSlashPrevOrientRotationToMovement ~= nil then
        SetOrientRotationToMovement(ctx, ctx.DashSlashPrevOrientRotationToMovement)
        ctx.DashSlashPrevOrientRotationToMovement = nil
    end

    ctx.DashSlashMoveDirection = nil

    SetMovementInputEnabled(ctx, true)
    PlayerCharacter.SetKatanaTrailActive(false)
end

function PlayerCharacter.UpdateDashSlash(ctx, dt)
    local owner = GetOwner(ctx)
    if owner == nil or ctx.DashSlashMoveDirection == nil then
        return
    end

    ctx.DashSlashElapsed = ctx.DashSlashElapsed + dt

    local dir = ctx.DashSlashMoveDirection
    dir.Z = 0.0

    if dir:Length() > 0.001 then
        local moveSpeed = DASH_SLASH_DISTANCE / DASH_SLASH_DURATION
        Reflection.Call(owner, "AddActorWorldOffset", dir:Normalized() * moveSpeed * dt)
    end

    if ctx.DashSlashElapsed >= DASH_SLASH_DURATION then
        ctx.DashSlashEnd = true
    end
end

function PlayerCharacter.BeginUltimate()
    print("Begin Ultimate")

    if PlayerCharacter.IsUltimateRunning then
        return
    end

    PlayerCharacter.IsUltimateRunning = true
    PlayerCharacter.IsInUltimateMode = false

    local owner = obj
    if owner == nil then
        PlayerCharacter.IsUltimateRunning = false
        return
    end

    local movementComp = owner:GetCharacterMovement()
    if movementComp == nil then
        print("Movement not found")
        PlayerCharacter.IsUltimateRunning = false
        return
    end

    local ultimateCamera = World.FindFirstActorByTag("UltimateCamera")
    if ultimateCamera == nil then
        print("UltimateCamera not found")
        PlayerCharacter.IsUltimateRunning = false
        return
    end

    local actorLocation = Reflection.Call(owner, "GetActorLocation")
    local actorRotation = Reflection.Call(owner, "GetActorRotation")
    local actorForward, actorRight = GetSafeOwnerBasis(owner)

    if actorLocation == nil or actorForward == nil or actorRight == nil then
        print("Invalid owner transform")
        PlayerCharacter.IsUltimateRunning = false
        return
    end

    -- 궁극기 종료 후 되돌릴 실제 게임플레이 위치/회전
    local originalLocation = actorLocation
    local originalRotation = actorRotation

    local PrimComp = owner:GetPrimitiveComponent()
    local PrevSimulatePhysics = false

    if PrimComp ~= nil then
        PrevSimulatePhysics = Reflection.Call(PrimComp, "GetSimulatePhysics")
        Reflection.Call(PrimComp, "SetSimulatePhysics", false)
    end

    local up = Vector(0.0, 0.0, 1.0)

    -- 카메라 위치:
    -- 기존 느낌 유지. 플레이어 뒤쪽 + 위에서 플레이어 진행 방향을 바라본다.
    local cameraLocation =
        actorLocation
        - actorForward * ULTIMATE_CAMERA_BACK_DISTANCE
        + up * ULTIMATE_CAMERA_HEIGHT

    local cameraYaw = math.atan2(actorForward.Y, actorForward.X) * 180.0 / math.pi
    local baseCameraRotation = Vector(-10.0, 15.0, cameraYaw)

    Reflection.Call(ultimateCamera, "SetActorLocation", cameraLocation)
    Reflection.Call(ultimateCamera, "SetActorRotation", GetUltimateCameraRotation(baseCameraRotation, 0.0))

    -- 카메라 전환
    CameraManager.ToggleOwnerCamera(ultimateCamera, 0)
    Reflection.Call(movementComp, "StopMovementImmediately")
    Reflection.Call(movementComp, "SetMovementInputEnabled", false)

    -- 카메라 블렌드/구도 안정화 대기
    Wait(0.15)

    -- 카메라 기준 곡선 이동 경로.
    -- 기존 카메라 느낌 유지를 위해 start/end/control은 기존 방식 그대로 둔다.
    local startPos =
        cameraLocation
        + actorForward * ULTIMATE_MOVE_START_DISTANCE
        + actorRight * ULTIMATE_MOVE_SIDE_OFFSET

    startPos.Z = actorLocation.Z

    local cinematicEndPos =
        cameraLocation
        + actorForward * ULTIMATE_MOVE_END_DISTANCE
        + actorRight * ULTIMATE_MOVE_END_RIGHT_DISTANCE

    cinematicEndPos.Z = actorLocation.Z

    local controlPos =
        cameraLocation
        + actorForward * ((ULTIMATE_MOVE_START_DISTANCE + ULTIMATE_MOVE_END_DISTANCE) * 0.5)
        + actorRight * ULTIMATE_MOVE_CONTROL_SIDE_OFFSET

    controlPos.Z = actorLocation.Z

    local decalPos, decalScale = BuildGroundDecalAABBScale3(
        startPos,
        controlPos,
        cinematicEndPos,
        0.0,
        0.0,
        16.0
    )

    Reflection.Call(owner, "SetActorLocation", startPos)

    local elapsed = 0.0
    local prevPos = startPos

    PlayerCharacter.IsInUltimateMode = true
    PlayerCharacter.SetKatanaTrailActive(true)

    while elapsed < ULTIMATE_MOVE_DURATION do
        Wait(ULTIMATE_FRAME_STEP)

        elapsed = elapsed + ULTIMATE_FRAME_STEP

        local t = Clamp(elapsed / ULTIMATE_MOVE_DURATION, 0.0, 1.0)
        local easedT = EaseOutCubic(t)

        local nextPos = Bezier2(startPos, controlPos, cinematicEndPos, easedT)
        nextPos.Z = actorLocation.Z

        Reflection.Call(ultimateCamera, "SetActorRotation", GetUltimateCameraRotation(baseCameraRotation, t))
        Reflection.Call(owner, "SetActorLocation", nextPos)

        -- 이동 방향을 바라보게 회전
        local moveDir = nextPos - prevPos
        moveDir.Z = 0.0

        if moveDir:Length() > 0.001 then
            PlayerCharacter.FaceOwnerToDirection(nil, moveDir:Normalized())
        end

        prevPos = nextPos
    end

    local decal = VFX.SpawnGroundCrackDecal(
        "Content/Material/VFX/GroundCrack.mat",
        cinematicEndPos,
        decalScale,
        0.35,
        0.45
    )

    if decal ~= nil then
        decal:SetColorRGBA(1.0, 0.05, 0.02, 1.0)
    end

    -- 연출상 도착 지점은 기존처럼 카메라 앞쪽
    Reflection.Call(owner, "SetActorLocation", cinematicEndPos)

    -- 도착 후 임시 연출
    CameraManager.StartWaveShake(1.0)
    -- CameraManager.FadeOut(0.2)
    -- CameraManager.SetVignette(0.7, 0.7, 0.5)

    Wait(0.4)

    Reflection.Call(movementComp, "SetMovementInputEnabled", true)

    -- 복귀 위치 기준으로 플레이어 카메라 재전환
    CameraManager.ToggleOwnerCamera(owner, 0.4)

    if PrimComp ~= nil then
        Reflection.Call(PrimComp, "SetSimulatePhysics", PrevSimulatePhysics)
    end

    PlayerCharacter.IsInUltimateMode = false
    PlayerCharacter.IsUltimateRunning = false
    PlayerCharacter.SetKatanaTrailActive(false)

    print("End Ultimate")
end

function BeginPlay()
    PlayerCharacter.IsUltimateRunning = false
    PlayerCharacter.IsInUltimateMode = false
    PlayerCharacter.AttachKatanaToWeaponSocket(PlayerCharacter)
    PlayerCharacter.AttachPSCToWeaponSocket(PlayerCharacter)
    print("[BeginPlay] " .. obj.UUID)
end

function EndPlay()
    -- 전역 Lua 테이블 데이터 초기화 -> 안정성 강화
    PlayerCharacter.KatanaComponent = nil
    PlayerCharacter.KatanaPSC = nil
    print("[EndPlay] " .. obj.UUID)
end

function OnOverlap(OtherActor)
end

function Tick(dt)
    UpdateCoroutines(dt)

    if not PlayerCharacter.IsUltimateRunning and Input.GetKeyDown(VK_Q) then
        StartCoroutine(PlayerCharacter.BeginUltimate)
    end
end

return PlayerCharacter
