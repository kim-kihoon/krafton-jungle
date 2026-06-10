local PlacementManager = {}

PlacementManager.RecordsById = {}
PlacementManager.NextId = 1

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

local function normalize_tags(options)
    local tags = {}
    if options == nil then
        return tags
    end

    if type(options.Tag) == "string" and options.Tag ~= "" then
        table.insert(tags, options.Tag)
    end

    if type(options.Tags) == "table" then
        for _, tag in ipairs(options.Tags) do
            if type(tag) == "string" and tag ~= "" then
                table.insert(tags, tag)
            end
        end
    end

    return tags
end

local function add_tags(actor, tags)
    if actor == nil or actor.AddTag == nil then
        return
    end

    for _, tag in ipairs(tags) do
        pcall(function()
            actor:AddTag(tag)
        end)
    end
end

local function make_id(self, options)
    if options ~= nil and type(options.Id) == "string" and options.Id ~= "" then
        return options.Id
    end

    local id = "Placement_" .. tostring(self.NextId)
    self.NextId = self.NextId + 1
    return id
end

local function collect_spawned_actors(path, options)
    if World == nil then
        return nil, "World unavailable"
    end

    local location = options and options.Location or nil
    local rotation = options and options.Rotation or nil
    local scale = options and options.Scale or nil

    if World.SpawnActorTemplateActors ~= nil then
        local actors = World.SpawnActorTemplateActors(path, location, rotation, scale)
        if actors ~= nil and #actors > 0 then
            return actors
        end
        return nil, "SpawnActorTemplateActors failed"
    end

    if World.SpawnActorTemplate ~= nil then
        local actor = World.SpawnActorTemplate(path, location, rotation, scale)
        if is_valid_actor(actor) then
            return { actor }
        end
        return nil, "SpawnActorTemplate failed"
    end

    return nil, "ActorTemplate spawn unavailable"
end

local function destroy_actor(actor)
    if not is_valid_actor(actor) or actor.Destroy == nil then
        return false
    end

    local ok = pcall(function()
        actor:Destroy()
    end)
    return ok == true
end

local function contains_actor(record, actor)
    if record == nil or actor == nil or record.Actors == nil then
        return false
    end

    for _, candidate in ipairs(record.Actors) do
        if candidate == actor then
            return true
        end
    end

    return false
end

function PlacementManager:_ResolveRecord(recordOrIdOrActor)
    if recordOrIdOrActor == nil then
        return nil
    end

    if type(recordOrIdOrActor) == "string" then
        return self.RecordsById[recordOrIdOrActor]
    end

    if type(recordOrIdOrActor) == "table" and recordOrIdOrActor.Id ~= nil then
        return self.RecordsById[recordOrIdOrActor.Id]
    end

    for _, record in pairs(self.RecordsById) do
        if contains_actor(record, recordOrIdOrActor) then
            return record
        end
    end

    return nil
end

function PlacementManager:Spawn(path, options)
    if type(path) ~= "string" or path == "" then
        return nil, "invalid path"
    end

    options = options or {}
    local id = make_id(self, options)
    if self.RecordsById[id] ~= nil then
        return nil, "duplicate id"
    end

    local actors, reason = collect_spawned_actors(path, options)
    if actors == nil or #actors <= 0 then
        return nil, reason or "spawn failed"
    end

    local tags = normalize_tags(options)
    for _, actor in ipairs(actors) do
        add_tags(actor, tags)
    end

    local record = {
        Id = id,
        Path = path,
        Primary = actors[1],
        Actors = actors
    }

    self.RecordsById[id] = record
    return record
end

function PlacementManager:Find(id)
    if type(id) ~= "string" then
        return nil
    end
    return self.RecordsById[id]
end

function PlacementManager:IsValid(recordOrActor)
    if recordOrActor == nil then
        return false
    end

    if type(recordOrActor) == "table" and recordOrActor.Actors ~= nil then
        for _, actor in ipairs(recordOrActor.Actors) do
            if is_valid_actor(actor) then
                return true
            end
        end
        return false
    end

    return is_valid_actor(recordOrActor)
end

function PlacementManager:Destroy(recordOrIdOrActor)
    local record = self:_ResolveRecord(recordOrIdOrActor)
    if record == nil then
        return false
    end

    local destroyedAny = false
    for _, actor in ipairs(record.Actors) do
        if destroy_actor(actor) then
            destroyedAny = true
        end
    end

    self.RecordsById[record.Id] = nil
    return destroyedAny
end

function PlacementManager:DestroyAll(filter)
    local destroyedCount = 0
    local ids = {}

    for id, record in pairs(self.RecordsById) do
        local shouldDestroy = true
        if type(filter) == "function" then
            local ok, result = pcall(filter, record)
            shouldDestroy = ok and result == true
        end

        if shouldDestroy then
            table.insert(ids, id)
        end
    end

    for _, id in ipairs(ids) do
        if self:Destroy(id) then
            destroyedCount = destroyedCount + 1
        end
    end

    return destroyedCount
end

function PlacementManager:PruneInvalid()
    local removedCount = 0
    for id, record in pairs(self.RecordsById) do
        if not self:IsValid(record) then
            self.RecordsById[id] = nil
            removedCount = removedCount + 1
        end
    end
    return removedCount
end

return PlacementManager
