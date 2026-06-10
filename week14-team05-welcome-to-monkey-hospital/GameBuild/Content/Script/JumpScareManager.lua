local JumpScareManager = {}

JumpScareManager.Tags = {
    Candidate = "JumpScareCandidate",
    Active = "JumpScareActive",
    Triggered = "JumpScareTriggered"
}

JumpScareManager.ActiveCount = 3
JumpScareManager.LastError = nil

local bRandomSeeded = false

local function make_seed(timeSeconds)
    local rawSeed = math.floor((tonumber(timeSeconds) or 0) * 1000000)
    if rawSeed <= 0 then
        return nil
    end
    return (rawSeed % 2147483646) + 1
end

local function seed_random_once()
    if bRandomSeeded then
        return
    end

    local seed = nil
    if World ~= nil and World.GetRealTimeSeconds ~= nil then
        seed = make_seed(World.GetRealTimeSeconds())
    end

    if seed ~= nil then
        math.randomseed(seed)
        bRandomSeeded = true
    end
end

local function is_valid_actor(actor)
    if actor == nil then
        return false
    end
    if actor.IsValid == nil then
        return true
    end

    local ok, valid = pcall(function()
        return actor:IsValid()
    end)
    return ok and valid == true
end

local function remove_tag(actor, tag)
    if is_valid_actor(actor) and actor.RemoveTag ~= nil then
        pcall(function()
            actor:RemoveTag(tag)
        end)
    end
end

local function add_tag(actor, tag)
    if is_valid_actor(actor) and actor.AddTag ~= nil then
        pcall(function()
            actor:AddTag(tag)
        end)
    end
end

local function get_skeletal_mesh(actor)
    if not is_valid_actor(actor) or actor.GetSkeletalMeshComponent == nil then
        return nil
    end

    local ok, mesh = pcall(function()
        return actor:GetSkeletalMeshComponent()
    end)
    if not ok then
        return nil
    end
    return mesh
end

local function reset_candidate_presentation(actor)
    local mesh = get_skeletal_mesh(actor)
    if mesh == nil then
        return
    end

    if mesh.StopAnimation ~= nil then
        pcall(function()
            mesh:StopAnimation()
        end)
    end

    if mesh.SetVisibility ~= nil then
        pcall(function()
            mesh:SetVisibility(false)
        end)
    end
end

local function shuffle_in_place(items)
    for index = #items, 2, -1 do
        local swapIndex = math.random(1, index)
        items[index], items[swapIndex] = items[swapIndex], items[index]
    end
end

function JumpScareManager:_GetCandidates()
    local candidates = {}
    if World == nil or World.FindActorsByTag == nil then
        self.LastError = "World.FindActorsByTag unavailable"
        return candidates
    end

    local found = World.FindActorsByTag(self.Tags.Candidate)
    if found == nil then
        return candidates
    end

    for _, actor in pairs(found) do
        if is_valid_actor(actor) then
            table.insert(candidates, actor)
        end
    end

    return candidates
end

function JumpScareManager:DeactivateAll()
    local candidates = self:_GetCandidates()
    for _, actor in ipairs(candidates) do
        remove_tag(actor, self.Tags.Active)
        remove_tag(actor, self.Tags.Triggered)
        reset_candidate_presentation(actor)
    end
    return #candidates
end

function JumpScareManager:ActivateRandom(count)
    seed_random_once()
    self.LastError = nil

    local candidates = self:_GetCandidates()
    if #candidates <= 0 then
        self.LastError = "JumpScareCandidate tag actor not found"
        return false
    end

    for _, actor in ipairs(candidates) do
        remove_tag(actor, self.Tags.Active)
        remove_tag(actor, self.Tags.Triggered)
        reset_candidate_presentation(actor)
    end

    local activateCount = tonumber(count) or self.ActiveCount
    activateCount = math.floor(activateCount)
    if activateCount < 0 then
        activateCount = 0
    end
    if activateCount > #candidates then
        activateCount = #candidates
    end

    shuffle_in_place(candidates)
    for index = 1, activateCount do
        add_tag(candidates[index], self.Tags.Active)
    end

    return true
end

function JumpScareManager:GetLastError()
    return self.LastError
end

function JumpScareManager:SetActiveCount(count)
    count = tonumber(count)
    if count == nil or count < 0 then
        self.LastError = "invalid active count"
        return false
    end

    self.ActiveCount = math.floor(count)
    self.LastError = nil
    return true
end

function JumpScareManager:GetActiveCount()
    return self.ActiveCount
end

return JumpScareManager
