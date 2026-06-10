local GameManager = require("GameManager")
local SoundManager = require("SoundManager")

local DoorManager = {}

local DOOR_OPEN_DURATION = 1.0
local DOOR_OPEN_ANGLE_PLUS = 80.0
local DOOR_OPEN_ANGLE_MINUS = -80.0
local DOOR_OPEN_SOUND_KEY = "DoorOpen"
local HEAVY_DOOR_OPEN_SOUND_KEY = "HeavyDoorOpen"
local DOOR_CONTACT_SLOP = 0.08
local DOOR_CONTACT_RAY_COUNT = 16
local DOOR_APPROACH_DOT_THRESHOLD = 1.0e-6
local INTERACT_DISTANCE = 1.0
local AUTO_CLOSE_DOOR_TRIGGER_X_MAX = -0.603
local AUTO_CLOSE_DOOR_TRIGGER_Y_MIN = 27.119
local AUTO_CLOSE_Y_DOOR_TRIGGER_Y_MIN = -2.0
local MAX_OPEN_SINGLE_DOORS_ON_WARP = 5
local CHAOS_DOOR_REMAINING_MAX = 90.0
local CHAOS_DOOR_REMAINING_MIN = 30.0
local CHAOS_DOOR_MIN_COUNT = 1
local CHAOS_DOOR_MAX_COUNT = 3
local CHAOS_DOOR_MIN_TIME_GAP = 2.0
local CHAOS_DOOR_WARP_ROLL_CHANCE = 0.5
local TOY_PROJECTILE_TAG = "ToyProjectile"
local ENTRY_DOOR_TAG = "DoorEntry"

local OPEN_PLUS_NAMES = {
    AStaticMeshActor_2 = true,
    AStaticMeshActor_2_Copy = true,
    AStaticMeshActor_4_Copy = true,
    AStaticMeshActor_23_Copy = true,
    AStaticMeshActor_24_Copy = true,
    AStaticMeshActor_13 = true,
    AStaticMeshActor_17 = true,
}

local OPEN_MINUS_NAMES = {
    AStaticMeshActor_12 = true,
    AStaticMeshActor_3 = true,
    AStaticMeshActor_4 = true,
    AStaticMeshActor_23 = true,
    AStaticMeshActor_24 = true,
    AStaticMeshActor_16 = true,
}

local INITIALLY_OPEN_NAMES = {
    AStaticMeshActor_3 = true,
    AStaticMeshActor_4_Copy = true,
    AStaticMeshActor_24 = true,
}

local DOUBLE_DOOR_NAMES = {
    AStaticMeshActor_12 = true,
    AStaticMeshActor_13 = true,
    AStaticMeshActor_16 = true,
    AStaticMeshActor_17 = true,
}

local AUTO_CLOSE_DOOR_NAMES = {
    AStaticMeshActor_16 = true,
    AStaticMeshActor_17 = true,
}

local EXIT_DOOR_NAMES = {
    AStaticMeshActor_16 = true,
    AStaticMeshActor_17 = true,
}

local EXIT_GUIDE_DECAL_ACTOR_NAME = "ADecalActor_1"

local AUTO_CLOSE_Y_DOOR_NAMES = {
    AStaticMeshActor_12 = true,
    AStaticMeshActor_13 = true,
}

local CYMBAL_TRIGGER_DOOR_NAMES = {
    AStaticMeshActor_12 = true,
    AStaticMeshActor_13 = true,
}

DoorManager.Doors = {}
DoorManager.DoorStateByName = {}
DoorManager.bDoorsInitialized = false
DoorManager.bExitDoorsUnlockedForCurrentLoop = false
DoorManager.PendingChaosDoorEvents = {}
DoorManager.bChaosDoorScheduleActive = false
DoorManager.bExitGuideDecalRemoved = false

local function is_entry_door(door)
    if door == nil or door.Actor == nil or door.Actor.HasTag == nil then
        return false
    end

    local ok, bHasTag = pcall(function()
        return door.Actor:HasTag(ENTRY_DOOR_TAG)
    end)
    return ok and bHasTag == true
end

local function actor_name(actor)
    if actor == nil then
        return ""
    end

    local ok, name = pcall(function()
        return actor:GetName()
    end)
    if not ok or name == nil then
        return ""
    end
    return name
