local LoopManager = require("LoopManager")

local StageManager = {}

StageManager.MAX_STAGE = 6
StageManager.TUTORIAL_STAGE = 1

function StageManager:GetStage()
    return LoopManager:GetWarpCount() + 1
end

function StageManager:CanZoneWarp()
    return self:GetStage() < self.MAX_STAGE
end

function StageManager:IsTutorialStage()
    return self:GetStage() == self.TUTORIAL_STAGE
end

function StageManager:IsFinalStage()
    return self:GetStage() == self.MAX_STAGE
end

return StageManager
