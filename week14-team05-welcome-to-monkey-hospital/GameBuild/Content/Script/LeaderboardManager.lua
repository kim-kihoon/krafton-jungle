local LeaderboardManager = {}

LeaderboardManager.MaxEntries = 20
LeaderboardManager.Entries = {}
LeaderboardManager.LastRecord = nil
LeaderboardManager.NextRecordId = 1
LeaderboardManager.StoragePath = "Leaderboard/leaderboard.tsv"
LeaderboardManager.bLoadedFromDisk = false

local SERIALIZE_HEADER = "MonkeyHospitalLeaderboard"
local SERIALIZE_VERSION = "1"
local FIELD_ORDER = {
    "Rank",
    "RecordId",
    "TotalTimeSeconds",
    "ElapsedTimeSeconds",
    "PlayerName",
    "Score",
    "ClearReason",
    "CreatedAtSeconds"
}

local function to_number_or_zero(value)
    local number = tonumber(value)
    if number == nil or number < 0 then
        return 0
    end
    return number
end

local function to_positive_integer(value, fallback)
    local number = math.floor(tonumber(value) or fallback or 0)
    if number < 1 then
        return fallback or 1
    end
    return number
end

local function copy_entry(entry)
    if entry == nil then
        return nil
    end

    return {
        Rank = entry.Rank,
        RecordId = entry.RecordId,
        TotalTimeSeconds = entry.TotalTimeSeconds,
        ElapsedTimeSeconds = entry.ElapsedTimeSeconds,
        PlayerName = entry.PlayerName,
        Score = entry.Score,
        ClearReason = entry.ClearReason,
        CreatedAtSeconds = entry.CreatedAtSeconds
    }
end

local function escape_field(value)
    value = tostring(value or "")
    value = string.gsub(value, "%%", "%%25")
    value = string.gsub(value, "\t", "%%09")
    value = string.gsub(value, "\r", "%%0D")
    value = string.gsub(value, "\n", "%%0A")
    return value
end

local function unescape_field(value)
    value = tostring(value or "")
    return string.gsub(value, "%%(%x%x)", function(hex)
        return string.char(tonumber(hex, 16))
    end)
end

local function split_tab_line(line)
    local fields = {}
    line = tostring(line or "") .. "\t"
    for field in string.gmatch(line, "(.-)\t") do
        table.insert(fields, field)
    end
    return fields
end

local function normalize_entry(entry)
    entry = entry or {}
    return {
        Rank = to_positive_integer(entry.Rank, 1),
        RecordId = to_positive_integer(entry.RecordId, 1),
        TotalTimeSeconds = to_number_or_zero(entry.TotalTimeSeconds),
        ElapsedTimeSeconds = to_number_or_zero(entry.ElapsedTimeSeconds),
        PlayerName = tostring(entry.PlayerName or "Player"),
        Score = to_number_or_zero(entry.Score),
        ClearReason = tostring(entry.ClearReason or "ClearGame"),
        CreatedAtSeconds = to_number_or_zero(entry.CreatedAtSeconds)
    }
end

local function sort_entries(entries)
    table.sort(entries, function(a, b)
        if a.TotalTimeSeconds == b.TotalTimeSeconds then
            if a.Score == b.Score then
                return a.RecordId < b.RecordId
            end
            return a.Score > b.Score
        end
        return a.TotalTimeSeconds < b.TotalTimeSeconds
    end)
end

local function refresh_ranks(entries)
    for index, entry in ipairs(entries) do
        entry.Rank = index
    end
end

local function trim_entries(entries, maxEntries)
    while #entries > maxEntries do
        table.remove(entries)
    end
end

local function next_record_id_from_entries(entries, fallback)
    local nextId = to_positive_integer(fallback, 1)
    for _, entry in ipairs(entries) do
        local recordId = to_positive_integer(entry.RecordId, 0)
        if recordId >= nextId then
            nextId = recordId + 1
        end
    end
    return nextId
end

function LeaderboardManager:AddClearRecord(record)
    self:EnsureLoaded()
    record = record or {}

    local entry = normalize_entry({
        Rank = 0,
        RecordId = self.NextRecordId,
        TotalTimeSeconds = record.TotalTimeSeconds,
        ElapsedTimeSeconds = record.ElapsedTimeSeconds,
        PlayerName = record.PlayerName,
        Score = record.Score,
        ClearReason = record.ClearReason,
        CreatedAtSeconds = record.CreatedAtSeconds
    })

    self.NextRecordId = self.NextRecordId + 1
    table.insert(self.Entries, entry)

    sort_entries(self.Entries)
    trim_entries(self.Entries, self.MaxEntries)
    refresh_ranks(self.Entries)
    self.LastRecord = entry
    self:Save()
    return copy_entry(entry)
