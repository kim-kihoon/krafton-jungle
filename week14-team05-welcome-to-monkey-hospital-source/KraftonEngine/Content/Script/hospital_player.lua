-- Hospital.Scene 플레이어 흐름과 씬 상호작용을 연결한다.

local GameManager = require("GameManager")
local DoorManager = require("DoorManager")
local SoundManager = require("SoundManager")
local UIManager = require("UIManager")
local ToolManager = require("ToolManager")
local SettingManager = require("SettingManager")
local GameOverMonkey = require("GameOverMonkey")
local StageManager = require("StageManager")
local EndingManager = require("EndingManager")
local StartupManager = require("StartupManager")

local TRIGGER_Y_MIN = 27.132
local TRIGGER_X_MAX = -3.0
local WARP_DELTA_X = 8.368179
local WARP_DELTA_Y = -33.80393
local WARP_DELTA_Z = 0.0

local bCanWarp = true
local bLastLoopStopped = false
local bTitleMode = true

local KEY_W = 0x57
local KEY_A = 0x41
local KEY_S = 0x53
local KEY_D = 0x44
local KEY_F10 = 0x79
local TITLE_CAMERA_TAG = "TitleCamera"
local TITLE_ACTOR_TAG = "Title"
local TITLE_MONKEY_ACTOR_NAME = "TitleMonkey"
local TITLE_MONKEY_READY_FUNCTION = "Ready"
local TITLE_MONKEY_STRIKE_FUNCTION = "Strike"
local TITLE_FADE_OUT_SECONDS = 0.75
local TITLE_BLACK_HOLD_SECONDS = 0.1
local TITLE_FADE_IN_SECONDS = 0.75

local bTitleTransitioning = false
local bPauseMenuActive = false
local TitleTransitionCoroutine = nil
local TitleTransitionWaitRemaining = 0.0
local GameOverStateChangedHandle = nil
local InitialPlayerLocation = nil
local InitialPlayerRotation = nil
local InitialPlayerControlRotation = nil

local function IsInTriggerZone(location)
    return location.Y > TRIGGER_Y_MIN and location.X < TRIGGER_X_MAX
end

local function SyncCrosshairVisibility()
    if Crosshair == nil or Crosshair.set_visible == nil then
        return
    end

    if bTitleMode or bPauseMenuActive then
        Crosshair.set_visible(false)
        return
    end

    if EndingManager:IsActive() then
        Crosshair.set_visible(false)
        return
    end

    if ToolManager:IsPistol() then
        Crosshair.set_visible(true)
    else
        Crosshair.set_visible(false)
    end
end

local function CopyVec3(value)
    if value == nil then
        return nil
    end

    return Vec3(value.X or 0.0, value.Y or 0.0, value.Z or 0.0)
end

local function GetActorRotation(actor)
    if actor == nil then
        return nil
    end

    local ok, rotation = pcall(function()
        return actor.Rotation
    end)
    if ok then
        return rotation
    end
    return nil
end

local function GetPlayerPawn()
    if obj == nil or obj.AsPawn == nil then
        return nil
    end

    local ok, pawn = pcall(function()
        return obj:AsPawn()
    end)
    if ok then
        return pawn
    end
    return nil
end

local function GetPawnControlRotation(pawn)
    if pawn == nil or pawn.GetControlRotation == nil then
        return nil
    end

    local ok, rotation = pcall(function()
        return pawn:GetControlRotation()
    end)
    if ok then
        return rotation
    end
    return nil
end

local function CaptureInitialPlayerTransform()
    if obj == nil then
        return false
    end

    local pawn = GetPlayerPawn()
    InitialPlayerLocation = CopyVec3(obj:GetLocation())
    InitialPlayerRotation = CopyVec3(GetActorRotation(obj))
    InitialPlayerControlRotation = CopyVec3(GetPawnControlRotation(pawn))
    return InitialPlayerLocation ~= nil
        and InitialPlayerRotation ~= nil
        and InitialPlayerControlRotation ~= nil
end

