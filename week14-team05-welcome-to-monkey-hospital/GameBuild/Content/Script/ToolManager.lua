local ToolManager = {}

ToolManager.Tool = {
    Pistol = 0,
    Camera = 1
}

ToolManager.CurrentTool = ToolManager.Tool.Pistol

function ToolManager:Reset()
    self.CurrentTool = self.Tool.Pistol
end

function ToolManager:GetCurrentTool()
    return self.CurrentTool
end

function ToolManager:SetCurrentTool(tool)
    if tool ~= self.Tool.Pistol and tool ~= self.Tool.Camera then
        return false
    end

    self.CurrentTool = tool
    return true
end

function ToolManager:IsPistol()
    return self.CurrentTool == self.Tool.Pistol
end

function ToolManager:IsCamera()
    return self.CurrentTool == self.Tool.Camera
end

function ToolManager:GetNextTool()
    if self.CurrentTool == self.Tool.Pistol then
        return self.Tool.Camera
    end
    return self.Tool.Pistol
end

function ToolManager:Toggle()
    self.CurrentTool = self:GetNextTool()
    return self.CurrentTool
end

function ToolManager:GetControlPromptMode()
    if self:IsCamera() then
        return "Camera"
    end
    return "Pistol"
end

return ToolManager
