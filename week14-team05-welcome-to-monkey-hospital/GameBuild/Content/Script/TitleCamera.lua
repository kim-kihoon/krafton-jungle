local Stiffness = 85.0
local Damping = 18.0

local MaxRotationZ = 8.0
local MaxLocationZ = 0.1

local BaseLocation = nil
local BaseRotation = nil
local CurrentLocationZ = 0.0
local CurrentZ = 0.0
local VelocityLocationZ = 0.0
local VelocityZ = 0.0

local function clamp(value, minValue, maxValue)
    if value < minValue then
        return minValue
    end
    if value > maxValue then
        return maxValue
    end
    return value
end

local function get_root_component()
    if obj == nil or obj.GetRootComponent == nil then
        return nil
    end
    return obj:GetRootComponent()
end

local function read_mouse_target()
    if Input == nil or Input.GetMouseClientPosition == nil or Input.GetMouseClientSize == nil then
        return 0.0, 0.0
    end

    local mouse = Input.GetMouseClientPosition()
    local size = Input.GetMouseClientSize()
    local width = tonumber(size.Width or size.X or size.x) or 0.0
    local height = tonumber(size.Height or size.Y or size.y) or 0.0
    if width <= 0.0 or height <= 0.0 then
        return 0.0, 0.0
    end

    local mouseX = tonumber(mouse.X or mouse.x) or (width * 0.5)
    local mouseY = tonumber(mouse.Y or mouse.y) or (height * 0.5)
    local normalizedX = clamp((mouseX / width) * 2.0 - 1.0, -1.0, 1.0)
    local normalizedY = clamp((mouseY / height) * 2.0 - 1.0, -1.0, 1.0)

    return normalizedX * MaxRotationZ, normalizedY * MaxLocationZ
end

local function step_spring(current, velocity, target, dt)
    local acceleration = (target - current) * Stiffness - velocity * Damping
    velocity = velocity + acceleration * dt
    current = current + velocity * dt
    return current, velocity
end

function BeginPlay()
    local root = get_root_component()
    if root == nil then
        BaseLocation = nil
        BaseRotation = nil
        return
    end

    BaseLocation = root:GetLocation()
    BaseRotation = root:GetRotation()
    CurrentLocationZ = 0.0
    CurrentZ = 0.0
    VelocityLocationZ = 0.0
    VelocityZ = 0.0
end

function Tick(dt)
    local root = get_root_component()
    if root == nil then
        return
    end

    if BaseLocation == nil then
        BaseLocation = root:GetLocation()
    end

    if BaseRotation == nil then
        BaseRotation = root:GetRotation()
    end

    local deltaTime = clamp(tonumber(dt) or 0.0, 0.0, 0.05)
    local targetZ, targetLocationZ = read_mouse_target()
    targetZ = clamp(targetZ, -MaxRotationZ, MaxRotationZ)
    targetLocationZ = clamp(targetLocationZ, -MaxLocationZ, MaxLocationZ)

    CurrentZ, VelocityZ = step_spring(CurrentZ, VelocityZ, targetZ, deltaTime)
    CurrentLocationZ, VelocityLocationZ = step_spring(CurrentLocationZ, VelocityLocationZ, targetLocationZ, deltaTime)
    CurrentZ = clamp(CurrentZ, -MaxRotationZ, MaxRotationZ)
    CurrentLocationZ = clamp(CurrentLocationZ, -MaxLocationZ, MaxLocationZ)

    root:SetRotation(Vec3(BaseRotation.X, BaseRotation.Y, BaseRotation.Z + CurrentZ))
    root:SetLocation(Vec3(BaseLocation.X, BaseLocation.Y, BaseLocation.Z + CurrentLocationZ))
end