local function RestoreInitialPlayerTransform()
    if obj == nil then
        return false
    end

    local pawn = GetPlayerPawn()
    local bRestored = false
    if InitialPlayerLocation ~= nil and obj.SetLocation ~= nil then
        local ok = pcall(function()
            obj:SetLocation(InitialPlayerLocation)
        end)
        bRestored = bRestored or ok
    end

    if InitialPlayerRotation ~= nil and obj.SetRotation ~= nil then
        local ok = pcall(function()
            obj:SetRotation(InitialPlayerRotation)
        end)
        bRestored = bRestored or ok
    end

    if InitialPlayerControlRotation ~= nil
        and pawn ~= nil
        and pawn.SetControlRotation ~= nil then
        local ok = pcall(function()
            pawn:SetControlRotation(InitialPlayerControlRotation)
        end)
        bRestored = bRestored or ok
    end

    return bRestored
end

local function IsKeyDown(key)
    if Input == nil or Input.GetKey == nil then
        return false
    end

    local ok, down = pcall(function()
        return Input.GetKey(key)
    end)
    return ok and down == true
end

local function GetInputAxis(name)
    if Input == nil or Input.GetAxis == nil then
        return 0.0
    end

    local ok, value = pcall(function()
        return Input.GetAxis(name)
    end)
    if ok and value ~= nil then
        return value
    end
    return 0.0
end

local function GetActionDown(name)
    if Input == nil or Input.GetActionDown == nil then
        return false
    end

    local ok, pressed = pcall(function()
        return Input.GetActionDown(name)
    end)
    return ok and pressed == true
end

local function IsLoopStopped()
    return GameManager ~= nil
        and GameManager.IsLoopStopped ~= nil
        and GameManager:IsLoopStopped()
end

local function AddPlayerMovement()
    if obj == nil then
        return
    end

    local forwardInput = GetInputAxis("MoveForward")
    local rightInput = GetInputAxis("MoveRight")
    if forwardInput == 0.0 and rightInput == 0.0 then
        if IsKeyDown(KEY_W) then forwardInput = forwardInput + 1.0 end
        if IsKeyDown(KEY_S) then forwardInput = forwardInput - 1.0 end
        if IsKeyDown(KEY_D) then rightInput = rightInput + 1.0 end
        if IsKeyDown(KEY_A) then rightInput = rightInput - 1.0 end
    end

    if forwardInput == 0.0 and rightInput == 0.0 then
        return
    end

    local forward = Vec3(1.0, 0.0, 0.0)
    local right = Vec3(0.0, 1.0, 0.0)

    local okForward, actorForward = pcall(function()
        return obj.Forward
    end)
    if okForward and actorForward ~= nil then
        forward = Vec3(actorForward.X, actorForward.Y, 0.0)
    end

    local okRight, actorRight = pcall(function()
        return obj.Right
    end)
    if okRight and actorRight ~= nil then
        right = Vec3(actorRight.X, actorRight.Y, 0.0)
    end

    if forwardInput ~= 0.0 then
        pcall(function()
            obj:AddMovementInput(forward, forwardInput)
        end)
    end
    if rightInput ~= 0.0 then
        pcall(function()
            obj:AddMovementInput(right, rightInput)
        end)
    end
end

local function GetActorCamera(actor)
    if actor == nil then
        return nil
    end

    local ok, camera = pcall(function()
        return actor:GetCamera()
    end)
    if ok then
        return camera
    end
    return nil
end

local function FindActorByName(name)
    if World == nil or World.FindActorByName == nil then
        return nil
    end

    local ok, actor = pcall(function()
        return World.FindActorByName(name)
    end)
    if ok then
        return actor
    end
    return nil
end

local function FindFirstActorByTag(tag)
    if World == nil or World.FindFirstActorByTag == nil then
        return nil
    end

    local ok, actor = pcall(function()
        return World.FindFirstActorByTag(tag)
    end)
    if ok then
        return actor
    end
    return nil
end

local function SetActiveCameraImmediate(camera)
    if camera == nil or CameraManager == nil then
        return false
    end

    if CameraManager.PossessCamera ~= nil then
        local ok, result = pcall(function()
            return CameraManager.PossessCamera(camera)
        end)
        if ok then
            return result ~= false
        end
    end

    if CameraManager.SetActiveCamera ~= nil then
        local ok = pcall(function()
            CameraManager.SetActiveCamera(camera)
        end)
        return ok
    end

    return false
