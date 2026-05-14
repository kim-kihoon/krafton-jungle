function BeginPlay(owner)
    if owner == nil then
        print("[Item][Lua] Globe BeginPlay owner=nil")
        return
    end

    local itemId = "globe"
    local registered = RegisterItemActor(owner, itemId)
    print("[Item][Lua] Register " .. itemId .. " actor=" .. owner:GetName() .. " result=" .. tostring(registered))
end