end

function LeaderboardManager:GetEntryCount()
    self:EnsureLoaded()
    return #self.Entries
end

function LeaderboardManager:GetEntry(index)
    self:EnsureLoaded()
    index = math.floor(tonumber(index) or 0)
    return copy_entry(self.Entries[index])
end

function LeaderboardManager:GetEntries()
    self:EnsureLoaded()
    local result = {}
    for index, entry in ipairs(self.Entries) do
        result[index] = copy_entry(entry)
    end
    return result
end

function LeaderboardManager:GetBestEntry()
    self:EnsureLoaded()
    return copy_entry(self.Entries[1])
end

function LeaderboardManager:GetLastRecord()
    self:EnsureLoaded()
    return copy_entry(self.LastRecord)
end

function LeaderboardManager:Serialize()
    self:EnsureLoaded()

    local lines = {}
    table.insert(lines, table.concat({
        SERIALIZE_HEADER,
        SERIALIZE_VERSION,
        tostring(self.MaxEntries),
        tostring(self.NextRecordId)
    }, "\t"))

    for _, entry in ipairs(self.Entries) do
        local fields = { "Entry" }
        for _, key in ipairs(FIELD_ORDER) do
            table.insert(fields, escape_field(entry[key]))
        end
        table.insert(lines, table.concat(fields, "\t"))
    end

    return table.concat(lines, "\n") .. "\n"
end

function LeaderboardManager:Deserialize(text)
    if text == nil or text == "" then
        self:Reset()
        return true
    end

    local entries = {}
    local nextRecordId = 1
    local bHeaderRead = false

    for line in string.gmatch(tostring(text) .. "\n", "([^\r\n]*)\r?\n") do
        if line ~= "" then
            local fields = split_tab_line(line)
            if not bHeaderRead then
                if fields[1] ~= SERIALIZE_HEADER or fields[2] ~= SERIALIZE_VERSION then
                    return false
                end
                self.MaxEntries = to_positive_integer(fields[3], self.MaxEntries)
                nextRecordId = to_positive_integer(fields[4], 1)
                bHeaderRead = true
            elseif fields[1] == "Entry" then
                local entry = {}
                for index, key in ipairs(FIELD_ORDER) do
                    entry[key] = unescape_field(fields[index + 1])
                end
                table.insert(entries, normalize_entry(entry))
            end
        end
    end

    if not bHeaderRead then
        self:Reset()
        return true
    end

    sort_entries(entries)
    trim_entries(entries, self.MaxEntries)
    refresh_ranks(entries)

    self.Entries = entries
    self.LastRecord = nil
    self.NextRecordId = next_record_id_from_entries(entries, nextRecordId)
    self.bLoadedFromDisk = true
    return true
end

function LeaderboardManager:Load(path)
    local storagePath = path or self.StoragePath
    if World == nil or World.ReadSaveTextFile == nil then
        self.bLoadedFromDisk = true
        return false
    end

    local text = World.ReadSaveTextFile(storagePath)
    if text == nil then
        self.bLoadedFromDisk = true
        return false
    end

    local ok = self:Deserialize(text)
    self.bLoadedFromDisk = true
    return ok
end

function LeaderboardManager:Save(path)
    local storagePath = path or self.StoragePath
    if World == nil or World.WriteSaveTextFile == nil then
        return false
    end

    return World.WriteSaveTextFile(storagePath, self:Serialize()) == true
end

function LeaderboardManager:EnsureLoaded()
    if self.bLoadedFromDisk then
        return true
    end
    self:Load()
    self.bLoadedFromDisk = true
    return true
end

function LeaderboardManager:Reset()
    self.Entries = {}
    self.LastRecord = nil
    self.NextRecordId = 1
    self.bLoadedFromDisk = true
end

function LeaderboardManager:Reload()
    self.Entries = {}
    self.LastRecord = nil
    self.NextRecordId = 1
    self.bLoadedFromDisk = false
    return self:Load()
end

function LeaderboardManager:ClearSavedRecords()
    self:Reset()
    return self:Save()
end

return LeaderboardManager