end

local function PlayCameraFadeOut(duration)
    if CameraManager == nil or CameraManager.FadeOut == nil then
        return false
    end

    local ok = pcall(function()
        CameraManager.FadeOut(duration)
    end)
    return ok == true
end

local function PlayCameraFadeIn(duration)
    if CameraManager == nil or CameraManager.FadeIn == nil then
        return false
    end

    local ok = pcall(function()
        CameraManager.FadeIn(duration)
    end)
    return ok == true
end

local function CaptureTitleCamera()
    local titleCameraActor = FindFirstActorByTag(TITLE_CAMERA_TAG) or FindActorByName("Camera")
    return SetActiveCameraImmediate(GetActorCamera(titleCameraActor))
end

local function CapturePlayerCamera()
    local camera = GetActorCamera(obj)
    if camera == nil then
        camera = GetActorCamera(FindActorByName("Player"))
    end
    return SetActiveCameraImmediate(camera)
end

local function DeactivateComponent(component)
    if component == nil then
        return
    end

    pcall(function()
        component:SetVisibility(false)
    end)
    pcall(function()
        component:SetVisible(false)
    end)
    pcall(function()
        component:SetActive(false)
    end)
    pcall(function()
        component:Deactivate()
    end)
end

local function ActivateComponent(component)
    if component == nil then
        return
    end

    pcall(function()
        component:SetVisibility(true)
    end)
    pcall(function()
        component:SetVisible(true)
    end)
    pcall(function()
        component:SetActive(true)
    end)
    pcall(function()
        component:Activate()
    end)
end

local function ActivateTitleActor(actor)
    if actor == nil then
        return
    end

    pcall(function()
        actor:SetVisible(true)
    end)

    local okComponents, components = pcall(function()
        return actor:GetComponents()
    end)
    if okComponents and components ~= nil then
        for _, component in ipairs(components) do
            ActivateComponent(component)
        end
        return
    end

    local okRoot, root = pcall(function()
        return actor:GetRootPrimitiveComponent()
    end)
    if okRoot then
        ActivateComponent(root)
    end
end

local function ActivateTitleActors()
    if World == nil or World.FindActorsByTag == nil then
        return
    end

    local ok, actors = pcall(function()
        return World.FindActorsByTag(TITLE_ACTOR_TAG)
    end)
    if not ok or actors == nil then
        return
    end

    for _, actor in ipairs(actors) do
        ActivateTitleActor(actor)
    end
end

local function DeactivateTitleActor(actor)
    if actor == nil then
        return
    end

    pcall(function()
        actor:SetVisible(false)
    end)

    local okComponents, components = pcall(function()
        return actor:GetComponents()
    end)
    if okComponents and components ~= nil then
        for _, component in ipairs(components) do
            DeactivateComponent(component)
        end
        return
    end

    local okRoot, root = pcall(function()
        return actor:GetRootPrimitiveComponent()
    end)
    if okRoot then
        DeactivateComponent(root)
    end
end

local function DeactivateTitleActors()
    if World == nil or World.FindActorsByTag == nil then
        return
    end

    local ok, actors = pcall(function()
        return World.FindActorsByTag(TITLE_ACTOR_TAG)
    end)
    if not ok or actors == nil then
        return
    end

    for _, actor in ipairs(actors) do
        DeactivateTitleActor(actor)
    end
end

local function CallTitleMonkeyFunction(functionName)
    local titleMonkey = FindActorByName(TITLE_MONKEY_ACTOR_NAME)
    if titleMonkey == nil or titleMonkey.GetLuaScriptComponent == nil then
        return false
    end

    local luaScript = titleMonkey:GetLuaScriptComponent()
    if luaScript == nil or luaScript.CallFunction == nil then
        return false
    end

    local ok, result = pcall(function()
        return luaScript:CallFunction(functionName)
    end)
    return ok and result ~= false
end

local function PlayTitleMonkeyReadyAnimation()
    return CallTitleMonkeyFunction(TITLE_MONKEY_READY_FUNCTION)
end