end

local function get_actor_yaw(actor)
    if actor == nil then
        return 0.0
    end

    local okRoot, root = pcall(function()
        return actor:GetRootPrimitiveComponent()
    end)
    if okRoot and root ~= nil then
        local okRot, rotation = pcall(function()
            return root:GetRotation()
        end)
        if okRot and rotation ~= nil and rotation.Z ~= nil then
            return rotation.Z
        end
    end

    local okRot, rotation = pcall(function()
        return actor.Rotation
    end)
    if okRot and rotation ~= nil and rotation.Z ~= nil then
        return rotation.Z
    end

    return 0.0
end

local function sync_door_physics(actor)
    if actor == nil then
        return
    end

    local ok, root = pcall(function()
        return actor:GetRootPrimitiveComponent()
    end)
    if not ok or root == nil then
        return
    end

    pcall(function()
        root:SyncPhysicsTransform()
    end)
end

local function set_door_yaw(door, yaw)
    if door == nil or door.Actor == nil then
        return false
    end

    local rotation = Vec3(0.0, 0.0, yaw)
    local actor = door.Actor

    local ok = pcall(function()
        actor.Rotation = rotation
    end)
    if not ok then
        ok = pcall(function()
            actor:SetRotation(rotation)
        end)
    end

    local okRoot, root = pcall(function()
        return actor:GetRootPrimitiveComponent()
    end)
    if okRoot and root ~= nil then
        pcall(function()
            root:SetRotation(rotation)
        end)
    end

    if ok then
        door.CurrentYaw = yaw
    end
    return ok
end

local function smooth_step(alpha)
    alpha = math.max(0.0, math.min(alpha, 1.0))
    return alpha * alpha * (3.0 - 2.0 * alpha)
end

local function sync_player_physics(player)
    if player == nil then
        return
    end

    local okRoot, root = pcall(function()
        return player:GetRootPrimitiveComponent()
    end)
    if okRoot and root ~= nil then
        pcall(function()
            root:SyncPhysicsTransform()
        end)
    end
end

local function get_player_capsule_radius(player)
    if player == nil then
        return 0.213333
    end

    local okCapsule, capsule = pcall(function()
        return player:GetCapsuleComponent()
    end)
    if okCapsule and capsule ~= nil then
        local okRadius, radius = pcall(function()
            return capsule:GetScaledCapsuleRadius()
        end)
        if okRadius and radius ~= nil and radius > 0.0 then
            return radius
        end
    end
    return 0.213333
end

local function get_player_door_contact(player, doorActor)
    if World == nil or World.LineTraceObjects == nil or player == nil or doorActor == nil then
        return false, nil, nil
    end

    local okPlayerLoc, playerLoc = pcall(function()
        return player:GetLocation()
    end)
    if not okPlayerLoc or playerLoc == nil then
        return false, nil, nil
    end

    local radius = get_player_capsule_radius(player)
    local probeDistance = radius + DOOR_CONTACT_SLOP
    local twoPi = math.pi * 2.0
    local bestDistance = probeDistance + 1.0
    local bestLocation = nil
    local bestNormal = nil

    for rayIndex = 0, DOOR_CONTACT_RAY_COUNT - 1 do
        local angle = (rayIndex / DOOR_CONTACT_RAY_COUNT) * twoPi
        local dirX = math.cos(angle)
        local dirY = math.sin(angle)
        local endPos = Vec3(
            playerLoc.X + dirX * probeDistance,
            playerLoc.Y + dirY * probeDistance,
            playerLoc.Z
        )

        local okHit, hit = pcall(function()
            return World.LineTraceObjects(playerLoc, endPos, player)
        end)
        if okHit and hit ~= nil and hit.Hit == true and hit.Actor == doorActor then
            local hitDistance = tonumber(hit.Distance) or probeDistance
            if hitDistance <= probeDistance and hitDistance < bestDistance then
                bestDistance = hitDistance
                bestLocation = hit.Location
                bestNormal = hit.Normal
            end
        end
    end

    if bestLocation == nil or bestNormal == nil then
        return false, nil, nil
    end

    return true, bestLocation, bestNormal
end

