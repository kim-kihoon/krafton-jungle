--- =========================================================================
--- GameViewportClient의 입력값을 전달 받아 CMC 의 멤버 함수를 호출하는 코드
--- ACharacter.cpp 담당자는 반드시 아래와 같은 함수를 제작해야한다.
--- UFUNCTION(Lua)
--- UCharacterMovementComponent* GetCharacterMovement() const;

--- 아래 4개의 조건이 충족되어야 한다(ACharacter 기준)
--- 1. APawn 계열이고 PIE에서 Possess된다
--- 2. UCharacterMovementComponent가 붙어 있다.
--- 3. 초기화 시 CharacterMovementComponent->SetUpdatedComponent(CapsuleComponent 또는 Root Primitive)를 호출한다.
--- 4. Lua에 GetCharacterMovement()를 UFUNCTION(Lua)로 제공한다.
--- =========================================================================

local FootIK = require("FootIK")

local movement = nil
local footIK = nil

function BeginPlay()
    movement = obj:GetCharacterMovement()
    footIK = FootIK.new(obj)

    if movement == nil then
        print("[CharacterController] CharacterMovementComponent not found: " .. obj.UUID)
        return
    end

    movement:SetMoveInput(0, 0)
    movement:SetLookInput(0, 0)
end

function EndPlay()
    if footIK ~= nil then
        footIK:Disable()
        footIK = nil
    end

    if movement ~= nil then
        movement:SetMoveInput(0, 0)
        movement:SetLookInput(0, 0)
        movement:StopMovementImmediately()
    end
end

function Tick(dt)
    if movement == nil then
        return
    end

    local forward = 0
    if Input.GetKey(Key.W) then forward = forward + 1 end
    if Input.GetKey(Key.S) then forward = forward - 1 end

    local right = 0
    if Input.GetKey(Key.D) then right = right + 1 end
    if Input.GetKey(Key.A) then right = right - 1 end

    -- 중요:
    -- MoveInput은 CMC Tick에서 자동 Clear하지 않는다.
    -- 따라서 키가 안 눌린 프레임에도 반드시 0, 0을 넣어야 한다.
    movement:SetMoveInput(forward, right)

    -- LookInput은 CMC Tick에서 소비 후 Clear된다.
    movement:SetLookInput(Input.GetMouseDeltaX(), Input.GetMouseDeltaY())

    if Input.GetKeyDown(Key.Space) then
        movement:Jump()
    end

    if footIK ~= nil then
        footIK:Tick(dt)
    end
end