local function PlayTitleMonkeyStrikeAnimation()
    return CallTitleMonkeyFunction(TITLE_MONKEY_STRIKE_FUNCTION)
end

local function StopTitleTransitionCoroutine()
    TitleTransitionCoroutine = nil
    TitleTransitionWaitRemaining = 0.0
    bTitleTransitioning = false
end

local function ClearGameOverPresentation()
    GameOverMonkey:ClearPresentation()
    UIManager:DisposeGameOver()
end

local function OpenGameOverMenu()
    UIManager:ShowGameOver()
end

local function StartGameOverPresentation()
    UIManager:DisposeGameOver()
    local bPresentationStarted = GameOverMonkey:StartPresentation(OpenGameOverMenu)
    if not bPresentationStarted then
        OpenGameOverMenu()
    end
    return bPresentationStarted
end

local function CanOpenInGamePauseMenu()
    if bPauseMenuActive or bTitleMode or bTitleTransitioning then
        return false
    end
    if StartupManager:IsActive() then
        return false
    end
    if GameManager == nil or GameManager.GetState == nil or GameManager.State == nil then
        return false
    end
    if GameManager:GetState() ~= GameManager.State.Playing then
        return false
    end
    if EndingManager ~= nil and EndingManager.IsActive ~= nil and EndingManager:IsActive() then
        return false
    end
    return true
end

local function CloseInGamePauseMenu()
    if not bPauseMenuActive then
        return
    end

    bPauseMenuActive = false
    UIManager:HidePauseMenu()

    if Engine ~= nil and Engine.ResumeGame ~= nil then
        pcall(function()
            Engine.ResumeGame()
        end)
    end

    if GameManager ~= nil
        and GameManager.GetState ~= nil
        and GameManager.State ~= nil
        and GameManager:GetState() == GameManager.State.Paused
        and GameManager.ResumeGame ~= nil then
        pcall(function()
            GameManager:ResumeGame()
        end)
    end
end

local function OpenInGamePauseMenu()
    if not CanOpenInGamePauseMenu() then
        return false
    end

    bPauseMenuActive = true

    if GameManager ~= nil and GameManager.PauseGame ~= nil then
        pcall(function()
            GameManager:PauseGame()
        end)
    end

    if Engine ~= nil and Engine.PauseGame ~= nil then
        pcall(function()
            Engine.PauseGame()
        end)
    end

    UIManager:ShowPauseMenu()
    SyncCrosshairVisibility()
    return true
end

local function BindEscapeMenuHandler()
    if Engine == nil or Engine.SetOnEscape == nil then
        return
    end

    Engine.SetOnEscape(function()
        if bPauseMenuActive then
            CloseInGamePauseMenu()
        else
            OpenInGamePauseMenu()
        end
    end)
end

local function UnbindEscapeMenuHandler()
    if Engine == nil or Engine.SetOnEscape == nil then
        return
    end

    Engine.SetOnEscape(function()
    end)
end

local function WasPauseMenuKeyPressed()
    if Input == nil then
        return false
    end

    if Input.GetRawKeyDown ~= nil then
        local ok, pressed = pcall(function()
            return Input.GetRawKeyDown(KEY_F10)
        end)
        if ok and pressed == true then
            return true
        end
    end

    if Input.GetKeyDown ~= nil then
        local ok, pressed = pcall(function()
            return Input.GetKeyDown("F10")
        end)
        if ok and pressed == true then
            return true
        end
    end

    return false
end

local function HandleGameStateChanged(nextState)
    if GameManager ~= nil
        and GameManager.State ~= nil
        and nextState == GameManager.State.GameOver then
        StartGameOverPresentation()
        return
    end

    ClearGameOverPresentation()
end

local function BindGameOverStateChanged()
    if GameOverStateChangedHandle ~= nil
        or GameManager == nil
        or GameManager.OnStateChanged == nil then
        return
    end

    GameOverStateChangedHandle = GameManager:OnStateChanged(function(nextState)
        HandleGameStateChanged(nextState)
    end)
end

local function UnbindGameOverStateChanged()
    if GameOverStateChangedHandle == nil then
        return
    end

    if GameManager ~= nil and GameManager.RemoveListener ~= nil then
        GameManager:RemoveListener("StateChanged", GameOverStateChangedHandle)
    end
    GameOverStateChangedHandle = nil