local function is_door_approaching_player(doorActor, yawDelta, contactLocation, contactNormal)
    if doorActor == nil or contactLocation == nil or contactNormal == nil then
        return false
    end
    if math.abs(yawDelta) < 0.001 then
        return false
    end

    local okDoorLoc, doorLoc = pcall(function()
        return doorActor:GetLocation()
    end)
    if not okDoorLoc or doorLoc == nil then
        return false
    end

    local rx = contactLocation.X - doorLoc.X
    local ry = contactLocation.Y - doorLoc.Y
    local yawDeltaRad = math.rad(yawDelta)
    local velX = -yawDeltaRad * ry
    local velY = yawDeltaRad * rx
    local approachDot = velX * contactNormal.X + velY * contactNormal.Y

    return approachDot > DOOR_APPROACH_DOT_THRESHOLD
end

local function push_player_from_door_hinge(player, doorActor, prevYaw, newYaw)
    if player == nil or doorActor == nil then
        return
    end

    local yawDelta = newYaw - prevYaw
    if math.abs(yawDelta) < 0.001 then
        return
    end

    local okPlayerLoc, playerLoc = pcall(function()
        return player:GetLocation()
    end)
    local okDoorLoc, doorLoc = pcall(function()
        return doorActor:GetLocation()
    end)
    if not okPlayerLoc or not okDoorLoc or playerLoc == nil or doorLoc == nil then
        return
    end

    local dx = playerLoc.X - doorLoc.X
    local dy = playerLoc.Y - doorLoc.Y
    if dx * dx + dy * dy < 0.0001 then
        return
    end

    local rad = math.rad(yawDelta)
    local cosA = math.cos(rad)
    local sinA = math.sin(rad)
    local newDx = dx * cosA - dy * sinA
    local newDy = dx * sinA + dy * cosA
    local pushX = newDx - dx
    local pushY = newDy - dy

    if pushX * pushX + pushY * pushY < 1.0e-8 then
        return
    end

    pcall(function()
        player:AddWorldOffset(Vec3(pushX, pushY, 0.0))
    end)
    sync_player_physics(player)
end

local function is_single_door(door)
    return door ~= nil and DOUBLE_DOOR_NAMES[door.Name] ~= true
end

local function shuffle_doors(doors)
    for index = #doors, 2, -1 do
        local swapIndex = math.random(index)
        doors[index], doors[swapIndex] = doors[swapIndex], doors[index]
    end
end

local function is_chaos_time_too_close(triggerRemaining, usedTriggerTimes)
    for _, usedRemaining in ipairs(usedTriggerTimes) do
        if math.abs(triggerRemaining - usedRemaining) < CHAOS_DOOR_MIN_TIME_GAP then
            return true
        end
    end
    return false
end

local function pick_chaos_trigger_remaining(usedTriggerTimes, slotIndex, slotCount)
    for _ = 1, 32 do
        local triggerRemaining = CHAOS_DOOR_REMAINING_MIN
            + math.random() * (CHAOS_DOOR_REMAINING_MAX - CHAOS_DOOR_REMAINING_MIN)
        if not is_chaos_time_too_close(triggerRemaining, usedTriggerTimes) then
            return triggerRemaining
        end
    end

    if slotCount <= 0 then
        slotCount = 1
    end
    local alpha = slotIndex / (slotCount + 1)
    return CHAOS_DOOR_REMAINING_MIN
        + alpha * (CHAOS_DOOR_REMAINING_MAX - CHAOS_DOOR_REMAINING_MIN)
end

local function collect_single_door_candidates(self)
    local candidates = {}
    for _, door in ipairs(self.Doors) do
        if is_single_door(door) and door.bPermanentlyLocked ~= true then
            table.insert(candidates, door)
        end
    end
    return candidates
end

local function is_in_auto_close_door_zone(location)
    return location.X < AUTO_CLOSE_DOOR_TRIGGER_X_MAX and location.Y > AUTO_CLOSE_DOOR_TRIGGER_Y_MIN
end

local function is_in_auto_close_y_door_zone(location)
    return location.Y > AUTO_CLOSE_Y_DOOR_TRIGGER_Y_MIN
end