end

local function BeginHospitalGameplaySession(options)
    options = options or {}

    CloseInGamePauseMenu()
    bCanWarp = true
    bTitleMode = false
    if HospitalPlayer ~= nil then
        HospitalPlayer.title_mode = false
    end

    EndingManager:Reset()
    DoorManager:Reset()
    if options.ResetSessionState == true then
        DoorManager:ResetSessionState()
    end
    DoorManager:ClearToyProjectiles()
    SoundManager:EnterPlayingState()
    ToolManager:Reset()
    UIManager:ResetHospital()
    CapturePlayerCamera()

    if options.RestoreTransform == true then
        RestoreInitialPlayerTransform()
    else
        CaptureInitialPlayerTransform()
    end

    DeactivateTitleActors()
    GameManager:RestartGame()
    bLastLoopStopped = IsLoopStopped()
end

local function ApplyGameplayStart()
    BeginHospitalGameplaySession({
        RestoreTransform = false,
        ResetSessionState = true,
    })
end

local function WaitTitleTransition(seconds)
    coroutine.yield(tonumber(seconds) or 0.0)
end

local function ResumeTitleTransition(dt)
    if TitleTransitionCoroutine == nil then
        return
    end

    if coroutine.status(TitleTransitionCoroutine) == "dead" then
        StopTitleTransitionCoroutine()
        return
    end

    TitleTransitionWaitRemaining = TitleTransitionWaitRemaining - (tonumber(dt) or 0.0)
    if TitleTransitionWaitRemaining > 0.0 then
        return
    end

    local ok, waitSeconds = coroutine.resume(TitleTransitionCoroutine)
    if not ok then
        StopTitleTransitionCoroutine()
        return
    end

    if coroutine.status(TitleTransitionCoroutine) == "dead" then
        StopTitleTransitionCoroutine()
        return
    end

    TitleTransitionWaitRemaining = math.max(0.0, tonumber(waitSeconds) or 0.0)
end

local function StartTitleTransitionCoroutine()
    TitleTransitionWaitRemaining = 0.0
    TitleTransitionCoroutine = coroutine.create(function()
        PlayCameraFadeOut(TITLE_FADE_OUT_SECONDS)
        WaitTitleTransition(TITLE_FADE_OUT_SECONDS)
        WaitTitleTransition(TITLE_BLACK_HOLD_SECONDS)

        ApplyGameplayStart()

        PlayCameraFadeIn(TITLE_FADE_IN_SECONDS)
        WaitTitleTransition(TITLE_FADE_IN_SECONDS)
    end)

    ResumeTitleTransition(0.0)
end

local function PlayTitleMusicNow()
    local playFn = SoundManager.PlayTitleMusicIfNeeded or SoundManager.PlayTitleMusic
    playFn(SoundManager)
end

local function ScheduleTitleMusic()
    if StartCoroutine == nil or Wait == nil then
        PlayTitleMusicNow()
        return
    end

    StartCoroutine(function()
        Wait(0.0)
        if bTitleMode and not bTitleTransitioning then
            PlayTitleMusicNow()
        end
    end)
end

local function EnterTitleScreen()
    UIManager:ShowTitle()
    CaptureTitleCamera()
    ScheduleTitleMusic()
    PlayTitleMonkeyReadyAnimation()
end

function BeginPlay()
    StopTitleTransitionCoroutine()
    StartupManager:Cancel()
    bPauseMenuActive = false
    InitialPlayerLocation = nil
    InitialPlayerRotation = nil
    InitialPlayerControlRotation = nil
    bCanWarp = true
    bLastLoopStopped = IsLoopStopped()
    bTitleMode = true
    GameOverMonkey:Initialize(obj)
    EndingManager:Initialize()
    BindGameOverStateChanged()
    BindEscapeMenuHandler()
    DoorManager:Reset()
    DoorManager:ResetSessionState()
    SoundManager:EnterTitleState()
    PlayTitleMusicNow()
    ToolManager:Reset()
    SettingManager:ApplyAll(obj)
    UIManager:ResetHospital()
    if HospitalPlayer ~= nil then
        HospitalPlayer.title_mode = true
    end
    CaptureTitleCamera()
    StartupManager:Begin(function()
        EnterTitleScreen()
        CaptureTitleCamera()
    end)
end

function EndPlay()
    StopTitleTransitionCoroutine()
    StartupManager:Cancel()
    CloseInGamePauseMenu()
    UnbindGameOverStateChanged()
    UnbindEscapeMenuHandler()
    GameOverMonkey:Shutdown()
    EndingManager:Reset()
    SoundManager:StopTitleMusic()
    bCanWarp = true
    bLastLoopStopped = false
    DoorManager:Reset()
    DoorManager:ResetSessionState()
    ToolManager:Reset()
    UIManager:ResetHospital()
    bTitleMode = true
end

function Tick(dt)
    if obj == nil then
        return
    end

    GameOverMonkey:Tick(dt)

    if StartupManager:IsActive() then
        CaptureTitleCamera()
        SyncCrosshairVisibility()
        return
    end

    if bTitleTransitioning then
        ResumeTitleTransition(dt)
        if bTitleMode then
            CaptureTitleCamera()
        end
    elseif bTitleMode then
        CaptureTitleCamera()
    end

    if bTitleTransitioning or bTitleMode then
        SyncCrosshairVisibility()
        return
    end

    if bPauseMenuActive then
        if WasPauseMenuKeyPressed() then
            CloseInGamePauseMenu()
        end
        SyncCrosshairVisibility()
        return
    end

    if GameManager ~= nil
        and GameManager.GetState ~= nil
        and GameManager:GetState() == GameManager.State.GameOver then
        SyncCrosshairVisibility()
        return
    end

    if EndingManager.ShouldProcessEndingTick ~= nil and EndingManager:ShouldProcessEndingTick() then
        EndingManager:Tick(dt)
        SyncCrosshairVisibility()
        return
    end

    if WasPauseMenuKeyPressed() and OpenInGamePauseMenu() then
        return
    end

    DoorManager:InitDoors()

    local location = obj:GetLocation()
    local bInZone = IsInTriggerZone(location)

    AddPlayerMovement()
    DoorManager:Tick(dt, obj, location)

    local bLoopStopped = IsLoopStopped()
    if bLoopStopped and not bLastLoopStopped then
        DoorManager:OpenExitDoorsForCurrentLoop()
    end
    bLastLoopStopped = bLoopStopped

    if bInZone and bCanWarp and StageManager:CanZoneWarp() then
        obj:AddWorldOffset(Vec3(WARP_DELTA_X, WARP_DELTA_Y, WARP_DELTA_Z))
        GameManager:OnWarp("PlayerWarp")
        bLastLoopStopped = IsLoopStopped()
        DoorManager:LockExitDoorsForCurrentLoop()
        DoorManager:RandomizeSingleDoorStatesOnWarp()
        DoorManager:ClearToyProjectiles()
        bCanWarp = false
    elseif not bInZone then
        bCanWarp = true
    end

    UIManager:UpdateControlPrompt()
    UIManager:UpdateAmmoPrompt(GameManager)
    UIManager:UpdateTimerPrompt(GameManager)

    local targetedDoor = DoorManager:FindTargetedDoor(obj)
    UIManager:UpdateDoorPrompt(targetedDoor)

    local bInteractPressed = GetActionDown("Interact")
    if bInteractPressed and targetedDoor ~= nil then
        DoorManager:ToggleDoor(targetedDoor)
        UIManager:UpdateDoorPrompt(targetedDoor)
    end

    SyncCrosshairVisibility()
end

function OnOverlap(OtherActor)
end

function StartGame()
    if not bTitleMode or bTitleTransitioning then
        return
    end

    bTitleTransitioning = true
    SoundManager:StopTitleMusic()
    UIManager:CloseTitlePopup()
    PlayTitleMonkeyStrikeAnimation()
    StartTitleTransitionCoroutine()
end

function ResumeGame()
    CloseInGamePauseMenu()
    SyncCrosshairVisibility()
end