function DoorManager:Reset()
    self.Doors = {}
    self.DoorStateByName = {}
    self.bDoorsInitialized = false
    self.bExitDoorsUnlockedForCurrentLoop = false
    self:ClearChaosDoorSchedule()
    SoundManager:Reset()
end

function DoorManager:ResetSessionState()
    self.bExitGuideDecalRemoved = false
end

local function destroy_world_actor(actor)
    if actor == nil then
        return false
    end

    local okValid, valid = pcall(function()
        return actor.IsValid == nil or actor:IsValid()
    end)
    if not okValid or not valid then
        return false
    end

    if actor.Destroy ~= nil then
        local okDestroy = pcall(function()
            actor:Destroy()
        end)
        return okDestroy == true
    end

    if World ~= nil and World.DestroyActor ~= nil then
        local okDestroy = pcall(function()
            World.DestroyActor(actor)
        end)
        return okDestroy == true
    end

    return false
end

function DoorManager:PermanentlyRemoveExitGuideDecal()
    if self.bExitGuideDecalRemoved then
        return false
    end

    if World == nil or World.FindActorByName == nil then
        return false
    end

    local ok, actor = pcall(function()
        return World.FindActorByName(EXIT_GUIDE_DECAL_ACTOR_NAME)
    end)
    if ok and actor ~= nil then
        destroy_world_actor(actor)
    end

    self.bExitGuideDecalRemoved = true
    return true
end

function DoorManager:ClearChaosDoorSchedule()
    self.PendingChaosDoorEvents = {}
    self.bChaosDoorScheduleActive = false
end