function RestartGame()
    CloseInGamePauseMenu()
    StopTitleTransitionCoroutine()
    ClearGameOverPresentation()
    BeginHospitalGameplaySession({
        RestoreTransform = true,
        ResetSessionState = false,
    })
end

function ExitToTitle()
    CloseInGamePauseMenu()
    StopTitleTransitionCoroutine()
    ClearGameOverPresentation()
    GameManager:Reset()
    RestoreInitialPlayerTransform()
    bCanWarp = true
    bTitleMode = true
    if HospitalPlayer ~= nil then
        HospitalPlayer.title_mode = true
    end
    bLastLoopStopped = IsLoopStopped()
    EndingManager:Reset()
    DoorManager:Reset()
    DoorManager:ResetSessionState()
    DoorManager:ClearToyProjectiles()
    SoundManager:EnterTitleState()
    ToolManager:Reset()
    ActivateTitleActors()
    UIManager:ResetHospital()
    EnterTitleScreen()
    SyncCrosshairVisibility()
end

local function RestoreTitleViewFromEnding()
    if CameraManager ~= nil then
        if CameraManager.StopCameraFade ~= nil then
            pcall(function()
                CameraManager.StopCameraFade()
            end)
        elseif CameraManager.FadeIn ~= nil then
            pcall(function()
                CameraManager.FadeIn(0.5)
            end)
        end
    end

    UIManager:ExitCutsceneMode()
end

function ExitToTitleFromEnding()
    StopTitleTransitionCoroutine()
    ClearGameOverPresentation()
    RestoreTitleViewFromEnding()
    GameManager:Reset()
    RestoreInitialPlayerTransform()
    bCanWarp = true
    bTitleMode = true
    if HospitalPlayer ~= nil then
        HospitalPlayer.title_mode = true
    end
    bLastLoopStopped = IsLoopStopped()
    DoorManager:Reset()
    DoorManager:ResetSessionState()
    DoorManager:ClearToyProjectiles()
    SoundManager:EnterTitleState()
    ToolManager:Reset()
    ActivateTitleActors()
    UIManager:ResetHospital()
    EnterTitleScreen()
    SyncCrosshairVisibility()
end

function SubmitEndingPlayerName()
    EndingManager:SubmitPlayerName()
end

EndingManager:RegisterReturnToTitleCallback(ExitToTitleFromEnding)

function ShowSetting()
    if not bTitleMode or bTitleTransitioning then
        return false
    end
    PlayTitleMonkeyStrikeAnimation()
    UIManager:ShowTitleSetting()
end

function CycleSettingGamma()
    SettingManager:CycleGamma()
    UIManager:RefreshTitleSetting()
end

function CycleSettingMasterVolume()
    SettingManager:CycleMasterVolume()
    UIManager:RefreshTitleSetting()
end

function CycleSettingMouseSensitivity()
    SettingManager:CycleMouseSensitivity(obj)
    UIManager:RefreshTitleSetting()
end

function ToggleSettingInvertY()
    SettingManager:ToggleInvertY(obj)
    UIManager:RefreshTitleSetting()
end

function ToggleSettingHeadBob()
    SettingManager:ToggleHeadBob()
    UIManager:RefreshTitleSetting()
end

function ToggleSettingControlPrompt()
    SettingManager:ToggleControlPrompt()
    UIManager:RefreshTitleSetting()
end

function CycleSettingDisplayMode()
    SettingManager:CycleDisplayMode()
    UIManager:RefreshTitleSetting()
end

function ShowRanking()
    if not bTitleMode or bTitleTransitioning then
        return false
    end
    PlayTitleMonkeyStrikeAnimation()
    UIManager:ShowTitleLeaderboard()
end

function ShowCredit()
    if not bTitleMode or bTitleTransitioning then
        return false
    end
    PlayTitleMonkeyStrikeAnimation()
    UIManager:ShowTitleCredit()
end

function ClosePopup()
    UIManager:CloseTitlePopup()
    if bTitleMode and not bTitleTransitioning then
        EnterTitleScreen()
    end
end

function ExitGame()
    if bTitleMode and not bTitleTransitioning then
        PlayTitleMonkeyStrikeAnimation()
    end

    if Engine ~= nil and Engine.Exit ~= nil then
        Engine.Exit()
    end
end