function DoorManager:TryScheduleChaosSingleDoorToggles(scheduleMode)
    local bShouldSchedule = false
    if scheduleMode == "first_timer" then
        bShouldSchedule = true
    elseif scheduleMode == "warp" then
        bShouldSchedule = math.random() < CHAOS_DOOR_WARP_ROLL_CHANCE
    end

    if not bShouldSchedule then
        return false
    end

    self:ClearChaosDoorSchedule()
    self:InitDoors()

    local candidates = collect_single_door_candidates(self)
    if #candidates == 0 then
        return false
    end

    shuffle_doors(candidates)
    local doorCount = math.random(
        CHAOS_DOOR_MIN_COUNT,
        math.min(CHAOS_DOOR_MAX_COUNT, #candidates)
    )

    local usedTriggerTimes = {}
    for index = 1, doorCount do
        local triggerRemaining = pick_chaos_trigger_remaining(usedTriggerTimes, index, doorCount)
        table.insert(usedTriggerTimes, triggerRemaining)
        table.insert(self.PendingChaosDoorEvents, {
            Door = candidates[index],
            TriggerRemaining = triggerRemaining,
            bFired = false,
        })
    end

    self.bChaosDoorScheduleActive = #self.PendingChaosDoorEvents > 0
    return self.bChaosDoorScheduleActive
end

function DoorManager:ToggleDoorForChaos(door)
    if door == nil or door.Actor == nil or door.bPermanentlyLocked then
        return false
    end

    self:SetDoorOpenState(door, not door.IsOpen, true, true)
    return true
end

function DoorManager:UpdateChaosSingleDoorToggles(remainingTime)
    if not self.bChaosDoorScheduleActive then
        return
    end

    remainingTime = tonumber(remainingTime)
    if remainingTime == nil then
        return
    end

    if remainingTime < CHAOS_DOOR_REMAINING_MIN then
        self:ClearChaosDoorSchedule()
        return
    end

    if remainingTime > CHAOS_DOOR_REMAINING_MAX then
        return
    end

    local bAllFired = true
    for _, event in ipairs(self.PendingChaosDoorEvents) do
        if not event.bFired then
            bAllFired = false
            if remainingTime <= event.TriggerRemaining then
                event.bFired = true
                self:ToggleDoorForChaos(event.Door)
            end
        end
    end

    if bAllFired then
        self.bChaosDoorScheduleActive = false
    end
end

function DoorManager:FindDoorByName(name)
    if name == nil or name == "" then
        return nil
    end

    for _, door in ipairs(self.Doors) do
        if door.Name == name then
            return door
        end
    end

    return nil
end

function DoorManager:TryOnLoopStartOnDoorOpen(door, bWasOpen)
    if door == nil or bWasOpen or not door.IsOpen then
        return
    end
    if not is_entry_door(door) then
        return
    end
    if CYMBAL_TRIGGER_DOOR_NAMES[door.Name] == true
        and GameManager ~= nil
        and GameManager.IsCymbalDoorTriggerUsed ~= nil
        and GameManager:IsCymbalDoorTriggerUsed() then
        return
    end
    if GameManager == nil or GameManager.OnLoopStart == nil then
        return
    end

    pcall(function()
        GameManager:OnLoopStart("DoorEntryOpened")
    end)
end

function DoorManager:TryStartCymbalMonkeyCycleOnDoorOpen(door, bWasOpen)
    if door == nil or bWasOpen or not door.IsOpen then
        return
    end
    if CYMBAL_TRIGGER_DOOR_NAMES[door.Name] ~= true then
        return
    end
    if GameManager == nil or GameManager.StartCymbalMonkeyCycle == nil then
        return
    end
    if GameManager.IsCymbalDoorTriggerUsed ~= nil and GameManager:IsCymbalDoorTriggerUsed() then
        return
    end

    pcall(function()
        return GameManager:StartCymbalMonkeyCycle()
    end)
end

function DoorManager:HandleDoorOpened(door, bWasOpen)
    self:TryOnLoopStartOnDoorOpen(door, bWasOpen)
    self:TryStartCymbalMonkeyCycleOnDoorOpen(door, bWasOpen)
end

function DoorManager:SetDoorOpenState(door, bOpen, bPlaySound, bSuppressGameplayHandlers)
    if door == nil or door.Actor == nil or door.IsOpen == bOpen then
        return
    end

    local bWasOpen = door.IsOpen
    door.IsOpen = bOpen
    self.DoorStateByName[door.Name] = bOpen
    door.StartYaw = door.CurrentYaw
    door.TargetYaw = bOpen and door.OpenYaw or door.CloseYaw
    door.Elapsed = 0.0
    door.bPushPlayer = false

    set_door_yaw(door, door.StartYaw)
    sync_door_physics(door.Actor)

    if bOpen and bSuppressGameplayHandlers ~= true then
        self:HandleDoorOpened(door, bWasOpen)
    end

    if not bPlaySound then
        return
    end

    if bOpen then
        SoundManager:PlayDoorOpen(door.Actor, door.OpenSoundKey == HEAVY_DOOR_OPEN_SOUND_KEY)
    elseif bWasOpen then
        SoundManager:QueueDoorClose(door.Actor)
    end
end

function DoorManager:CloseDoorIfOpen(door)
    self:SetDoorOpenState(door, false, true)
end

function DoorManager:ClearToyProjectiles()
    if World == nil or World.FindActorsByTag == nil then
        return
    end

    local ok, found = pcall(function()
        return World.FindActorsByTag(TOY_PROJECTILE_TAG)
    end)
    if not ok or found == nil then
        return
    end

    for _, actor in ipairs(found) do
        if actor ~= nil then
            local okValid, valid = pcall(function()
                return actor.IsValid ~= nil and actor:IsValid()
            end)
            if okValid and valid and actor.Destroy ~= nil then
                pcall(function()
                    actor:Destroy()
                end)
            elseif okValid and valid and World.DestroyActor ~= nil then
                pcall(function()
                    World.DestroyActor(actor)
                end)
            end
        end
    end
end

function DoorManager:RandomizeSingleDoorStatesOnWarp()
    local candidates = {}
    for _, door in ipairs(self.Doors) do
        if is_single_door(door) then
            table.insert(candidates, door)
        end
    end

    shuffle_doors(candidates)
    local maxOpenCount = math.min(MAX_OPEN_SINGLE_DOORS_ON_WARP, #candidates)
    local openCount = math.random(0, maxOpenCount)
    local shouldOpenByName = {}
    for index = 1, openCount do
        shouldOpenByName[candidates[index].Name] = true
    end

    for _, door in ipairs(candidates) do
        self:SetDoorOpenState(door, shouldOpenByName[door.Name] == true, false)
    end
end

function DoorManager:UpdateAutoCloseDoors(location)
    if not is_in_auto_close_door_zone(location) then
        return
    end

    for name, _ in pairs(AUTO_CLOSE_DOOR_NAMES) do
        local door = self:FindDoorByName(name)
        if door ~= nil then
            self:CloseDoorIfOpen(door)
            door.bPermanentlyLocked = true
        end
    end
    self.bExitDoorsUnlockedForCurrentLoop = false
    self:PermanentlyRemoveExitGuideDecal()
end

function DoorManager:LockExitDoorsForCurrentLoop()
    self.bExitDoorsUnlockedForCurrentLoop = false
    for name, _ in pairs(EXIT_DOOR_NAMES) do
        local door = self:FindDoorByName(name)
        if door ~= nil then
            door.bPermanentlyLocked = true
            self:SetDoorOpenState(door, false, false)
        end
    end
end

function DoorManager:OpenExitDoorsForCurrentLoop()
    if self.bExitDoorsUnlockedForCurrentLoop then
        return
    end

    self.bExitDoorsUnlockedForCurrentLoop = true
    for name, _ in pairs(EXIT_DOOR_NAMES) do
        local door = self:FindDoorByName(name)
        if door ~= nil then
            door.bPermanentlyLocked = false
            self:SetDoorOpenState(door, true, true)
        end
    end
end

function DoorManager:UpdateAutoCloseYDoors(location)
    if not is_in_auto_close_y_door_zone(location) then
        return
    end

    for name, _ in pairs(AUTO_CLOSE_Y_DOOR_NAMES) do
        self:CloseDoorIfOpen(self:FindDoorByName(name))
    end
end

function DoorManager:ToggleDoor(door)
    if door == nil or door.Actor == nil or door.bPermanentlyLocked then
        return
    end

    local bWasOpen = door.IsOpen
    door.IsOpen = not door.IsOpen
    self.DoorStateByName[door.Name] = door.IsOpen

    local targetYaw = door.IsOpen and door.OpenYaw or door.CloseYaw
    door.StartYaw = door.CurrentYaw
    door.TargetYaw = targetYaw
    door.Elapsed = 0.0
    door.bPushPlayer = false

    set_door_yaw(door, door.StartYaw)
    sync_door_physics(door.Actor)

    if door.IsOpen then
        self:HandleDoorOpened(door, bWasOpen)
        SoundManager:PlayDoorOpen(door.Actor, door.OpenSoundKey == HEAVY_DOOR_OPEN_SOUND_KEY)
    elseif bWasOpen then
        SoundManager:QueueDoorClose(door.Actor)
    end

end

function DoorManager:AddDoor(actor, openYaw)
    if actor == nil then
        return
    end

    local name = actor_name(actor)
    for _, door in ipairs(self.Doors) do
        if door.Actor == actor or door.Name == name then
            return
        end
    end

    local sceneYaw = get_actor_yaw(actor)
    local isSceneOpen = math.abs(sceneYaw) > 45.0
    local bUseSceneYawAsOpen = isSceneOpen or math.abs(sceneYaw - openYaw) < 20.0
    local isOpen = INITIALLY_OPEN_NAMES[name] == true or bUseSceneYawAsOpen
    local closeYaw = isOpen and 0.0 or sceneYaw
    local resolvedOpenYaw = bUseSceneYawAsOpen and sceneYaw or openYaw
    local currentYaw = isOpen and resolvedOpenYaw or closeYaw
    local openSoundKey = DOUBLE_DOOR_NAMES[name] == true and HEAVY_DOOR_OPEN_SOUND_KEY or DOOR_OPEN_SOUND_KEY

    table.insert(self.Doors, {
        Actor = actor,
        Name = name,
        OpenYaw = resolvedOpenYaw,
        CloseYaw = closeYaw,
        StartYaw = currentYaw,
        TargetYaw = currentYaw,
        CurrentYaw = currentYaw,
        Elapsed = DOOR_OPEN_DURATION,
        IsOpen = isOpen,
        OpenSoundKey = openSoundKey,
        bPermanentlyLocked = false,
        bPushPlayer = false,
    })

    self.DoorStateByName[name] = isOpen
end

function DoorManager:AddDoorsByTag(tag, openYaw)
    if World == nil or World.FindActorsByTag == nil then
        return
    end

    local ok, actors = pcall(function()
        return World.FindActorsByTag(tag)
    end)
    if not ok or actors == nil then
        return
    end

    for _, actor in ipairs(actors) do
        self:AddDoor(actor, openYaw)
    end
end

function DoorManager:AddDoorByName(name, openYaw)
    if World == nil or World.FindActorByName == nil then
        return
    end

    local ok, actor = pcall(function()
        return World.FindActorByName(name)
    end)
    if ok and actor ~= nil then
        self:AddDoor(actor, openYaw)
    end
end

function DoorManager:InitDoors()
    if self.bDoorsInitialized then
        return
    end

    self.Doors = {}
    self.DoorStateByName = {}

    self:AddDoorsByTag("DoorOpenPlus", DOOR_OPEN_ANGLE_PLUS)
    self:AddDoorsByTag("DoorOpenMinus", DOOR_OPEN_ANGLE_MINUS)

    for name, _ in pairs(OPEN_PLUS_NAMES) do
        self:AddDoorByName(name, DOOR_OPEN_ANGLE_PLUS)
    end
    for name, _ in pairs(OPEN_MINUS_NAMES) do
        self:AddDoorByName(name, DOOR_OPEN_ANGLE_MINUS)
    end

    for _, door in ipairs(self.Doors) do
        set_door_yaw(door, door.CurrentYaw)
        sync_door_physics(door.Actor)
    end

    self:LockExitDoorsForCurrentLoop()

    self.bDoorsInitialized = true
end

function DoorManager:UpdateDoors(dt, player)
    local deltaTime = tonumber(dt) or 0.0
    if deltaTime <= 0.0 then
        return
    end

    for _, door in ipairs(self.Doors) do
        if door.Elapsed < DOOR_OPEN_DURATION then
            door.Elapsed = math.min(door.Elapsed + deltaTime, DOOR_OPEN_DURATION)
            local alpha = smooth_step(door.Elapsed / DOOR_OPEN_DURATION)
            local prevYaw = door.CurrentYaw
            local nextYaw = door.StartYaw + (door.TargetYaw - door.StartYaw) * alpha
            if set_door_yaw(door, nextYaw) then
                sync_door_physics(door.Actor)

                local yawDelta = nextYaw - prevYaw
                local touching, contactLocation, contactNormal = get_player_door_contact(player, door.Actor)
                local approaching = touching and is_door_approaching_player(
                    door.Actor, yawDelta, contactLocation, contactNormal
                )

                if touching and approaching then
                    door.bPushPlayer = true
                elseif not approaching then
                    door.bPushPlayer = false
                end
                if door.bPushPlayer and approaching then
                    push_player_from_door_hinge(player, door.Actor, prevYaw, nextYaw)
                end
            end

            if door.Elapsed >= DOOR_OPEN_DURATION then
                door.bPushPlayer = false
            end
        end
    end
end

function DoorManager:Tick(dt, player, location)
    self:UpdateDoors(dt, player)
    SoundManager:TickGameplaySounds(dt)

    if location ~= nil then
        self:UpdateAutoCloseDoors(location)
        self:UpdateAutoCloseYDoors(location)
    end
end

function DoorManager:FindDoorByActor(actor)
    if actor == nil then
        return nil
    end

    for _, door in ipairs(self.Doors) do
        if door.Actor == actor then
            return door
        end
    end

    return nil
end

function DoorManager:FindTargetedDoor(player)
    if World == nil or World.LineTraceObjects == nil or player == nil then
        return nil
    end

    local camera = nil
    local okCamera = pcall(function()
        camera = player:GetCamera()
    end)
    if not okCamera or camera == nil then
        return nil
    end

    local start = camera:GetLocation()
    local direction = camera.Forward
    if start == nil or direction == nil then
        return nil
    end

    local endPos = start + direction * INTERACT_DISTANCE
    local okHit, hit = pcall(function()
        return World.LineTraceObjects(start, endPos, player)
    end)
    if not okHit or hit == nil or hit.Hit ~= true or hit.Actor == nil then
        return nil
    end

    local door = self:FindDoorByActor(hit.Actor)
    if door == nil then
        return nil
    end

    local distance = tonumber(hit.Distance)
    if distance ~= nil and distance > INTERACT_DISTANCE then
        return nil
    end

    return door
end

return DoorManager
